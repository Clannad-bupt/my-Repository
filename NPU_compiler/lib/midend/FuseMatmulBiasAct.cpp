// fuse_matmul_bias_act(README §8.1):
//   silu(matmul)                → matmul {epilogue = "silu"}
//   add(matmul, bias)           → matmul {epilogue = "bias",  bias}
//   silu(add(matmul, bias))     → matmul {epilogue = "bias_silu", bias}
// 合法性:silu 是 matmul 结果唯一使用者;bias 为 1-D 长度 N(§8.1)。
#include "mininpuc/midend/MiniGraphDialect.h"
#include "mininpuc/midend/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/Pass/Pass.h"

using namespace mlir;
using namespace mininpu::graph;

namespace {

// bias 合法性:1-D 且长度等于 N(§8.1「Bias 广播维与 N 对齐」)
static bool isLegalBias(Value bias, int64_t n) {
  auto t = dyn_cast<RankedTensorType>(bias.getType());
  return t && t.getRank() == 1 && t.getShape()[0] == n;
}

struct FuseMatmulBiasActPass
    : PassWrapper<FuseMatmulBiasActPass, OperationPass<func::FuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FuseMatmulBiasActPass)

  StringRef getArgument() const override {
    return "minigraph-fuse-matmul-bias-act";
  }
  StringRef getDescription() const override {
    return "融合 matmul(+bias)(+silu) 为带 epilogue 的 minigraph.matmul(§8.1)";
  }

  // 用带 epilogue/bias 的新 matmul 替换旧链,并记下被擦除的 op
  MatmulOp rebuildWithEpilogue(MatmulOp mm, Value bias, StringRef epilogue,
                               SmallPtrSetImpl<Operation*>& erased) {
    OpBuilder b(mm);
    auto newMm = b.create<MatmulOp>(mm.getLoc(), mm.getResult().getType(),
                                    mm.getA(), mm.getB(), bias,
                                    mm.getTransposeAAttr(), mm.getTransposeBAttr(),
                                    b.getStringAttr(epilogue));
    mm.getResult().replaceAllUsesWith(newMm.getResult());
    erased.insert(mm);
    mm->erase();
    return newMm;
  }

  void runOnOperation() override {
    SmallPtrSet<Operation*, 8> erased;
    bool changed = false;

    // 先收集,再改写(README §13 步骤 3:禁止边遍历边删)
    SmallVector<SiluOp> silus;
    getOperation().walk([&](SiluOp op) { silus.push_back(op); });

    for (SiluOp silu : silus) {
      Value in = silu.getX();
      // 情形 A:silu(add(matmul, bias)) —— bias 链
      if (auto add = in.getDefiningOp<AddOp>()) {
        auto mm = add.getLhs().getDefiningOp<MatmulOp>();
        if (!mm || !add->getResult(0).hasOneUse() ||
            !mm.getResult().hasOneUse() || mm.getEpilogue().has_value())
          continue;
        auto outT = cast<RankedTensorType>(mm.getResult().getType());
        if (!isLegalBias(add.getRhs(), outT.getShape()[1]))
          continue;
        MatmulOp newMm =
            rebuildWithEpilogue(mm, add.getRhs(), "bias_silu", erased);
        silu.getResult().replaceAllUsesWith(newMm.getResult());
        erased.insert(silu);
        silu->erase();
        erased.insert(add);
        add->erase();
        changed = true;
        continue;
      }
      // 情形 B:silu(matmul)
      auto mm = in.getDefiningOp<MatmulOp>();
      if (!mm || !mm.getResult().hasOneUse() || mm.getEpilogue().has_value())
        continue;
      MatmulOp newMm = rebuildWithEpilogue(mm, nullptr, "silu", erased);
      silu.getResult().replaceAllUsesWith(newMm.getResult());
      erased.insert(silu);
      silu->erase();
      changed = true;
    }

    // 情形 C:add(matmul, bias)(无 silu)。注意 add 可能已在情形 A 中被擦除。
    SmallVector<AddOp> adds;
    getOperation().walk([&](AddOp op) {
      if (!erased.contains(op.getOperation())) adds.push_back(op);
    });
    for (AddOp add : adds) {
      auto mm = add.getLhs().getDefiningOp<MatmulOp>();
      if (!mm || !mm.getResult().hasOneUse() || mm.getEpilogue().has_value())
        continue;
      auto outT = cast<RankedTensorType>(mm.getResult().getType());
      if (!isLegalBias(add.getRhs(), outT.getShape()[1]))
        continue;
      MatmulOp newMm = rebuildWithEpilogue(mm, add.getRhs(), "bias", erased);
      add.getResult().replaceAllUsesWith(newMm.getResult());
      erased.insert(add);
      add->erase();
      changed = true;
    }

    if (!changed)
      markAllAnalysesPreserved();
  }
};

} // namespace

std::unique_ptr<Pass> mininpu::createFuseMatmulBiasActPass() {
  return std::make_unique<FuseMatmulBiasActPass>();
}
