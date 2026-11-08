# tools/ — CLI 入口(README §3.4)
add_executable(minic minic.cpp)
add_executable(minirt minirt.cpp)
add_executable(backend_selftest backend_selftest.cpp)

target_link_libraries(minirt PRIVATE mininpu_rt mininpu_sim mininpu_runtime)
target_link_libraries(backend_selftest PRIVATE mininpu_backend)
if(MLIR_FOUND)
  target_compile_definitions(minic PRIVATE MININPUC_WITH_MLIR=1)
  target_link_libraries(minic PRIVATE mininpu_midend mininpu_backend mininpu_sim mininpu_runtime)
else()
  target_link_libraries(minic PRIVATE mininpu_sim mininpu_runtime)
endif()

# README §17 的演示路径是 ./build/minic,输出到构建根目录
set_target_properties(minic minirt backend_selftest PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
