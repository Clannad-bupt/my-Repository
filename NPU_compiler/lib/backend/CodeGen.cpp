// CodeGen:mininpu IR → minisa Executable(README §6/§9.5)。
//
// 直译规则:
//   sram_alloc/dram_alloc/func 实参 → 描述符(sram base=静态偏移;dram base 留 0,
//     runtime 按 meta.json dram 列表 bump 分配回填);
//   view → 沿链解析出 (root, offset, shape, stride),生成独立描述符
//     (DRAM 视图的 base = parent base + offset,经 meta.json views 由 runtime 回填);
//   dma_load/dma_store/gemm_tile/vec_* → 对应 opcode;token → 定义它的 op 所在通道;
//   sync → BAR(mask = 各 token 通道并集);
//   wait 操作数合法性检查:token 须被某个前置 sync 覆盖,或与消费者同通道(FIFO 保序)。
#include "mininpuc/backend/CodeGen.h"

#include "mininpuc/Float16.h"
#include "mininpuc/midend/MiniNPUDialect.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

#include <map>
#include <stdexcept>
#include <string>

using namespace mlir;

namespace mininpu {
namespace mn = mininpu::npu;

namespace {

struct Resolved {
  uint32_t rootId = 0;   // 最终 DRAM/SRAM buffer 的 desc id
  int64_t offsetBytes = 0;
  llvm::SmallVector<int64_t> shape;
  llvm::SmallVector<int64_t> strides; // 元素计
  uint8_t memspace = MS_DRAM;
  uint8_t dtype = DT_F16;
  bool isView = false;    // 与 root 不同基址解析(dram 视图需 runtime 回填)
};

uint8_t dtypeOf(Type elem) {
  if (elem.isF16()) return DT_F16;
  if (elem.isF32()) return DT_F32;
  if (elem.isInteger(8)) return DT_I8;   // 二期-INT8(§6.5)
  if (elem.isInteger(32)) return DT_I32;
  if (elem.isBF16()) return DT_BF16;     // 三期(§6.6)
  if (isa<Float8E4M3FNType>(elem)) return DT_F8E4M3; // 三期(§6.6):IR 仅容器标签
  throw std::runtime_error("codegen: 只支持 f16/f32/bf16/i8/i32/f8E4M3FN");
}

// 视图描述符去重 key:几何(形状/stride)与最终地址的全部决定因素。
// SRAM 视图为绝对静态偏移;DRAM 视图为 parent base + offset(parent 也须入 key)。
std::string viewKey(const Resolved& r, uint32_t parentId) {
  std::string k = std::to_string(r.memspace) + "|" + std::to_string(r.dtype) +
                  "|" + std::to_string(r.memspace == MS_DRAM ? parentId : 0) +
                  "|" + std::to_string(r.offsetBytes) + "|";
  for (size_t i = 0; i < r.shape.size(); ++i)
    k += std::to_string(r.shape[i]) + "x" + std::to_string(r.strides[i]) + ";";
  return k;
}

class Emitter {
public:
  explicit Emitter(func::FuncOp f) : f_(f) {}

  CodeGenResult run() {
    // 函数实参 → DRAM 描述符(按参数顺序,保证 meta/const 表稳定)
    for (BlockArgument arg : f_.getBody().front().getArguments()) {
      Resolved r = resolveBase(arg);
      uint32_t id = addDesc(r, /*isView=*/false);
      descOf_[arg] = id;
      res_.inputs.push_back({id, "arg" + std::to_string(arg.getArgNumber())});
    }

    for (Operation& op : f_.getBody().front().getOperations()) {
      if (auto a = dyn_cast<mn::SramAllocOp>(&op)) { emitAlloc(a); continue; }
      if (auto a = dyn_cast<mn::DramAllocOp>(&op)) { emitAlloc(a); continue; }
      if (isa<mn::ViewOp>(&op)) continue; // 惰性:用到时沿链解析
      if (isa<mn::SramDeallocOp>(&op)) continue;
      if (auto l = dyn_cast<mn::DmaLoadOp>(&op)) { emitDma(l); continue; }
      if (auto s = dyn_cast<mn::DmaStoreOp>(&op)) { emitDma(s); continue; }
      if (auto g = dyn_cast<mn::GemmTileOp>(&op)) { emitGemm(g); continue; }
      if (auto r = dyn_cast<mn::VecRmsOp>(&op)) { emitVecRms(r); continue; }
      if (auto v = dyn_cast<mn::VecSiluOp>(&op)) { emitVecEltwise(v, OP_V_SILU); continue; }
      if (auto v = dyn_cast<mn::VecMulOp>(&op)) { emitVecEltwise(v, OP_V_MUL); continue; }
      if (auto v = dyn_cast<mn::VecAddOp>(&op)) { emitVecEltwise(v, OP_V_ADD); continue; }
      if (auto q = dyn_cast<mn::VecQuantOp>(&op)) { emitVecQuant(q); continue; }
      if (auto d = dyn_cast<mn::VecDequantOp>(&op)) { emitVecDequant(d); continue; }
      if (auto s = dyn_cast<mn::SyncOp>(&op)) { emitSync(s); continue; }
      if (auto ret = dyn_cast<func::ReturnOp>(&op)) { emitReturn(ret); continue; }
      throw std::runtime_error(std::string("codegen: 不支持的 op: ") +
                               op.getName().getStringRef().str());
    }
    res_.exe.code.push_back(mkInstr(OP_END, 0, 0, 0, 0));
    if (nextId_ > 256)
      throw std::runtime_error("codegen: 描述符数量超过 256(§6.1;视图已按几何去重,"
                               "剩余为真实不同的 buffer/形状);请缩小演示规模");
    return std::move(res_);
  }

private:
  func::FuncOp f_;
  CodeGenResult res_;
  uint32_t nextId_ = 0;
  DenseMap<Value, uint32_t> descOf_;      // memref value -> desc id(含 view)
  DenseMap<Value, int> tokenChannel_;     // token -> 通道
  DenseSet<Value> covered_;               // 已被某个前置 sync 覆盖的 token
  DenseMap<uint32_t, bool> isViewDesc_;   // dram 视图 desc(运行时不参与 bump 分配)
  // 视图描述符去重:unrolled tile 循环会对同一 (槽位基址,形状,stride) 反复
  // 创建 view op;几何相同的视图共享描述符,否则 §6.1 的 256 上限被
  // 程序规模(而非不同 buffer 数)打爆(512³ GEMM 曾产生 ~2600 个描述符)。
  std::map<std::string, uint32_t> viewDedup_;

  static llvm::SmallVector<int64_t> contigStrides(ArrayRef<int64_t> shape) {
    llvm::SmallVector<int64_t> st(shape.size());
    for (size_t d = shape.size(); d-- > 0;)
      st[d] = (d + 1 == shape.size()) ? 1 : st[d + 1] * shape[d + 1];
    return st;
  }

  // 无 layout(或 identity)的 base buffer 解析。
  // 注意 MLIR 21:MemRefType::get 会把 null layout 替换为 identity AffineMapAttr
  // (打印时省略),故不能按 null 判断,统一走 MemRefLayoutAttrInterface::isIdentity。
  Resolved resolveBase(Value v) {
    auto mt = cast<MemRefType>(v.getType());
    Resolved r;
    r.shape.assign(mt.getShape().begin(), mt.getShape().end());
    r.strides = contigStrides(r.shape);
    if (!mt.getLayout().isIdentity()) {
      std::string msg = "codegen: base buffer 不应带非 identity layout; type=";
      std::string tyStr;
      llvm::raw_string_ostream os(tyStr);
      mt.print(os);
      msg += os.str();
      if (Operation* def = v.getDefiningOp())
        msg += std::string(" (def=") + def->getName().getStringRef().str() + ")";
      else
        msg += " (block arg)";
      throw std::runtime_error(msg);
    }
    auto space = mt.getMemorySpace();
    r.memspace = (space && cast<IntegerAttr>(space).getInt() == 1) ? MS_SRAM : MS_DRAM;
    r.dtype = dtypeOf(mt.getElementType());
    if (r.shape.empty() || r.shape.size() > 4)
      throw std::runtime_error("codegen: rank 必须 ∈ [1,4](§6.4)");
    if (Operation* def = v.getDefiningOp())
      if (auto a = dyn_cast<mn::SramAllocOp>(def))
        r.offsetBytes = a.getBaseOffset();
    return r;
  }

  uint32_t addDesc(const Resolved& r, bool isView, uint32_t parentId = 0) {
    uint32_t id = nextId_++;
    Descriptor d;
    d.id = id;
    d.memspace = r.memspace;
    d.dtype = r.dtype;
    d.rank = static_cast<uint8_t>(r.shape.size());
    uint32_t es = dtypeSize(static_cast<DType>(r.dtype));
    int64_t span = 1;
    for (size_t i = 0; i < r.shape.size(); ++i) {
      if (r.shape[i] <= 0 || r.shape[i] > INT32_MAX)
        throw std::runtime_error("codegen: shape 越界");
      d.shape[i] = static_cast<uint32_t>(r.shape[i]);
      d.stride[i] = static_cast<int32_t>(r.strides[i]);
      span += (r.shape[i] - 1) * r.strides[i];
    }
    d.bytes = static_cast<uint64_t>(span) * es;
    if (r.memspace == MS_SRAM) {
      d.base = static_cast<uint64_t>(r.offsetBytes); // 编译期静态偏移
    } else if (isView) {
      d.base = 0; // runtime 经 meta views 回填 = parent base + offset
      isViewDesc_[id] = true;
      res_.views.push_back({id, parentId, r.offsetBytes});
    } else {
      d.base = 0; // runtime bump 分配
    }
    res_.exe.descs.push_back(d);
    return id;
  }

  // 沿 view 链解析任意 memref value → desc id(惰性建描述符)
  uint32_t descIdOf(Value v) {
    auto it = descOf_.find(v);
    if (it != descOf_.end()) return it->second;

    Operation* def = v.getDefiningOp();
    if (!def) throw std::runtime_error("codegen: 未解析的 block 实参?");
    if (isa<mn::SramAllocOp, mn::DramAllocOp>(def)) {
      Resolved r = resolveBase(v);
      uint32_t id = addDesc(r, /*isView=*/false);
      descOf_[v] = id;
      return id;
    }
    if (auto view = dyn_cast<mn::ViewOp>(def)) {
      auto outTy = cast<MemRefType>(view.getOut().getType());
      auto layout = dyn_cast<StridedLayoutAttr>(outTy.getLayout());
      if (!layout)
        throw std::runtime_error("codegen: view 必须带 strided layout");
      Resolved root = resolveChain(view.getSrc()); // 解析到根
      Resolved r;
      r.rootId = root.rootId;
      r.memspace = root.memspace;
      r.dtype = root.dtype;
      r.shape.assign(outTy.getShape().begin(), outTy.getShape().end());
      r.strides.assign(layout.getStrides().begin(), layout.getStrides().end());
      uint32_t es = dtypeSize(static_cast<DType>(r.dtype));
      r.offsetBytes = root.offsetBytes + layout.getOffset() * es;
      r.isView = true;
      if (r.shape.empty() || r.shape.size() > 4)
        throw std::runtime_error("codegen: view rank 必须 ∈ [1,4]");
      // 去重:几何完全相同的视图(同一基址/父 buffer、同形状 stride)共享 id
      std::string key = viewKey(r, root.rootId);
      auto dup = viewDedup_.find(key);
      if (dup != viewDedup_.end()) {
        descOf_[v] = dup->second;
        return dup->second;
      }
      bool isDramView = r.memspace == MS_DRAM;
      uint32_t id = addDesc(r, isDramView, root.rootId);
      if (!isDramView) { // sram 视图:静态基址直接展开
        res_.exe.descs.back().base = static_cast<uint64_t>(r.offsetBytes);
      }
      descOf_[v] = id;
      viewDedup_[key] = id;
      return id;
    }
    throw std::runtime_error(std::string("codegen: 非 buffer 值: ") +
                             def->getName().getStringRef().str());
  }

  Resolved resolveChain(Value v) {
    Operation* def = v.getDefiningOp();
    if (!def) { // block 实参
      Resolved r = resolveBase(v);
      r.rootId = descIdOf(v);
      return r;
    }
    if (auto view = dyn_cast<mn::ViewOp>(def)) {
      Resolved parent = resolveChain(view.getSrc());
      auto outTy = cast<MemRefType>(view.getOut().getType());
      auto layout = cast<StridedLayoutAttr>(outTy.getLayout());
      uint32_t es = dtypeSize(static_cast<DType>(parent.dtype));
      parent.offsetBytes += layout.getOffset() * es;
      parent.shape.assign(outTy.getShape().begin(), outTy.getShape().end());
      parent.strides.assign(layout.getStrides().begin(), layout.getStrides().end());
      return parent;
    }
    if (isa<mn::SramAllocOp, mn::DramAllocOp>(def)) {
      Resolved r = resolveBase(v);
      r.rootId = descIdOf(v);
      return r;
    }
    throw std::runtime_error("codegen: view 链解析失败");
  }

  template <typename AllocOpT> void emitAlloc(AllocOpT a) { (void)descIdOf(a.getResult()); }

  int channelOfToken(Value tok, Operation* user) {
    auto it = tokenChannel_.find(tok);
    if (it == tokenChannel_.end())
      throw std::runtime_error(std::string("codegen: ") +
                               user->getName().getStringRef().str() +
                               " 的 wait token 不是异步 op 产出");
    return it->second;
  }

  void checkCovered(Value tok, int consumerCh, Operation* user) {
    int prodCh = channelOfToken(tok, user);
    if (prodCh == consumerCh) return; // 同通道 FIFO 保序
    if (covered_.contains(tok)) return;
    throw std::runtime_error(
        std::string("codegen: ") + user->getName().getStringRef().str() +
        " 的 wait token 未被任何前置 sync 覆盖(§5.5);跨通道依赖必须经 sync");
  }

  int64_t byteOffOf(IntegerAttr attr, Operation* op) {
    int64_t v = attr.getInt();
    if (v < 0 || v > UINT32_MAX)
      throw std::runtime_error(std::string("codegen: ") +
                               op->getName().getStringRef().str() +
                               " 的 byte_offset 超出 u32 imm 范围");
    return v;
  }

  void emitDma(mn::DmaLoadOp l) {
    uint32_t src = descIdOf(l.getSrc()), dst = descIdOf(l.getDst());
    uint64_t imm = static_cast<uint64_t>(byteOffOf(l.getByteOffsetAttr(), l));
    res_.exe.code.push_back(mkInstr(OP_DMA_LD, dst, src, 0, imm));
    tokenChannel_[l.getToken()] = CH_DMA_LD;
  }
  void emitDma(mn::DmaStoreOp s) {
    uint32_t src = descIdOf(s.getSrc()), dst = descIdOf(s.getDst());
    checkCovered(s.getWait(), CH_DMA_ST, s);
    uint64_t imm = static_cast<uint64_t>(byteOffOf(s.getByteOffsetAttr(), s));
    res_.exe.code.push_back(mkInstr(OP_DMA_ST, dst, src, 0, imm));
    tokenChannel_[s.getToken()] = CH_DMA_ST;
  }
  void emitGemm(mn::GemmTileOp g) {
    checkCovered(g.getWait(), CH_GEMM, g);
    uint32_t a = descIdOf(g.getA()), b = descIdOf(g.getB()), c = descIdOf(g.getC());
    const Descriptor& A = res_.exe.descs[a];
    const Descriptor& B = res_.exe.descs[b];
    const Descriptor& C = res_.exe.descs[c];
    if (A.shape[1] != B.shape[0] || A.shape[0] != C.shape[0] || B.shape[1] != C.shape[1])
      throw std::runtime_error("codegen: gemm_tile 描述符形状不一致");
    uint64_t tm = A.shape[0], tk = A.shape[1], tn = B.shape[1];
    if (tm > 0xffff || tn > 0xffff || tk > 0xffff)
      throw std::runtime_error("codegen: tile 尺寸超出 16-bit imm(§6.3)");
    // 二期-INT8 / 三期(§6.5/§6.6):gemm_tile 按操作数 dtype 分派——
    // i8 A/B + i32 C 为精确整数路径;f8E4M3 A/B + f32 C 为 FP8 路径
    // (e4m3×e4m3 乘积 f32 精确,累加误差仅 f32 RNE);f16/f32/bf16 三元
    // 一致为浮点 MAC 路径(bf16 跨 Tk 部分和 bf16 量化,镜像 §2.3)
    uint8_t op;
    if (A.dtype == DT_I8 && B.dtype == DT_I8 && C.dtype == DT_I32) {
      op = OP_GEMM_I8;
    } else if (A.dtype == DT_F8E4M3 && B.dtype == DT_F8E4M3 && C.dtype == DT_F32) {
      op = OP_GEMM_F8;
    } else if ((A.dtype == DT_F16 || A.dtype == DT_F32 || A.dtype == DT_BF16) &&
               A.dtype == B.dtype && A.dtype == C.dtype) {
      op = OP_GEMM;
    } else {
      throw std::runtime_error(
          "codegen: gemm_tile dtype 组合不支持(合法:f16/f32/bf16 三元一致 / "
          "i8,i8→i32 / f8,f8→f32)");
    }
    uint32_t imm = (static_cast<uint32_t>(tn) << 16) | static_cast<uint32_t>(tm);
    res_.exe.code.push_back(mkInstr(op, c, a, b, imm));
    res_.exe.code.push_back(mkGemmWord1(static_cast<uint16_t>(tk),
                                        static_cast<uint8_t>(g.getAccMode()), 0, 0));
    tokenChannel_[g.getToken()] = CH_GEMM;
  }
  // 二期-INT8(§6.5)/ 三期-FP8(§6.6):vec_quant → OP_V_QUANT;vec_dequant →
  // OP_V_DEQUANT,第 4 操作数 sa 经 imm_hi 低 8 bit(§6.5 编码惯例)。
  void emitVecQuant(mn::VecQuantOp q) {
    checkCovered(q.getWait(), CH_VEC, q);
    uint32_t x = descIdOf(q.getX()), sa = descIdOf(q.getSa()),
             y = descIdOf(q.getY());
    checkDescAny(y, {DT_I8, DT_F8E4M3}, "vec_quant.y");
    res_.exe.code.push_back(mkInstr(OP_V_QUANT, y, x, sa, 0));
    tokenChannel_[q.getToken()] = CH_VEC;
  }
  void emitVecDequant(mn::VecDequantOp d) {
    checkCovered(d.getWait(), CH_VEC, d);
    uint32_t x = descIdOf(d.getX()), sb = descIdOf(d.getSb()),
             sa = descIdOf(d.getSa()), y = descIdOf(d.getY());
    checkDescAny(x, {DT_I32, DT_F32}, "vec_dequant.x");
    checkDescAny(y, {DT_F16, DT_BF16}, "vec_dequant.y");
    if (sa > 0xff)
      throw std::runtime_error("codegen: vec_dequant.sa 描述符 id 超出 imm_hi 8 bit");
    res_.exe.code.push_back(
        mkInstr(OP_V_DEQUANT, y, x, sb, static_cast<uint32_t>(sa) << 16));
    tokenChannel_[d.getToken()] = CH_VEC;
  }
  void checkDescAny(uint32_t id, std::initializer_list<uint8_t> expect, const char* what) {
    for (uint8_t e : expect)
      if (res_.exe.descs[id].dtype == e) return;
    throw std::runtime_error(std::string("codegen: ") + what + " dtype 不符(§6.5/§6.6)");
  }
  void emitVecRms(mn::VecRmsOp r) {
    checkCovered(r.getWait(), CH_VEC, r);
    uint32_t x = descIdOf(r.getX()), g = descIdOf(r.getGamma()), y = descIdOf(r.getY());
    // eps imm 编码按输出 elem dtype(§6.6):bf16 图用 bf16 位型;f16/f32 图
    // 沿用 f16 编码(Sim 侧对称解码,f32 图精度足够存 1e-6 级 eps)
    float eps = static_cast<float>(r.getEps().convertToDouble());
    uint16_t epsBits = res_.exe.descs[y].dtype == DT_BF16 ? f32ToBf16Bits(eps)
                                                          : f32ToF16Bits(eps);
    res_.exe.code.push_back(mkInstr(OP_V_RMS, y, x, g, epsBits));
    tokenChannel_[r.getToken()] = CH_VEC;
  }
  template <typename VecOpT> void emitVecEltwise(VecOpT v, uint8_t opcode) {
    checkCovered(v.getWait(), CH_VEC, v);
    uint32_t a = descIdOf(v.getA()), b = descIdOf(v.getB()), y = descIdOf(v.getY());
    res_.exe.code.push_back(mkInstr(opcode, y, a, b, 0));
    tokenChannel_[v.getToken()] = CH_VEC;
  }
  void emitSync(mn::SyncOp s) {
    uint16_t mask = 0;
    for (Value tok : s.getWaits()) {
      mask |= chMask(channelOfToken(tok, s));
      covered_.insert(tok);
    }
    if (mask) res_.exe.code.push_back(mkInstr(OP_BAR, 0, 0, 0, mask));
  }
  void emitReturn(func::ReturnOp ret) {
    for (Value v : ret.getOperands())
      res_.outputs.push_back({descIdOf(v), "out" + std::to_string(res_.outputs.size())});
  }
};

} // namespace

CodeGenResult emitExecutable(ModuleOp module) {
  func::FuncOp found;
  for (Operation& op : module.getBody()->getOperations()) {
    if (auto f = dyn_cast<func::FuncOp>(&op)) {
      if (found) throw std::runtime_error("codegen: 第一期只支持单函数 module");
      found = f;
    }
  }
  if (!found) throw std::runtime_error("codegen: module 中没有 func.func");
  Emitter e(found);
  CodeGenResult r = e.run();
  r.entry = found.getSymName().str();
  return r;
}

} // namespace mininpu
