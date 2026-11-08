// mininpu dialect 声明(README §5.3)。生成文件在 build/lib/midend/。
#ifndef MININPUC_MIDEND_MININPUDIALECT_H
#define MININPUC_MIDEND_MININPUDIALECT_H

#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/Bytecode/BytecodeOpInterface.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

// 生成:MiniNPUDialect 类
#include "MiniNPUDialect.h.inc"

// 生成:TokenType
#define GET_TYPEDEF_CLASSES
#include "MiniNPUTypes.h.inc"

#define GET_OP_CLASSES
#include "MiniNPUOps.h.inc"

#endif // MININPUC_MIDEND_MININPUDIALECT_H
