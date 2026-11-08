// minigraph dialect 实现:dialect 注册 + op verifier(README §5.2)。
#include "mininpuc/midend/MiniGraphDialect.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h" // Float8E4M3FNType(三期)

#include "llvm/ADT/SmallVector.h"

using namespace mlir;
using namespace mininpu::graph;

#include "MiniGraphDialect.cpp.inc"

void MiniGraphDialect::initialize() {
  addOperations<
#define GET_OP_LIST
#include "MiniGraphOps.cpp.inc"
      >();
}

#define GET_OP_CLASSES
#include "MiniGraphOps.cpp.inc"

namespace mininpu::graph {

static RankedTensorType ranked(Type t) { return dyn_cast<RankedTensorType>(t); }

// 取 matmul 输入在「不考虑 transpose flag」下的 (rows, cols)
static std::pair<int64_t, int64_t> dims2D(RankedTensorType t) {
  return {t.getShape()[0], t.getShape()[1]};
}

LogicalResult MatmulOp::verify() {
  auto aT = ranked(getA().getType());
  auto bT = ranked(getB().getType());
  auto cT = ranked(getResult().getType());
  if (!aT || !bT || !cT)
    return emitOpError("operands/result 必须是 ranked tensor");
  if (aT.getRank() != 2 || bT.getRank() != 2 || cT.getRank() != 2)
    return emitOpError("第一期只支持 2D matmul(§5.2.1)");
  auto [ar, ac] = dims2D(aT);
  auto [br, bc] = dims2D(bT);
  int64_t m = getTransposeA() ? ac : ar;
  int64_t kA = getTransposeA() ? ar : ac;
  int64_t kB = getTransposeB() ? bc : br;
  int64_t n = getTransposeB() ? br : bc;
  if (kA != kB)
    return emitOpError("内维 K 不一致:") << kA << " vs " << kB;
  if (cT.getShape()[0] != m || cT.getShape()[1] != n)
    return emitOpError("输出 shape 应为 (M,N)=(")
           << m << "," << n << "),实为 " << cT.getShape();
  if (aT.getElementType() != bT.getElementType() ||
      aT.getElementType() != cT.getElementType())
    return emitOpError("元素类型必须一致(f16/f32/bf16)");

  StringRef ep = getEpilogue().value_or("");
  bool hasBias = getBias() != nullptr;
  if (ep != "" && ep != "silu" && ep != "bias" && ep != "bias_silu")
    return emitOpError("未知 epilogue: ") << ep;
  if (ep.find("bias") != StringRef::npos && !hasBias)
    return emitOpError("epilogue 含 bias 但缺 bias 操作数");
  if (hasBias && ep.find("bias") == StringRef::npos)
    return emitOpError("有 bias 操作数但 epilogue 未含 bias");
  if (hasBias) {
    auto biasT = ranked(getBias().getType());
    if (!biasT || biasT.getRank() != 1 || biasT.getShape()[0] != n)
      return emitOpError("bias 必须是长度 N 的 1-D tensor(§8.1)");
  }
  return success();
}

LogicalResult RmsNormOp::verify() {
  auto xT = ranked(getX().getType());
  auto gT = ranked(getGamma().getType());
  auto yT = ranked(getResult().getType());
  if (!xT || !gT || !yT)
    return emitOpError("operands/result 必须是 ranked tensor");
  if (gT.getRank() != 1)
    return emitOpError("gamma 必须是 1-D");
  int64_t h = xT.getShape()[xT.getRank() - 1];
  if (gT.getShape()[0] != h || yT.getShape() != xT.getShape())
    return emitOpError("gamma 长度须等于最后一维 H,且输出形状等于输入");
  return success();
}

LogicalResult SiluOp::verify() {
  auto xT = ranked(getX().getType());
  auto yT = ranked(getResult().getType());
  if (!xT || !yT || xT.getShape() != yT.getShape())
    return emitOpError("silu 输出形状必须等于输入");
  return success();
}

static LogicalResult verifyEltwise(Operation* op, Type lhs, Type rhs, Type res) {
  auto lT = ranked(lhs), rT = ranked(rhs), yT = ranked(res);
  if (!lT || !rT || !yT)
    return op->emitOpError("operands/result 必须是 ranked tensor");
  if (lT.getShape() != yT.getShape())
    return op->emitOpError("输出形状必须等于 lhs");
  // 同形,或 rhs 为长度等于末维的 1-D(bias 广播,§8.1)
  if (rT.getShape() != lT.getShape() &&
      !(rT.getRank() == 1 && rT.getShape()[0] == lT.getShape()[lT.getRank() - 1]))
    return op->emitOpError("rhs 须与 lhs 同形,或为长度=末维的 1-D bias(§8.1)");
  return success();
}

LogicalResult MulOp::verify() {
  return verifyEltwise(*this, getLhs().getType(), getRhs().getType(),
                       getResult().getType());
}

LogicalResult AddOp::verify() {
  return verifyEltwise(*this, getLhs().getType(), getRhs().getType(),
                       getResult().getType());
}

LogicalResult TransposeOp::verify() {
  auto xT = ranked(getX().getType());
  auto yT = ranked(getResult().getType());
  if (!xT || !yT)
    return emitOpError("operands/result 必须是 ranked tensor");
  SmallVector<int64_t> perm;
  for (Attribute e : getPermutation())
    perm.push_back(cast<IntegerAttr>(e).getInt());
  if (static_cast<int64_t>(perm.size()) != xT.getRank())
    return emitOpError("permutation 长度须等于 rank");
  llvm::SmallVector<bool> seen(xT.getRank(), false);
  for (int64_t p : perm) {
    if (p < 0 || p >= xT.getRank() || seen[p])
      return emitOpError("permutation 非法");
    seen[p] = true;
  }
  for (int64_t i = 0; i < xT.getRank(); ++i)
    if (yT.getShape()[i] != xT.getShape()[perm[i]])
      return emitOpError("输出形状与 permutation 不一致");
  return success();
}

LogicalResult ReshapeOp::verify() {
  auto xT = ranked(getX().getType());
  auto yT = ranked(getResult().getType());
  if (!xT || !yT)
    return emitOpError("operands/result 必须是 ranked tensor");
  int64_t numel = 1;
  for (Attribute e : getShape()) numel *= cast<IntegerAttr>(e).getInt();
  if (numel != xT.getNumElements() ||
      yT.getNumElements() != xT.getNumElements())
    return emitOpError("reshape 必须保持元素数");
  return success();
}

// ---- 二期-INT8(§6.5)/ 三期-FP8(§6.6)----
// 8-bit 容器判定:i8(二期)或 f8E4M3FN(三期,IR 仅作标签,算术语义在模拟器)
static bool isI8OrF8(Type t) {
  return t.isInteger(8) || isa<Float8E4M3FNType>(t);
}

LogicalResult QuantOp::verify() {
  auto xT = ranked(getX().getType());
  auto sT = ranked(getSa().getType());
  auto yT = ranked(getResult().getType());
  if (!xT || !sT || !yT)
    return emitOpError("operands/result 必须是 ranked tensor");
  if (xT.getRank() != 2)
    return emitOpError("第一期 quant 只支持 2D 输入(§6.5)");
  // sa:长度=行数(per-token)或 1(per-tensor,FP8 标准粒度,广播)
  if (sT.getRank() != 1 ||
      (sT.getShape()[0] != xT.getShape()[0] && sT.getShape()[0] != 1))
    return emitOpError("sa 必须是长度=行数(首维)或 1 的 1-D scale");
  if (!isI8OrF8(yT.getElementType()) || yT.getShape() != xT.getShape())
    return emitOpError("输出必须是与输入同形状的 i8/f8E4M3FN tensor");
  return success();
}

LogicalResult QuantMatmulOp::verify() {
  auto aT = ranked(getA().getType());
  auto bT = ranked(getB().getType());
  auto saT = ranked(getSa().getType());
  auto sbT = ranked(getSb().getType());
  auto cT = ranked(getResult().getType());
  if (!aT || !bT || !saT || !sbT || !cT)
    return emitOpError("operands/result 必须是 ranked tensor");
  if (aT.getRank() != 2 || bT.getRank() != 2 || cT.getRank() != 2)
    return emitOpError("第一期 quant_matmul 只支持 2D(§6.5)");
  if (!isI8OrF8(aT.getElementType()) || !isI8OrF8(bT.getElementType()) ||
      aT.getElementType() != bT.getElementType())
    return emitOpError("a/b 必须是同一种 8-bit 容器(i8 或 f8E4M3FN)");
  int64_t m = aT.getShape()[0], kA = aT.getShape()[1];
  int64_t kB = bT.getShape()[0], n = bT.getShape()[1];
  if (kA != kB)
    return emitOpError("内维 K 不一致:") << kA << " vs " << kB;
  if (cT.getShape()[0] != m || cT.getShape()[1] != n)
    return emitOpError("输出 shape 应为 (M,N)=(")
           << m << "," << n << "),实为 " << cT.getShape();
  // sa ∈ {M,1}(per-token / per-tensor),sb ∈ {N,1}(per-channel / per-tensor)
  if (saT.getRank() != 1 || (saT.getShape()[0] != m && saT.getShape()[0] != 1))
    return emitOpError("sa 必须是长度 M 或 1 的 1-D 激活 scale");
  if (sbT.getRank() != 1 || (sbT.getShape()[0] != n && sbT.getShape()[0] != 1))
    return emitOpError("sb 必须是长度 N 或 1 的 1-D 权重 scale");
  if (saT.getElementType() != cT.getElementType() ||
      sbT.getElementType() != cT.getElementType())
    return emitOpError("sa/sb 元素类型须与输出一致(f16/f32/bf16)");
  return success();
}

} // namespace mininpu::graph
