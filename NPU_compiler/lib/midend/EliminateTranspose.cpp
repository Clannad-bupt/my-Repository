// eliminate_transpose(README §8 pass 7,§5.2.4):
//   transpose(transpose(x)) → x(或合并为单个 transpose)
//   matmul(transpose2D(A), B) → matmul A, B {transpose_a = !transpose_a},B 同理
#include "mininpuc/midend/MiniGraphDialect.h"
#include "mininpuc/midend/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/Pass/Pass.h"

using namespace mlir;
using namespace mininpu::graph;

namespace {

// I64ArrayAttr 的 ODS getter 返回 ArrayAttr;取出为 int64 数组
static SmallVector<int64_t> i64Array(ArrayAttr a) {
  SmallVector<int64_t> v;
  for (Attribute e : a) v.push_back(cast<IntegerAttr>(e).getInt());
  return v;
}

struct EliminateTransposePass
    : PassWrapper<EliminateTransposePass, OperationPass<func::FuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(EliminateTransposePass)

  StringRef getArgument() const override { return "minigraph-eliminate-transpose"; }
  StringRef getDescription() const override {
    return "消除/折叠 transpose(§5.2.4)";
  }

  void runOnOperation() override {
    bool changed = false;
    bool localFixedPoint = false;
    // transpose 折叠会产生新的相邻 transpose 对,跑到不动点(图很小,直接迭代)
    while (!localFixedPoint) {
      localFixedPoint = true;
      SmallVector<TransposeOp> transposes;
      getOperation().walk([&](TransposeOp op) { transposes.push_back(op); });
      for (TransposeOp outer : transposes) {
        auto inner = outer.getX().getDefiningOp<TransposeOp>();
        if (!inner || !inner.getResult().hasOneUse())
          continue;
        // 合并 permutation:outer(inner(x)) 的 perm = inner.perm ∘ outer.perm
        auto pi = i64Array(inner.getPermutation());
        auto po = i64Array(outer.getPermutation());
        SmallVector<int64_t> comp;
        for (int64_t p : po) comp.push_back(pi[p]);
        bool identity = true;
        for (size_t i = 0; i < comp.size(); ++i)
          if (comp[i] != static_cast<int64_t>(i)) identity = false;
        if (identity) {
          outer.getResult().replaceAllUsesWith(inner.getX());
        } else {
          OpBuilder b(outer);
          auto nt = b.create<TransposeOp>(outer.getLoc(), outer.getResult().getType(),
                                          inner.getX(), b.getI64ArrayAttr(comp));
          outer.getResult().replaceAllUsesWith(nt.getResult());
        }
        outer->erase();
        inner->erase();
        changed = true;
        localFixedPoint = false;
        break; // IR 已变,重新 walk
      }
    }

    // 折进 matmul.transpose_*(只处理 2D swap [1,0])
    SmallVector<MatmulOp> matmuls;
    getOperation().walk([&](MatmulOp op) { matmuls.push_back(op); });
    for (MatmulOp mm : matmuls) {
      auto strip = [&](Value v, BoolAttr flag,
                       TransposeOp* consumed) -> std::pair<Value, bool> {
        auto t = v.getDefiningOp<TransposeOp>();
        if (!t || !t.getResult().hasOneUse())
          return {v, flag.getValue()};
        auto perm = i64Array(t.getPermutation());
        if (perm.size() != 2 || perm[0] != 1 || perm[1] != 0)
          return {v, flag.getValue()};
        *consumed = t;
        return {t.getX(), !flag.getValue()};
      };
      TransposeOp tA = nullptr, tB = nullptr;
      auto [newA, flagA] = strip(mm.getA(), mm.getTransposeAAttr(), &tA);
      auto [newB, flagB] = strip(mm.getB(), mm.getTransposeBAttr(), &tB);
      if (!tA && !tB)
        continue;
      OpBuilder b(mm);
      auto newMm = b.create<MatmulOp>(
          mm.getLoc(), mm.getResult().getType(), newA, newB, mm.getBias(),
          b.getBoolAttr(flagA), b.getBoolAttr(flagB),
          mm.getEpilogue().has_value() ? mm.getEpilogueAttr()
                                       : StringAttr{});
      mm.getResult().replaceAllUsesWith(newMm.getResult());
      mm->erase();
      if (tA) tA->erase();
      if (tB) tB->erase();
      changed = true;
    }

    if (!changed)
      markAllAnalysesPreserved();
  }
};

} // namespace

std::unique_ptr<Pass> mininpu::createEliminateTransposePass() {
  return std::make_unique<EliminateTransposePass>();
}
