// mininpu dialect 实现(README §5.3)。
#include "mininpuc/midend/MiniNPUDialect.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h" // DialectAsmParser/Printer(类型解析钩子)
#include "llvm/ADT/TypeSwitch.h"

using namespace mlir;
using namespace mininpu::npu;

#include "MiniNPUDialect.cpp.inc"

#define GET_TYPEDEF_CLASSES
#include "MiniNPUTypes.cpp.inc"

void MiniNPUDialect::initialize() {
  addTypes<
#define GET_TYPEDEF_LIST
#include "MiniNPUTypes.cpp.inc"
      >();
  addOperations<
#define GET_OP_LIST
#include "MiniNPUOps.cpp.inc"
      >();
}

#define GET_OP_CLASSES
#include "MiniNPUOps.cpp.inc"
