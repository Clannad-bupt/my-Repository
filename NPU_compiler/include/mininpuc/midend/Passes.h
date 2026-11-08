// 中端 pass 声明(README §8)。
#ifndef MININPUC_MIDEND_PASSES_H
#define MININPUC_MIDEND_PASSES_H

#include "mlir/Pass/Pass.h"

#include <memory>

namespace mininpu {

/// silu(add(matmul, bias)?) → matmul{epilogue}(README §8.1)
std::unique_ptr<mlir::Pass> createFuseMatmulBiasActPass();
/// transpose(transpose(x))→x;matmul(transpose(A),B)→transpose_a(§8,§5.2.4)
std::unique_ptr<mlir::Pass> createEliminateTransposePass();

/// 注册全部 mininpu pass(minic 启动时调用)
void registerMininpuPasses();

} // namespace mininpu

#endif // MININPUC_MIDEND_PASSES_H
