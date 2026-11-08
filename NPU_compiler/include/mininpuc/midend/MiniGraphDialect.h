// minigraph dialect 声明(README §5.2)。生成文件在 build/lib/midend/。
#ifndef MININPUC_MIDEND_MINIGRAPHDIALECT_H
#define MININPUC_MIDEND_MINIGRAPHDIALECT_H

#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/Bytecode/BytecodeOpInterface.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

// 生成:MiniGraphDialect 类
#include "MiniGraphDialect.h.inc"

#define GET_OP_CLASSES
#include "MiniGraphOps.h.inc"

#endif // MININPUC_MIDEND_MINIGRAPHDIALECT_H
