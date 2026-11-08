# lib/ — 编译器与运行时实现(README §12.2)

add_library(mininpu_runtime STATIC
  runtime/Json.cpp
  runtime/HardwareSpec.cpp
  runtime/Float16.cpp
  runtime/minisa.cpp
  runtime/npy.cpp)
target_include_directories(mininpu_runtime PUBLIC ${CMAKE_SOURCE_DIR}/include)

add_library(mininpu_sim STATIC sim/Sim.cpp)
target_link_libraries(mininpu_sim PUBLIC mininpu_runtime)

# Runtime/Stream(§10.1):在 sim 之上(Executor),单独成库避免静态库循环依赖
add_library(mininpu_rt STATIC runtime/Runtime.cpp)
target_link_libraries(mininpu_rt PUBLIC mininpu_sim)

# MLIR 相关(midend/backend)在 MLIR 可用时构建
if(MLIR_FOUND)
  add_subdirectory(midend)
  add_subdirectory(backend)
endif()
