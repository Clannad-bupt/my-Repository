# lib/backend — TilePlanner / SRAM 分配 / lowering / CodeGen(README §9,步骤 4-7)
# 本目录只在 MLIR 可用时构建(见 lib/CMakeLists.txt)。
add_library(mininpu_backend STATIC
  TilePlanner.cpp
  SramAllocator.cpp
  Lowering.cpp
  CodeGen.cpp)
target_include_directories(mininpu_backend PUBLIC
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_BINARY_DIR}/lib/midend)  # tablegen 生成的 .inc(经 mininpu_midend 头引用)
target_link_libraries(mininpu_backend PUBLIC mininpu_runtime mininpu_midend)
