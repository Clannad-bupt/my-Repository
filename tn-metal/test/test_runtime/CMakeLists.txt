# Copyright (c) 2026 ThinkNative. All rights reserved.

# runtime flow tests: exercise the host API (tnrt/tndnn) end to end in
# TN_PSEUDO_CODE mode; one executable per test source. The scalar image
# is dlopen'ed by the runtime at run time (not linked), so its absolute
# build path is injected and the build dependency declared explicitly
# Sources globbed (CONFIGURE_DEPENDS re-globs every build): a new test
# file needs no CMake edit
file(GLOB test_runtime_sources CONFIGURE_DEPENDS "*.cpp")
foreach(test_source ${test_runtime_sources})
  get_filename_component(test_name ${test_source} NAME_WE)
  add_executable(${test_name} ${test_source})
  target_link_libraries(${test_name} PRIVATE tnrt)
  target_compile_definitions(${test_name} PRIVATE
    TN_SCALAR_SO_PATH="$<TARGET_FILE:tn_scalar>")
  add_dependencies(${test_name} tn_scalar)
endforeach()
# test_op_split exercises the ops module directly (host split + per-core
# scalar run); the scalar path runs on the ref backend, so its archive
# joins the link (libtnrt's own copies are private to the shared
# library), and the host split fits come from tn_ops_host
target_link_libraries(test_op_split PRIVATE tn_ops tn_ops_host tn_kernel_ref)
