# lib/midend — minigraph/mininpu dialect + 中端 pass(README §5、§8)

# tablegen:minigraph
set(LLVM_TARGET_DEFINITIONS td/MiniGraphOps.td)
mlir_tablegen(MiniGraphDialect.h.inc -gen-dialect-decls -dialect minigraph)
mlir_tablegen(MiniGraphDialect.cpp.inc -gen-dialect-defs -dialect minigraph)
mlir_tablegen(MiniGraphOps.h.inc -gen-op-decls)
mlir_tablegen(MiniGraphOps.cpp.inc -gen-op-defs)
add_public_tablegen_target(MiniGraphIncGen)

# tablegen:mininpu
set(LLVM_TARGET_DEFINITIONS td/MiniNPUOps.td)
mlir_tablegen(MiniNPUDialect.h.inc -gen-dialect-decls -dialect mininpu)
mlir_tablegen(MiniNPUDialect.cpp.inc -gen-dialect-defs -dialect mininpu)
mlir_tablegen(MiniNPUTypes.h.inc -gen-typedef-decls -dialect mininpu)
mlir_tablegen(MiniNPUTypes.cpp.inc -gen-typedef-defs -dialect mininpu)
mlir_tablegen(MiniNPUOps.h.inc -gen-op-decls)
mlir_tablegen(MiniNPUOps.cpp.inc -gen-op-defs)
add_public_tablegen_target(MiniNPUIncGen)

add_library(mininpu_midend STATIC
  MiniGraphDialect.cpp
  MiniNPUDialect.cpp
  FuseMatmulBiasAct.cpp
  EliminateTranspose.cpp
  RegisterPasses.cpp)
add_dependencies(mininpu_midend MiniGraphIncGen MiniNPUIncGen)
target_include_directories(mininpu_midend PUBLIC ${CMAKE_CURRENT_BINARY_DIR}
                                                 ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(mininpu_midend PUBLIC
  MLIRIR
  MLIRPass
  MLIRSupport
  MLIRFuncDialect
  MLIRParser
  MLIRTransforms)
