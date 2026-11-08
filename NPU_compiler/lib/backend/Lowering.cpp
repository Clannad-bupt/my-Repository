// minigraph → mininpu lowering(README §9.4/§9.5,§13 步骤 6-7)。
//
// 第一期策略(§13 步骤 7):每个 minigraph op 独立模板展开,算子间经 DRAM 物化。
//   - matmul:m→n→k 循环嵌套 + K 向双缓冲(§9.4 模板),C tile 片上 epilogue
//     (bias → V_ADD 广播; silu → V_SILU)后 DMA_ST;
//   - rms_norm / silu / mul / add:行块 tile + x/y 双缓冲流水;
//   - reshape → DRAM view(纯元数据);transpose 必须由中端折进 matmul(否则报错);
//   - SRAM:全部槽位先以 base_offset=0 创建,lowering 结束后统一线性扫描分配
//     (§9.3),bank 偏好 A→0 B→1 C→2。
// 依赖通过显式 token + sync 表达(§5.5);sync → BAR 的翻译在 CodeGen。
#include "mininpuc/backend/Lowering.h"

#include "mininpuc/backend/SramAllocator.h"
#include "mininpuc/backend/TilePlanner.h"
#include "mininpuc/midend/MiniGraphDialect.h"
#include "mininpuc/midend/MiniNPUDialect.h"
#include "mininpuc/minisa.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

#include <sstream>
#include <stdexcept>
#include <string>

using namespace mlir;

namespace mininpu {
namespace mg = mininpu::graph;
namespace mn = mininpu::npu;

std::string LoweringReport::str() const {
  std::string s;
  for (const auto& e : entries) { s += "- "; s += e; s += "\n"; }
  return s;
}

namespace {

int64_t numelOf(ArrayRef<int64_t> s) {
  int64_t n = 1;
  for (int64_t d : s) n *= d;
  return n;
}

uint32_t elemSizeOf(Type t) {
  if (t.isF16()) return 2;
  if (t.isF32()) return 4;
  if (t.isInteger(8)) return 1;   // 二期-INT8(§6.5)
  if (t.isInteger(32)) return 4;
  if (t.isBF16()) return 2;       // 三期(§6.6)
  if (isa<Float8E4M3FNType>(t)) return 1; // 三期(§6.6):IR 容器标签,es 同 i8
  throw std::runtime_error("lowering: 只支持 f16/f32/bf16/i8/i32/f8E4M3FN(§2.3/§6.6)");
}

// DRAM 侧逻辑视图(shape/stride 可能与存储不同:transpose/reshape 经 view 表达)
struct DramRef {
  Value v;
  llvm::SmallVector<int64_t> shape;
  llvm::SmallVector<int64_t> strides; // 元素计
};

class LoweringDriver {
public:
  LoweringDriver(func::FuncOp f, const HardwareSpec& spec, LoweringReport* report)
      : f_(f), spec_(spec), report_(report), b_(f.getContext()), ctx_(f.getContext()) {
    dramSpace_ = b_.getI64IntegerAttr(0);
    sramSpace_ = b_.getI64IntegerAttr(1);
  }

  void run() {
    // vmap:函数参数(tensor)即 DRAM buffer(类型在最后统一改为 memref)
    for (BlockArgument arg : f_.getBody().front().getArguments())
      vmap_[arg] = arg;

    bool failed = false;
    // early-increment 遍历:lowering 过程中会创建/擦除 op
    for (Operation& op :
         llvm::make_early_inc_range(f_.getBody().front().getOperations())) {
      if (auto m = dyn_cast<mg::MatmulOp>(&op)) { failed |= !lowerMatmul(m); continue; }
      if (auto q = dyn_cast<mg::QuantMatmulOp>(&op)) {
        failed |= !lowerQuantMatmul(q);
        continue;
      }
      if (auto q = dyn_cast<mg::QuantOp>(&op)) { failed |= !lowerQuant(q); continue; }
      if (auto r = dyn_cast<mg::RmsNormOp>(&op)) { failed |= !lowerRmsNorm(r); continue; }
      if (auto s = dyn_cast<mg::SiluOp>(&op)) {
        failed |= !lowerEltwise(s, s.getX(), s.getResult(), "silu");
        continue;
      }
      if (auto m = dyn_cast<mg::MulOp>(&op)) {
        failed |= !lowerEltwise(m, m.getLhs(), m.getResult(), "mul", m.getRhs());
        continue;
      }
      if (auto a = dyn_cast<mg::AddOp>(&op)) {
        failed |= !lowerEltwise(a, a.getLhs(), a.getResult(), "add", a.getRhs());
        continue;
      }
      if (auto r = dyn_cast<mg::ReshapeOp>(&op)) { failed |= !lowerReshape(r); continue; }
      if (auto t = dyn_cast<mg::TransposeOp>(&op)) {
        t.emitOpError("standalone transpose 第一期不支持:请先跑 eliminate-transpose,"
                      "让 transpose 折进 matmul 的 transpose_* 标志(§8)");
        failed = true;
        continue;
      }
      if (auto r = dyn_cast<mg::RopeOp>(&op)) {
        r.emitOpError("rope 为第二期预留(§5.2.5)");
        failed = true;
        continue;
      }
    }
    if (failed) throw std::runtime_error("lowering 失败(见上方诊断)");

    allocateSram();
    convertSignature();
    // 延迟擦除:return 改写(vmap 映射)之后,旧 minigraph op 的结果才没有使用者。
    // 逆序擦除(使用者先于其 operand 的 def),避免 def 先死时仍有存活使用者。
    for (Operation* op : llvm::reverse(toErase_)) op->erase();
    // sliceOf 为 store 计算的 sram 侧视图未被使用(store 几何由 src 决定)——清掉死视图
    for (Operation& op :
         llvm::make_early_inc_range(f_.getBody().front().getOperations()))
      if (isa<mn::ViewOp>(&op) && op.use_empty()) op.erase();
  }

private:
  // ---------- 基础设施 ----------
  func::FuncOp f_;
  const HardwareSpec& spec_;
  LoweringReport* report_;
  OpBuilder b_;
  MLIRContext* ctx_;
  Attribute dramSpace_, sramSpace_;
  DenseMap<Value, Value> vmap_;            // minigraph tensor -> dram memref
  DenseMap<Value, int> bankPref_;          // sram_alloc value -> bank 偏好
  DenseMap<Value, std::string> allocName_; // sram_alloc value -> 可读名
  llvm::SmallVector<Operation*> toErase_;  // 已 lowering 的 minigraph op(延迟擦除)
  int opCounter_ = 0;

  Location loc(Operation* op) { return op->getLoc(); }
  Type tokenTy() { return mn::TokenType::get(ctx_); }

  MemRefType memrefTy(ArrayRef<int64_t> shape, Type elem, Attribute space) {
    return MemRefType::get(shape, elem, /*layout=*/MemRefLayoutAttrInterface{},
                           space);
  }

  Type elemOf(Value v) {
    if (auto mt = dyn_cast<MemRefType>(v.getType())) return mt.getElementType();
    return cast<RankedTensorType>(v.getType()).getElementType();
  }
  Attribute spaceOf(Value v) { // tensor(函数实参)视为 dram
    if (auto mt = dyn_cast<MemRefType>(v.getType())) return mt.getMemorySpace();
    return dramSpace_;
  }

  Value sramAlloc(Operation* at, ArrayRef<int64_t> shape, Type elem,
                  int bank, StringRef name) {
    OpBuilder::InsertionGuard g(b_);
    b_.setInsertionPoint(at);
    auto op = mn::SramAllocOp::create(b_, loc(at),
                                      memrefTy(shape, elem, sramSpace_),
                                      b_.getI64IntegerAttr(0));
    bankPref_[op.getResult()] = bank;
    allocName_[op.getResult()] = name.str();
    return op.getResult();
  }

  Value dramAlloc(Operation* at, ArrayRef<int64_t> shape, Type elem,
                  const std::string& name) {
    OpBuilder::InsertionGuard g(b_);
    b_.setInsertionPoint(at);
    return mn::DramAllocOp::create(b_, loc(at), memrefTy(shape, elem, dramSpace_),
                                   b_.getI64IntegerAttr(64),
                                   b_.getStringAttr(name))
        .getResult();
  }

  // 纯元数据视图:同块内存的 shape/stride/offset 重解释(§MiniNPU_ViewOp)
  Value viewOf(Operation* at, Value src, ArrayRef<int64_t> shape,
               ArrayRef<int64_t> strides, int64_t offsetElems = 0) {
    OpBuilder::InsertionGuard g(b_);
    b_.setInsertionPoint(at);
    auto layout = StridedLayoutAttr::get(ctx_, offsetElems, strides);
    auto ty = MemRefType::get(shape, elemOf(src), layout, spaceOf(src));
    return mn::ViewOp::create(b_, loc(at), ty, src).getResult();
  }

  Value dmaLoad(Operation* at, Value srcDram, Value dstSram, int64_t byteOff) {
    OpBuilder::InsertionGuard g(b_);
    b_.setInsertionPoint(at);
    return mn::DmaLoadOp::create(b_, loc(at), tokenTy(), srcDram, dstSram,
                                 b_.getI64IntegerAttr(byteOff))
        .getToken();
  }
  Value dmaStore(Operation* at, Value srcSram, Value dstDram, Value wait,
                 int64_t byteOff) {
    OpBuilder::InsertionGuard g(b_);
    b_.setInsertionPoint(at);
    return mn::DmaStoreOp::create(b_, loc(at), tokenTy(), srcSram, dstDram, wait,
                                  b_.getI64IntegerAttr(byteOff))
        .getToken();
  }
  Value gemmTile(Operation* at, Value a, Value bM, Value c, Value wait,
                 int64_t acc) {
    OpBuilder::InsertionGuard g(b_);
    b_.setInsertionPoint(at);
    return mn::GemmTileOp::create(b_, loc(at), tokenTy(), a, bM, c, wait,
                                  b_.getI64IntegerAttr(acc))
        .getToken();
  }
  Value vecRms(Operation* at, Value x, Value gamma, Value y, Value wait,
               double eps) {
    OpBuilder::InsertionGuard g(b_);
    b_.setInsertionPoint(at);
    return mn::VecRmsOp::create(b_, loc(at), tokenTy(), x, gamma, y, wait,
                                b_.getF64FloatAttr(eps))
        .getToken();
  }
  Value vecOp(Operation* at, StringRef kind, Value a, Value bM, Value y,
              Value wait) {
    OpBuilder::InsertionGuard g(b_);
    b_.setInsertionPoint(at);
    if (kind == "silu")
      return mn::VecSiluOp::create(b_, loc(at), tokenTy(), a, bM, y, wait).getToken();
    if (kind == "mul")
      return mn::VecMulOp::create(b_, loc(at), tokenTy(), a, bM, y, wait).getToken();
    return mn::VecAddOp::create(b_, loc(at), tokenTy(), a, bM, y, wait).getToken();
  }
  void sync(Operation* at, llvm::SmallVector<Value> toks) {
    if (toks.empty()) return;
    OpBuilder::InsertionGuard g(b_);
    b_.setInsertionPoint(at);
    mn::SyncOp::create(b_, loc(at), toks);
  }
  // 二期-INT8(§6.5)
  Value vecQuant(Operation* at, Value x, Value sa, Value y, Value wait) {
    OpBuilder::InsertionGuard g(b_);
    b_.setInsertionPoint(at);
    return mn::VecQuantOp::create(b_, loc(at), tokenTy(), x, sa, y, wait)
        .getToken();
  }
  Value vecDequant(Operation* at, Value x, Value sb, Value sa, Value y,
                   Value wait) {
    OpBuilder::InsertionGuard g(b_);
    b_.setInsertionPoint(at);
    return mn::VecDequantOp::create(b_, loc(at), tokenTy(), x, sb, sa, y, wait)
        .getToken();
  }

  // DRAM 切片 DMA:sliceShape(取 sram 侧几何),offsets 为各维起始元素偏移。
  // 返回 (dst view, byte_offset)。检查 32B 对齐(§6.1/§9.4)。
  std::pair<Value, int64_t> sliceOf(Operation* at, const DramRef& src,
                                    Value sramSlot,
                                    ArrayRef<int64_t> sliceShape,
                                    ArrayRef<int64_t> offsets,
                                    StringRef what) {
    uint32_t es = elemSizeOf(elemOf(src.v));
    int64_t imm = 0;
    for (size_t d = 0; d < offsets.size(); ++d) imm += offsets[d] * src.strides[d];
    imm *= es;
    if (imm % 32 != 0)
      throw std::runtime_error("lowering: " + what.str() + " 切片 imm 非 32B 对齐(" +
                               std::to_string(imm) + ");请保证 K/N 维为 16 倍数(§9.4)");
    if ((sliceShape.back() * int64_t(es)) % 32 != 0)
      throw std::runtime_error(
          "lowering: " + what.str() + " 残块 inner 非 32B 对齐(shape 末维 " +
          std::to_string(sliceShape.back()) +
          ");第一期要求残块末维仍为 16 倍数(§9.4 padding 方案留作扩展)");
    // sram 侧切片视图:紧凑 contiguous
    llvm::SmallVector<int64_t> st(sliceShape.size());
    for (size_t d = sliceShape.size(); d-- > 0;)
      st[d] = (d + 1 == sliceShape.size()) ? 1 : st[d + 1] * sliceShape[d + 1];
    Value dst = viewOf(at, sramSlot, sliceShape, st);
    return {dst, imm};
  }

  Value mapIn(Operation* op, Value tensor, const char* what) {
    auto it = vmap_.find(tensor);
    if (it == vmap_.end())
      throw std::runtime_error(std::string("lowering: ") + what +
                               " 的输入未物化(非 minigraph 产出/参数?)");
    return it->second;
  }

  // ---------- matmul ----------
  bool lowerMatmul(mg::MatmulOp op) {
    auto aT = cast<RankedTensorType>(op.getA().getType());
    auto bT = cast<RankedTensorType>(op.getB().getType());
    bool ta = op.getTransposeA(), tb = op.getTransposeB();
    int64_t ar = aT.getShape()[0], ac = aT.getShape()[1];
    int64_t br = bT.getShape()[0], bc = bT.getShape()[1];
    int64_t M = ta ? ac : ar, K = ta ? ar : ac;
    int64_t N = tb ? br : bc;
    Type elem = aT.getElementType();
    uint32_t es = elemSizeOf(elem);
    std::string me = "mm" + std::to_string(opCounter_++);
    Location L = loc(op);

    TilePlan plan;
    try {
      plan = planGemmTile(M, N, K, es, spec_, /*doubleBuffer=*/true);
    } catch (const std::runtime_error& e) {
      op.emitOpError("tile 规划失败: ") << e.what();
      return false;
    }
    if (report_)
      report_->add(me + " matmul M=" + std::to_string(M) + " N=" + std::to_string(N) +
                   " K=" + std::to_string(K) + " → " + plan.reason);

    // DRAM 侧逻辑视图(transpose 折进 stride)
    Value aStored = mapIn(op, op.getA(), "matmul.a");
    Value bStored = mapIn(op, op.getB(), "matmul.b");
    DramRef A{aStored, {M, K}, ta ? llvm::SmallVector<int64_t>{1, ac}
                                  : llvm::SmallVector<int64_t>{ac, 1}};
    DramRef B{bStored, {K, N}, tb ? llvm::SmallVector<int64_t>{1, bc}
                                  : llvm::SmallVector<int64_t>{bc, 1}};
    if (ta) A.v = viewOf(op, aStored, A.shape, A.strides);
    if (tb) B.v = viewOf(op, bStored, B.shape, B.strides);

    StringRef ep = op.getEpilogue().value_or("");
    bool hasBias = op.getBias() != nullptr;
    Value cDram = dramAlloc(op, {M, N}, elem, me + ".out");
    DramRef C{cDram, {M, N}, {N, 1}};

    // SRAM 槽位:A0/A1、B0/B1(双缓冲)、C 单份(§9.4)
    const uint32_t Tm = plan.tm, Tn = plan.tn, Tk = plan.tk;
    Value aS[2] = {sramAlloc(op, {Tm, Tk}, elem, 0, me + ".A0"),
                   sramAlloc(op, {Tm, Tk}, elem, 0, me + ".A1")};
    Value bS[2] = {sramAlloc(op, {Tk, Tn}, elem, 1, me + ".B0"),
                   sramAlloc(op, {Tk, Tn}, elem, 1, me + ".B1")};
    Value cS = sramAlloc(op, {Tm, Tn}, elem, 2, me + ".C");
    Value biasS;
    Value biasD;
    if (hasBias) {
      biasS = sramAlloc(op, {int64_t(Tn)}, elem, 3, me + ".bias");
      biasD = mapIn(op, op.getBias(), "matmul.bias");
    }

    Value pendingSt; // 上一个 (mt,nt) 的 C 回写 token(WAR:C 槽复用)
    for (uint32_t mt = 0; mt < plan.tilesM; ++mt) {
      int64_t m0 = int64_t(mt) * Tm;
      int64_t tmEff = std::min<int64_t>(Tm, M - m0);
      for (uint32_t nt = 0; nt < plan.tilesN; ++nt) {
        int64_t n0 = int64_t(nt) * Tn;
        int64_t tnEff = std::min<int64_t>(Tn, N - n0);

        auto tkEff = [&](uint32_t kt) -> int64_t {
          return std::min<int64_t>(Tk, K - int64_t(kt) * Tk);
        };
        // ---- prologue:k=0 切片(+ bias),sync ----
        llvm::SmallVector<Value> proToks;
        Value la0, lb0;
        {
          auto [dv, imm] = sliceOf(op, A, aS[0], {tmEff, tkEff(0)}, {m0, 0}, me + ".A");
          la0 = dmaLoad(op, A.v, dv, imm);
          proToks.push_back(la0);
        }
        {
          auto [dv, imm] = sliceOf(op, B, bS[0], {tkEff(0), tnEff}, {0, n0}, me + ".B");
          lb0 = dmaLoad(op, B.v, dv, imm);
          proToks.push_back(lb0);
        }
        Value biasTok;
        if (hasBias) {
          DramRef biasR{biasD, {N}, {1}};
          auto [dv, imm] = sliceOf(op, biasR, biasS, {tnEff}, {n0}, me + ".bias");
          biasTok = dmaLoad(op, biasR.v, dv, imm);
          proToks.push_back(biasTok);
        }
        if (pendingSt) proToks.push_back(pendingSt); // C 槽 WAR
        sync(op, proToks);

        // ---- k 循环(双缓冲模板,§9.4)----
        // 注意:sram 侧视图必须与 DMA 写入的紧凑布局一致(残块时行距=有效宽)
        Value cv = viewOf(op, cS, {tmEff, tnEff}, {tnEff, 1});
        Value lastLd = la0, lastGemm;
        int p = 0;
        for (uint32_t kt = 0; kt < plan.tilesK; ++kt) {
          llvm::SmallVector<Value> syncToks;
          // 本拍 gemm 的数据凭据:prologue 载入(kt=0)或上一拍预取(kt>0),
          // 两者均已被前置 sync 覆盖。须先保存,再让 lastLd 指向本拍预取。
          Value curLd = lastLd;
          if (kt + 1 < plan.tilesK) { // 预取下一拍到另一组槽位
            auto [dva, imma] =
                sliceOf(op, A, aS[1 - p], {tmEff, tkEff(kt + 1)},
                        {m0, int64_t(kt + 1) * Tk}, me + ".A");
            Value la = dmaLoad(op, A.v, dva, imma);
            auto [dvb, immb] =
                sliceOf(op, B, bS[1 - p], {tkEff(kt + 1), tnEff},
                        {int64_t(kt + 1) * Tk, n0}, me + ".B");
            Value lb = dmaLoad(op, B.v, dvb, immb);
            lastLd = la;
            syncToks.push_back(la);
            syncToks.push_back(lb);
          }
          Value av = viewOf(op, aS[p], {tmEff, tkEff(kt)}, {tkEff(kt), 1});
          Value bv = viewOf(op, bS[p], {tkEff(kt), tnEff}, {tnEff, 1});
          lastGemm = gemmTile(op, av, bv, cv, curLd, /*acc=*/kt > 0 ? 1 : 0);
          syncToks.insert(syncToks.begin(), lastGemm);
          sync(op, syncToks);
          p = 1 - p;
        }

        // ---- epilogue(片上,§13 步骤 6:bias→V_ADD 广播;silu→V_SILU)----
        Value lastVec;
        if (hasBias) {
          Value biasV = viewOf(op, biasS, {tnEff}, {1});
          lastVec = vecOp(op, "add", cv, biasV, cv, lastGemm);
        }
        if (ep == "silu" || ep == "bias_silu")
          lastVec = vecOp(op, "silu", cv, cv, cv, lastVec ? lastVec : lastGemm);
        if (lastVec) sync(op, {lastVec});
        auto [dummy, imm] = sliceOf(op, C, cS, {tmEff, tnEff}, {m0, n0}, me + ".C");
        (void)dummy; // store 的切片几何由 src(sram 侧)决定;dst 用 DRAM 整体 + imm
        pendingSt = dmaStore(op, cv, C.v, lastVec ? lastVec : lastGemm, imm);
      }
    }
    sync(op, {pendingSt});

    vmap_[op.getResult()] = cDram;
    toErase_.push_back(op);
    return true;
  }

  // ---------- 二期-INT8(§6.5)/ 三期-FP8(§6.6):quant_matmul ----------
  // 与 lowerMatmul 同构:A/B 槽位 8-bit 容器(i8 或 f8E4M3FN),C 槽 i32
  // (i8:跨 Tk 精确整数累加)或 f32(f8:e4m3×e4m3 乘积精确,累加误差仅
  // f32 RNE),K 归约完成后 V_DEQUANT 一次性落到出块槽再 DMA_ST;
  // sa/sb 逐 (mt,nt) 切片;len-1(per-tensor,FP8 标准粒度)整载一次。
  // 依赖要点:c2S 复用的 WAR 由 pendingSt 覆盖;cS 被上一 tile 的
  // V_DEQUANT 读、被本 tile 首个 GEMM(acc=0)覆写,故 pendingDq 也要进
  // prologue sync(GEMM 与 VEC 不同通道,不能靠 FIFO)。
  bool lowerQuantMatmul(mg::QuantMatmulOp op) {
    auto aT = cast<RankedTensorType>(op.getA().getType());
    auto bT = cast<RankedTensorType>(op.getB().getType());
    auto saT = cast<RankedTensorType>(op.getSa().getType());
    auto sbT = cast<RankedTensorType>(op.getSb().getType());
    int64_t M = aT.getShape()[0], K = aT.getShape()[1], N = bT.getShape()[1];
    int64_t saLen = saT.getShape()[0]; // M(per-token)或 1(per-tensor)
    int64_t sbLen = sbT.getShape()[0]; // N(per-channel)或 1(per-tensor)
    Type elemAB = aT.getElementType(); // i8 或 f8E4M3FN(verifier 保证)
    Type elemAcc =
        elemAB.isInteger(8) ? Type(b_.getI32Type()) : Type(b_.getF32Type());
    Type outElem =
        cast<RankedTensorType>(op.getResult().getType()).getElementType();
    uint32_t esOut = elemSizeOf(outElem);
    bool isF8 = isa<Float8E4M3FNType>(elemAB);
    std::string me = std::string(isF8 ? "qmmF8" : "qmm") +
                     std::to_string(opCounter_++);
    if (M % 32 || N % 32 || K % 32) {
      op.emitOpError(
          "quant_matmul 第一期要求 M/N/K 均为 32 倍数(8-bit 容器内维 32B DMA "
          "对齐,§6.5/§6.6)");
      return false;
    }

    GemmDtypeOpts dt; // i8 与 f8E4M3FN 容器字节数一致:esAB=1、esC=4(i32/f32)
    dt.esAB = 1;
    dt.esC = 4;
    dt.esOut = esOut;
    dt.esScale = esOut; // sa/sb 与输出同位宽(verifier 已保证)
    dt.step = 32;
    TilePlan plan;
    try {
      plan = planGemmTile(M, N, K, /*es=*/1, spec_, /*doubleBuffer=*/true,
                          /*vecScratch=*/0, dt);
    } catch (const std::runtime_error& e) {
      op.emitOpError("tile 规划失败: ") << e.what();
      return false;
    }
    if (report_)
      report_->add(me + " quant_matmul M=" + std::to_string(M) +
                   " N=" + std::to_string(N) + " K=" + std::to_string(K) +
                   " → " + plan.reason);

    Value aD = mapIn(op, op.getA(), "quant_matmul.a");
    Value bD = mapIn(op, op.getB(), "quant_matmul.b");
    Value saD = mapIn(op, op.getSa(), "quant_matmul.sa");
    Value sbD = mapIn(op, op.getSb(), "quant_matmul.sb");
    DramRef A{aD, {M, K}, {K, 1}};
    DramRef B{bD, {K, N}, {N, 1}};
    // len-1(per-tensor)scale:DRAM 侧 stride-0 广播视图——npy/描述符只含
    // 1 个元素,DMA 以 32B inner 把它复制 padN 份进 SRAM(§6.6);vec 侧用
    // (1,) 视图触发广播分支。padN = 32B / esOut(f16→16、f32→8)
    const int64_t padN = 32 / int64_t(esOut);
    DramRef SA{saD, {saLen}, {1}};
    if (saLen == 1) SA = DramRef{viewOf(op, saD, {padN}, {0}), {padN}, {0}};
    DramRef SB{sbD, {sbLen}, {1}};
    if (sbLen == 1) SB = DramRef{viewOf(op, sbD, {padN}, {0}), {padN}, {0}};
    Value cDram = dramAlloc(op, {M, N}, outElem, me + ".out");
    DramRef C{cDram, {M, N}, {N, 1}};

    const uint32_t Tm = plan.tm, Tn = plan.tn, Tk = plan.tk;
    Value aS[2] = {sramAlloc(op, {int64_t(Tm), int64_t(Tk)}, elemAB, 0, me + ".A0"),
                   sramAlloc(op, {int64_t(Tm), int64_t(Tk)}, elemAB, 0, me + ".A1")};
    Value bS[2] = {sramAlloc(op, {int64_t(Tk), int64_t(Tn)}, elemAB, 1, me + ".B0"),
                   sramAlloc(op, {int64_t(Tk), int64_t(Tn)}, elemAB, 1, me + ".B1")};
    Value cS = sramAlloc(op, {int64_t(Tm), int64_t(Tn)}, elemAcc, 2, me + ".Cacc");
    Value c2S = sramAlloc(op, {int64_t(Tm), int64_t(Tn)}, outElem, 2, me + ".C");
    Value saS = sramAlloc(op, {saLen == 1 ? padN : int64_t(Tm)}, outElem, 3,
                          me + ".sa");
    Value sbS = sramAlloc(op, {sbLen == 1 ? padN : int64_t(Tn)}, outElem, 3,
                          me + ".sb");
    // per-tensor scale 一次性载入(槽位只写一次,无 WAR)
    Value saTok1, sbTok1;
    if (saLen == 1) saTok1 = dmaLoad(op, SA.v, saS, 0);
    if (sbLen == 1) sbTok1 = dmaLoad(op, SB.v, sbS, 0);

    Value pendingSt, pendingDq; // C 槽 WAR;i32 槽 RAW(见函数头注释)
    for (uint32_t mt = 0; mt < plan.tilesM; ++mt) {
      int64_t m0 = int64_t(mt) * Tm;
      int64_t tmEff = std::min<int64_t>(Tm, M - m0);
      for (uint32_t nt = 0; nt < plan.tilesN; ++nt) {
        int64_t n0 = int64_t(nt) * Tn;
        int64_t tnEff = std::min<int64_t>(Tn, N - n0);

        auto tkEff = [&](uint32_t kt) -> int64_t {
          return std::min<int64_t>(Tk, K - int64_t(kt) * Tk);
        };
        // ---- prologue:k=0 切片 + sa/sb 切片 + 两个 WAR/RAW token,sync ----
        llvm::SmallVector<Value> proToks;
        Value la0, lb0;
        {
          auto [dv, imm] = sliceOf(op, A, aS[0], {tmEff, tkEff(0)}, {m0, 0}, me + ".A");
          la0 = dmaLoad(op, A.v, dv, imm);
          proToks.push_back(la0);
        }
        {
          auto [dv, imm] = sliceOf(op, B, bS[0], {tkEff(0), tnEff}, {0, n0}, me + ".B");
          lb0 = dmaLoad(op, B.v, dv, imm);
          proToks.push_back(lb0);
        }
        if (saLen > 1) {
          auto [dv, imm] = sliceOf(op, SA, saS, {tmEff}, {m0}, me + ".sa");
          proToks.push_back(dmaLoad(op, SA.v, dv, imm));
        } else {
          proToks.push_back(saTok1); // 一次性 token;进 sync 仅为覆盖首次使用
        }
        if (sbLen > 1) {
          auto [dv, imm] = sliceOf(op, SB, sbS, {tnEff}, {n0}, me + ".sb");
          proToks.push_back(dmaLoad(op, SB.v, dv, imm));
        } else {
          proToks.push_back(sbTok1);
        }
        if (pendingSt) proToks.push_back(pendingSt);
        if (pendingDq) proToks.push_back(pendingDq);
        sync(op, proToks);

        Value cv = viewOf(op, cS, {tmEff, tnEff}, {tnEff, 1});
        Value cv2 = viewOf(op, c2S, {tmEff, tnEff}, {tnEff, 1});
        // len-1 scale 交给 vec 的是 (1,) 视图 → sim 广播分支(§6.6)
        Value saV = saLen == 1 ? viewOf(op, saS, {1}, {1})
                               : viewOf(op, saS, {tmEff}, {1});
        Value sbV = sbLen == 1 ? viewOf(op, sbS, {1}, {1})
                               : viewOf(op, sbS, {tnEff}, {1});
        Value lastLd = la0, lastGemm;
        int p = 0;
        for (uint32_t kt = 0; kt < plan.tilesK; ++kt) {
          llvm::SmallVector<Value> syncToks;
          Value curLd = lastLd;
          if (kt + 1 < plan.tilesK) {
            auto [dva, imma] =
                sliceOf(op, A, aS[1 - p], {tmEff, tkEff(kt + 1)},
                        {m0, int64_t(kt + 1) * Tk}, me + ".A");
            Value la = dmaLoad(op, A.v, dva, imma);
            auto [dvb, immb] =
                sliceOf(op, B, bS[1 - p], {tkEff(kt + 1), tnEff},
                        {int64_t(kt + 1) * Tk, n0}, me + ".B");
            Value lb = dmaLoad(op, B.v, dvb, immb);
            lastLd = la;
            syncToks.push_back(la);
            syncToks.push_back(lb);
          }
          Value av = viewOf(op, aS[p], {tmEff, tkEff(kt)}, {tkEff(kt), 1});
          Value bv = viewOf(op, bS[p], {tkEff(kt), tnEff}, {tnEff, 1});
          lastGemm = gemmTile(op, av, bv, cv, curLd, /*acc=*/kt > 0 ? 1 : 0);
          syncToks.insert(syncToks.begin(), lastGemm);
          sync(op, syncToks);
          p = 1 - p;
        }

        // ---- dequant 出块(一次性:i32/f32 累加块 → f16/bf16),再回写 ----
        Value dq = vecDequant(op, cv, sbV, saV, cv2, lastGemm);
        sync(op, {dq});
        auto [dummy, imm] = sliceOf(op, C, c2S, {tmEff, tnEff}, {m0, n0}, me + ".C");
        (void)dummy;
        pendingSt = dmaStore(op, cv2, C.v, dq, imm);
        pendingDq = dq;
      }
    }
    sync(op, {pendingSt});

    vmap_[op.getResult()] = cDram;
    toErase_.push_back(op);
    return true;
  }

  // ---------- 二期-INT8(§6.5)/ 三期-FP8(§6.6):quant ----------
  // 行块模板(同 eltwise):x/y 双缓冲;sa 为 per-token(行)或 per-tensor
  // (len-1)scale。per-token 逐 tile 载切片(FIFO 内晚于同 tile 的 x,故
  // quant 的 wait 取 sa token 即覆盖两者);per-tensor 整载一次(stride-0
  // 广播 DMA,§6.6),此时 vec 的 wait 改取本 tile x 的载入 token(直接数据
  // 依赖;sa token 已被 prologue sync 覆盖)。
  // perRow = x(2H) + y 8-bit(1H) 双缓冲 ≈ 6H;sa 槽 ≤ 1024 行(Tr 上限),预
  // 留 2048B scratch。要求 H 为 32 倍数(8-bit 容器出块 inner 32B 对齐)。
  bool lowerQuant(mg::QuantOp op) {
    auto xT = cast<RankedTensorType>(op.getX().getType());
    int64_t rows = xT.getShape()[0], H = xT.getShape()[1];
    Type elem = xT.getElementType();
    Type elemY = cast<RankedTensorType>(op.getResult().getType())
                     .getElementType(); // i8 或 f8E4M3FN
    uint32_t es = elemSizeOf(elem);
    int64_t saLen = cast<RankedTensorType>(op.getSa().getType()).getShape()[0];
    std::string me = std::string(isa<Float8E4M3FNType>(elemY) ? "quantF8" : "quant") +
                     std::to_string(opCounter_++);
    if (H % 32 != 0 || (H * int64_t(es)) % 32 != 0) {
      op.emitOpError(
          "quant 第一期要求末维 H 为 32 倍数(8-bit 容器输出 32B 对齐,§6.5/§6.6)");
      return false;
    }

    Value xD = mapIn(op, op.getX(), "quant.x");
    Value saD = mapIn(op, op.getSa(), "quant.sa");
    Value yD = dramAlloc(op, xT.getShape(), elemY, me + ".out");
    DramRef X{xD, {rows, H}, {H, 1}};
    DramRef Y{yD, {rows, H}, {H, 1}};
    // len-1(per-tensor)sa:stride-0 广播视图(同 quant_matmul,§6.6)
    const int64_t padN = 32 / int64_t(es);
    DramRef SA{saD, {saLen}, {1}};
    if (saLen == 1) SA = DramRef{viewOf(op, saD, {padN}, {0}), {padN}, {0}};

    bool db = true;
    uint32_t Tr;
    try {
      Tr = planRowTile(H, /*es=*/1, /*buffers=*/6, spec_, /*scratch=*/2048);
    } catch (const std::runtime_error&) {
      db = false;
      try {
        Tr = planRowTile(H, 1, 3, spec_, 2048);
      } catch (const std::runtime_error& e) {
        op.emitOpError("行块规划失败: ") << e.what();
        return false;
      }
    }
    // 收顶到 rows 的 16 倍数上界:Tr≫rows 只会虚占 SRAM 槽位(小行数图)
    int64_t trCap = (rows + 15) / 16 * 16;
    if (int64_t(Tr) > trCap) Tr = static_cast<uint32_t>(trCap);
    if (report_)
      report_->add(me + " rows=" + std::to_string(rows) + " H=" +
                   std::to_string(H) + " → Tr=" + std::to_string(Tr) +
                   (db ? "(x/y 双缓冲)" : "(单缓冲)"));

    int nbuf = db ? 2 : 1;
    llvm::SmallVector<Value> xS(nbuf), yS(nbuf), saS(saLen > 1 ? nbuf : 0);
    for (int i = 0; i < nbuf; ++i) {
      xS[i] = sramAlloc(op, {int64_t(Tr), H}, elem, 0, me + ".X" + std::to_string(i));
      yS[i] = sramAlloc(op, {int64_t(Tr), H}, elemY, 2, me + ".Y" + std::to_string(i));
      if (saLen > 1)
        saS[i] = sramAlloc(op, {int64_t(Tr)}, elem, 3, me + ".sa" + std::to_string(i));
    }
    Value saQ; // per-tensor:len-1 单槽(只写一次,无 WAR)
    if (saLen == 1) saQ = sramAlloc(op, {padN}, elem, 3, me + ".sa");

    int64_t ntiles = (rows + Tr - 1) / Tr;
    auto trEff = [&](int64_t i) { return std::min<int64_t>(Tr, rows - i * Tr); };
    Value lastLd, lastSa;
    {
      auto [dv, imm] = sliceOf(op, X, xS[0], {trEff(0), H}, {0, 0}, me + ".X");
      lastLd = dmaLoad(op, X.v, dv, imm);
      if (saLen > 1) {
        auto [ds, ims] = sliceOf(op, SA, saS[0], {trEff(0)}, {0}, me + ".sa");
        lastSa = dmaLoad(op, SA.v, ds, ims);
      } else {
        lastSa = dmaLoad(op, SA.v, saQ, 0); // stride-0 广播:32B inner 复制 padN 份
      }
    }
    sync(op, {lastLd, lastSa});

    Value pendingSt;
    int p = 0;
    for (int64_t i = 0; i < ntiles; ++i) {
      llvm::SmallVector<Value> syncToks;
      // 本 tile 的 sa/x token 须先保存,再让 lastSa/lastLd 指向预取(同 matmul
      // 的 curLd;per-token 时 sa 晚于 x 同通道发射 ⇒ wait sa 即覆盖两者)
      Value curSa = lastSa;
      Value curLd = lastLd; // per-tensor 时 vec 的 wait 凭据(直接数据依赖)
      if (db && i + 1 < ntiles) {
        auto [dv, imm] =
            sliceOf(op, X, xS[1 - p], {trEff(i + 1), H}, {(i + 1) * Tr, 0}, me + ".X");
        lastLd = dmaLoad(op, X.v, dv, imm);
        syncToks.push_back(lastLd);
        if (saLen > 1) {
          auto [ds, ims] =
              sliceOf(op, SA, saS[1 - p], {trEff(i + 1)}, {(i + 1) * Tr}, me + ".sa");
          lastSa = dmaLoad(op, SA.v, ds, ims);
          syncToks.push_back(lastSa);
        }
      } else if (!db && i > 0) {
        // 单缓冲退化:上一拍 vec/store 已被上一拍 sync 等待;当前 tile 的
        // x(+per-token sa)顺序载入并立即 sync(per-tensor sa 序言一次载毕)
        llvm::SmallVector<Value> ldToks;
        auto [dv, imm] = sliceOf(op, X, xS[0], {trEff(i), H}, {i * Tr, 0}, me + ".X");
        lastLd = dmaLoad(op, X.v, dv, imm);
        ldToks.push_back(lastLd);
        if (saLen > 1) {
          auto [ds, ims] = sliceOf(op, SA, saS[0], {trEff(i)}, {i * Tr}, me + ".sa");
          lastSa = dmaLoad(op, SA.v, ds, ims);
          ldToks.push_back(lastSa);
        }
        sync(op, ldToks);
        curLd = lastLd;
        curSa = lastSa;
      }
      Value xv = viewOf(op, xS[p], {trEff(i), H}, {H, 1});
      Value yv = viewOf(op, yS[p], {trEff(i), H}, {H, 1});
      Value saV = saLen > 1 ? viewOf(op, saS[p], {trEff(i)}, {1})
                            : viewOf(op, saQ, {1}, {1}); // (1,) → 广播分支
      Value v = vecQuant(op, xv, saV, yv, saLen > 1 ? curSa : curLd);
      syncToks.insert(syncToks.begin(), v);
      if (pendingSt) syncToks.push_back(pendingSt);
      sync(op, syncToks);
      auto [dummy, imm] = sliceOf(op, Y, yS[p], {trEff(i), H}, {i * Tr, 0}, me + ".Y");
      (void)dummy;
      pendingSt = dmaStore(op, yv, Y.v, v, imm);
      if (db) p = 1 - p; // 单缓冲只有一组槽,p 恒 0
    }
    sync(op, {pendingSt});

    vmap_[op.getResult()] = yD;
    toErase_.push_back(op);
    return true;
  }

  // ---------- rms_norm ----------
  bool lowerRmsNorm(mg::RmsNormOp op) {
    auto xT = cast<RankedTensorType>(op.getX().getType());
    int64_t H = xT.getShape().back();
    int64_t rows = xT.getNumElements() / H;
    Type elem = xT.getElementType();
    uint32_t es = elemSizeOf(elem);
    std::string me = "rms" + std::to_string(opCounter_++);
    if ((H * int64_t(es)) % 32 != 0) {
      op.emitOpError("H*es 须为 32B 倍数(DMA inner 对齐,§9.4)");
      return false;
    }

    Value xD = mapIn(op, op.getX(), "rms_norm.x");
    Value gD = mapIn(op, op.getGamma(), "rms_norm.gamma");
    Value yD = dramAlloc(op, xT.getShape(), elem, me + ".out");
    // 折叠前导维为 [rows, H](contiguous,纯视图)
    DramRef X{xD, {rows, H}, {H, 1}};
    if (xT.getRank() != 2) X.v = viewOf(op, xD, X.shape, X.strides);
    DramRef Y{yD, {rows, H}, {H, 1}};
    if (xT.getRank() != 2) Y.v = viewOf(op, yD, Y.shape, Y.strides);
    Value gS = sramAlloc(op, {H}, elem, 3, me + ".gamma");

    uint64_t gammaBytes = (H * es + 31) / 32 * 32;
    bool db = true;
    uint32_t Tr;
    try {
      Tr = planRowTile(H, es, /*buffers=*/4, spec_, gammaBytes);
    } catch (const std::runtime_error&) {
      db = false;
      try {
        Tr = planRowTile(H, es, /*buffers=*/2, spec_, gammaBytes);
      } catch (const std::runtime_error& e) {
        op.emitOpError("行块规划失败: ") << e.what();
        return false;
      }
    }
    if (report_)
      report_->add(me + " rms_norm rows=" + std::to_string(rows) +
                   " H=" + std::to_string(H) + " → Tr=" + std::to_string(Tr) +
                   (db ? "(x/y 双缓冲)" : "(单缓冲)"));

    Value xS[2], yS[2];
    int nbuf = db ? 2 : 1;
    for (int i = 0; i < nbuf; ++i) {
      xS[i] = sramAlloc(op, {int64_t(Tr), H}, elem, 0, me + ".X" + std::to_string(i));
      yS[i] = sramAlloc(op, {int64_t(Tr), H}, elem, 2, me + ".Y" + std::to_string(i));
    }

    Value gTok = dmaLoad(op, gD, gS, 0);
    int64_t ntiles = (rows + Tr - 1) / Tr;
    auto trEff = [&](int64_t i) { return std::min<int64_t>(Tr, rows - i * Tr); };
    Value ld0;
    {
      auto [dv, imm] = sliceOf(op, X, xS[0], {trEff(0), H}, {0, 0}, me + ".X");
      ld0 = dmaLoad(op, X.v, dv, imm);
    }
    sync(op, {ld0, gTok});

    Value lastLd = ld0, pendingSt;
    int p = 0;
    for (int64_t i = 0; i < ntiles; ++i) {
      llvm::SmallVector<Value> syncToks;
      // 本 tile 的 x 凭据:序言载入(i=0)或上一拍预取,均已被前置 sync 覆盖;
      // 须先保存,再让 lastLd 指向本拍预取(同 matmul 的 curLd,§9.4)
      Value curLd = lastLd;
      if (db && i + 1 < ntiles) {
        auto [dv, imm] =
            sliceOf(op, X, xS[1 - p], {trEff(i + 1), H}, {(i + 1) * Tr, 0}, me + ".X");
        lastLd = dmaLoad(op, X.v, dv, imm);
        syncToks.push_back(lastLd);
      } else if (!db && i > 0) {
        // 单缓冲退化:上一拍 vec/store 已被上一拍 sync 等待,顺序载入当前
        // tile 后立即 sync——单缓冲本无重叠可言,序列化即正确语义
        auto [dv, imm] = sliceOf(op, X, xS[0], {trEff(i), H}, {i * Tr, 0}, me + ".X");
        lastLd = dmaLoad(op, X.v, dv, imm);
        sync(op, {lastLd});
        curLd = lastLd;
      }
      Value xv = viewOf(op, xS[p], {trEff(i), H}, {H, 1});
      Value yv = viewOf(op, yS[p], {trEff(i), H}, {H, 1});
      Value vr = vecRms(op, xv, gS, yv, curLd, op.getEps().convertToDouble());
      syncToks.insert(syncToks.begin(), vr);
      if (pendingSt) syncToks.push_back(pendingSt); // y 槽 WAR
      sync(op, syncToks);
      auto [dummy, imm] = sliceOf(op, Y, yS[p], {trEff(i), H}, {i * Tr, 0}, me + ".Y");
      (void)dummy;
      pendingSt = dmaStore(op, yv, Y.v, vr, imm);
      if (db) p = 1 - p; // 单缓冲只有一组槽,p 恒 0
    }
    sync(op, {pendingSt});

    vmap_[op.getResult()] = yD;
    toErase_.push_back(op);
    return true;
  }

  // ---------- silu / mul / add ----------
  template <typename OpT>
  bool lowerEltwise(OpT op, Value xTensor, Value resTensor, StringRef kind,
                    Value rhsTensor = nullptr) {
    auto xT = cast<RankedTensorType>(xTensor.getType());
    int64_t H = xT.getShape().back();
    int64_t rows = xT.getNumElements() / H;
    Type elem = xT.getElementType();
    uint32_t es = elemSizeOf(elem);
    std::string me = kind.str() + std::to_string(opCounter_++);
    if ((H * int64_t(es)) % 32 != 0) {
      op.emitOpError("末维*es 须为 32B 倍数(DMA inner 对齐,§9.4)");
      return false;
    }

    Value xD = mapIn(op, xTensor, "eltwise.x");
    DramRef X{xD, {rows, H}, {H, 1}};
    if (xT.getRank() != 2) X.v = viewOf(op, xD, X.shape, X.strides);
    Value yD = dramAlloc(op, xT.getShape(), elem, me + ".out");
    DramRef Y{yD, {rows, H}, {H, 1}};
    if (xT.getRank() != 2) Y.v = viewOf(op, yD, Y.shape, Y.strides);

    // rhs:同形(逐 tile 加载)或 1-D 广播(整载一次,§8.1);silu 无 rhs
    Value rD, rS;
    bool rhs1d = false;
    uint64_t scratch = 0;
    if (rhsTensor) {
      auto rT = cast<RankedTensorType>(rhsTensor.getType());
      rhs1d = rT.getRank() == 1;
      rD = mapIn(op, rhsTensor, "eltwise.rhs");
      if (rhs1d) {
        int64_t RL = rT.getShape().back();
        rS = sramAlloc(op, {RL}, elem, 1, me + ".rhs");
        scratch = (RL * es + 31) / 32 * 32;
      }
    }
    int nInSlots = rhsTensor && !rhs1d ? 2 : 1; // x(+b) 输入槽组数
    int bufGroups = nInSlots + 1;               // + y
    bool db = true;
    uint32_t Tr;
    try {
      Tr = planRowTile(H, es, bufGroups * 2, spec_, scratch);
    } catch (const std::runtime_error&) {
      db = false;
      try {
        Tr = planRowTile(H, es, bufGroups, spec_, scratch);
      } catch (const std::runtime_error& e) {
        op.emitOpError("行块规划失败: ") << e.what();
        return false;
      }
    }
    if (report_)
      report_->add(me + " rows=" + std::to_string(rows) + " H=" + std::to_string(H) +
                   " → Tr=" + std::to_string(Tr) + (db ? "(双缓冲)" : "(单缓冲)"));

    int nbuf = db ? 2 : 1;
    llvm::SmallVector<Value> xS(nbuf), bS(nbuf), yS(nbuf);
    for (int i = 0; i < nbuf; ++i) {
      xS[i] = sramAlloc(op, {int64_t(Tr), H}, elem, 0, me + ".A" + std::to_string(i));
      if (rhsTensor && !rhs1d)
        bS[i] = sramAlloc(op, {int64_t(Tr), H}, elem, 1, me + ".B" + std::to_string(i));
      yS[i] = sramAlloc(op, {int64_t(Tr), H}, elem, 2, me + ".Y" + std::to_string(i));
    }

    DramRef B2;
    if (rhsTensor && !rhs1d) {
      B2 = {rD, {rows, H}, {H, 1}};
      auto rT = cast<RankedTensorType>(rhsTensor.getType());
      if (rT.getRank() != 2) B2.v = viewOf(op, rD, B2.shape, B2.strides);
    }
    Value rTok;
    if (rhs1d) {
      rTok = dmaLoad(op, rD, rS, 0);
      if (kind == "silu") rTok = nullptr; // 不会发生
    }

    int64_t ntiles = (rows + Tr - 1) / Tr;
    auto trEff = [&](int64_t i) { return std::min<int64_t>(Tr, rows - i * Tr); };
    llvm::SmallVector<Value> proToks;
    Value lastLd;
    {
      auto [dv, imm] = sliceOf(op, X, xS[0], {trEff(0), H}, {0, 0}, me + ".A");
      lastLd = dmaLoad(op, X.v, dv, imm);
      proToks.push_back(lastLd);
    }
    if (rhsTensor && !rhs1d) {
      auto [dv, imm] = sliceOf(op, B2, bS[0], {trEff(0), H}, {0, 0}, me + ".B");
      proToks.push_back(dmaLoad(op, B2.v, dv, imm));
    }
    if (rTok) proToks.push_back(rTok);
    sync(op, proToks);

    Value pendingSt;
    int p = 0;
    for (int64_t i = 0; i < ntiles; ++i) {
      llvm::SmallVector<Value> syncToks;
      // 本 tile 的 x 凭据:序言载入(i=0)或上一拍预取,均已被前置 sync 覆盖;
      // 须先保存,再让 lastLd 指向本拍预取(同 matmul 的 curLd,§9.4)
      Value curLd = lastLd;
      if (db && i + 1 < ntiles) {
        auto [dv, imm] =
            sliceOf(op, X, xS[1 - p], {trEff(i + 1), H}, {(i + 1) * Tr, 0}, me + ".A");
        lastLd = dmaLoad(op, X.v, dv, imm);
        syncToks.push_back(lastLd);
        if (rhsTensor && !rhs1d) {
          auto [dvb, immb] =
              sliceOf(op, B2, bS[1 - p], {trEff(i + 1), H}, {(i + 1) * Tr, 0},
                      me + ".B");
          syncToks.push_back(dmaLoad(op, B2.v, dvb, immb));
        }
      } else if (!db && i > 0) {
        // 单缓冲退化:上一拍 vec/store 已被上一拍 sync 等待;当前 tile 的
        // x(+rhs) 顺序载入并立即 sync——单缓冲本无重叠可言,序列化即正确语义
        llvm::SmallVector<Value> ldToks;
        auto [dv, imm] = sliceOf(op, X, xS[0], {trEff(i), H}, {i * Tr, 0}, me + ".A");
        lastLd = dmaLoad(op, X.v, dv, imm);
        ldToks.push_back(lastLd);
        if (rhsTensor && !rhs1d) {
          auto [dvb, immb] =
              sliceOf(op, B2, bS[0], {trEff(i), H}, {i * Tr, 0}, me + ".B");
          ldToks.push_back(dmaLoad(op, B2.v, dvb, immb));
        }
        sync(op, ldToks);
        curLd = lastLd;
      }
      Value xv = viewOf(op, xS[p], {trEff(i), H}, {H, 1});
      Value yv = viewOf(op, yS[p], {trEff(i), H}, {H, 1});
      Value rhs;
      if (kind == "silu") rhs = xv;
      else if (rhs1d) rhs = rS;
      else rhs = viewOf(op, bS[p], {trEff(i), H}, {H, 1});
      Value v = vecOp(op, kind, xv, rhs, yv, curLd);
      syncToks.insert(syncToks.begin(), v);
      if (pendingSt) syncToks.push_back(pendingSt);
      sync(op, syncToks);
      auto [dummy, imm] = sliceOf(op, Y, yS[p], {trEff(i), H}, {i * Tr, 0}, me + ".Y");
      (void)dummy;
      pendingSt = dmaStore(op, yv, Y.v, v, imm);
      if (db) p = 1 - p; // 单缓冲只有一组槽,p 恒 0
    }
    sync(op, {pendingSt});

    vmap_[resTensor] = yD;
    toErase_.push_back(op);
    return true;
  }

  // ---------- reshape:纯视图别名 ----------
  bool lowerReshape(mg::ReshapeOp op) {
    llvm::SmallVector<int64_t> shape;
    for (Attribute e : op.getShape())
      shape.push_back(cast<IntegerAttr>(e).getInt());
    llvm::SmallVector<int64_t> st(shape.size());
    for (size_t d = shape.size(); d-- > 0;)
      st[d] = (d + 1 == shape.size()) ? 1 : st[d + 1] * shape[d + 1];
    if (shape.size() > 4) {
      op.emitOpError("rank > 4 超出描述符能力(§6.4)");
      return false;
    }
    Value src = mapIn(op, op.getX(), "reshape.x");
    vmap_[op.getResult()] = viewOf(op, src, shape, st);
    toErase_.push_back(op);
    return true;
  }

  // ---------- SRAM 统一分配(§9.3)----------
  void allocateSram() {
    // 程序点 = 函数体线性序
    DenseMap<Operation*, int> pt;
    int counter = 0;
    for (Operation& op : f_.getBody().front().getOperations())
      pt[&op] = counter++;

    // 生命周期:alloc 定义点 → 经 view 链传递的最后使用点
    DenseMap<Operation*, int> lastUse; // sram_alloc op -> last pt
    auto rootAlloc = [&](Value v) -> Operation* {
      while (Operation* def = v.getDefiningOp()) {
        if (auto view = dyn_cast<mn::ViewOp>(def)) { v = view.getSrc(); continue; }
        return isa<mn::SramAllocOp>(def) ? def : nullptr;
      }
      return nullptr;
    };
    for (Operation& op : f_.getBody().front().getOperations()) {
      for (Value operand : op.getOperands())
        if (Operation* alloc = rootAlloc(operand))
          lastUse[alloc] = std::max(lastUse.lookup(alloc), pt[&op]);
    }

    std::vector<LiveInterval> ivs;
    std::vector<Operation*> allocOps;
    for (Operation& op : f_.getBody().front().getOperations()) {
      auto alloc = dyn_cast<mn::SramAllocOp>(&op);
      if (!alloc) continue;
      auto mt = cast<MemRefType>(alloc.getResult().getType());
      LiveInterval iv;
      iv.name = allocName_.lookup(alloc.getResult());
      iv.sizeBytes = uint64_t(mt.getNumElements()) * elemSizeOf(mt.getElementType());
      iv.begin = pt[&op];
      auto it = lastUse.find(&op);
      iv.end = (it == lastUse.end()) ? iv.begin + 1 : it->second + 1;
      auto bp = bankPref_.find(alloc.getResult());
      iv.preferredBank = (bp == bankPref_.end()) ? -1 : bp->second;
      ivs.push_back(iv);
      allocOps.push_back(&op);
    }
    std::vector<AllocResult> res;
    try {
      res = SramAllocator(spec_).allocate(ivs);
    } catch (const std::runtime_error& e) {
      throw std::runtime_error(std::string("lowering: SRAM 分配失败:") + e.what());
    }
    uint64_t peak = 0;
    for (size_t i = 0; i < res.size(); ++i) {
      auto alloc = cast<mn::SramAllocOp>(allocOps[i]);
      alloc->setAttr("base_offset", b_.getI64IntegerAttr(int64_t(res[i].base)));
      peak = std::max(peak, res[i].base + ivs[i].sizeBytes);
    }
    if (report_)
      report_->add("SRAM 峰值占用 " + std::to_string(peak) + "B / " +
                   std::to_string(spec_.sramBytes) + "B");
  }

  // ---------- 函数签名:tensor → memref(dram) ----------
  void convertSignature() {
    Block& block = f_.getBody().front();
    llvm::SmallVector<Type> inTys;
    for (BlockArgument arg : block.getArguments()) {
      auto tt = cast<RankedTensorType>(arg.getType());
      auto mt = memrefTy(tt.getShape(), tt.getElementType(), dramSpace_);
      arg.setType(mt);
      inTys.push_back(mt);
    }
    // return 改写
    llvm::SmallVector<Type> outTys;
    Operation* oldRet = nullptr;
    for (Operation& op : block.getOperations())
      if (auto ret = dyn_cast<func::ReturnOp>(&op)) oldRet = &op;
    if (!oldRet) throw std::runtime_error("lowering: 函数缺少 return");
    auto ret = cast<func::ReturnOp>(oldRet);
    llvm::SmallVector<Value> newOperands;
    for (Value r : ret.getOperands()) {
      auto it = vmap_.find(r);
      if (it == vmap_.end())
        throw std::runtime_error("lowering: return 值未物化");
      newOperands.push_back(it->second);
      auto mt = dyn_cast<MemRefType>(it->second.getType());
      if (!mt) // 恒等返回参数的情况:arg 已是 memref
        mt = memrefTy(cast<RankedTensorType>(r.getType()).getShape(),
                      cast<RankedTensorType>(r.getType()).getElementType(),
                      dramSpace_);
      outTys.push_back(mt);
    }
    OpBuilder rb(oldRet);
    func::ReturnOp::create(rb, oldRet->getLoc(), newOperands);
    oldRet->erase();
    f_.setFunctionType(FunctionType::get(ctx_, inTys, outTys));
  }
};

class LowerToMininpuPass
    : public PassWrapper<LowerToMininpuPass, OperationPass<func::FuncOp>> {
public:
  LowerToMininpuPass() = default;
  LowerToMininpuPass(const HardwareSpec& spec, LoweringReport* report)
      : spec_(spec), report_(report) {}
  LowerToMininpuPass(const LowerToMininpuPass& o)
      : spec_(o.spec_), report_(o.report_) {}

  StringRef getArgument() const override { return "minigraph-lower-to-mininpu"; }
  StringRef getDescription() const override {
    return "minigraph → mininpu 模板展开(§9.4,第一期算子间 DRAM 物化)";
  }

  void runOnOperation() override {
    try {
      LoweringDriver(getOperation(), spec_, report_).run();
    } catch (const std::runtime_error& e) {
      getOperation()->emitError(e.what());
      signalPassFailure();
    }
  }

private:
  HardwareSpec spec_;
  LoweringReport* report_ = nullptr;
};

} // namespace

std::unique_ptr<mlir::Pass>
createLowerToMininpuPass(const HardwareSpec& spec, LoweringReport* report) {
  return std::make_unique<LowerToMininpuPass>(spec, report);
}

void registerMininpuBackendPasses() {
  static bool done = [] {
    mlir::registerPass([] { return std::make_unique<LowerToMininpuPass>(); });
    return true;
  }();
  (void)done;
}

} // namespace mininpu
