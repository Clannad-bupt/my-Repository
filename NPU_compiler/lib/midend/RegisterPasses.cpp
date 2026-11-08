// pass 注册(minic 启动时调用)
#include "mininpuc/midend/Passes.h"

#include "mlir/Pass/PassRegistry.h"

void mininpu::registerMininpuPasses() {
  mlir::registerPass(createFuseMatmulBiasActPass);
  mlir::registerPass(createEliminateTransposePass);
}
