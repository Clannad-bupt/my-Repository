# Copyright (c) 2026 ThinkNative. All rights reserved.

# host runtime: one SHARED lib (no backend split), public C API in
# include/runtime/tnrt.h and include/runtime/tndnn.h; artifact pinned
# to lib*.so on every platform (macOS would otherwise default to .dylib)
# Sources globbed (CONFIGURE_DEPENDS re-globs every build): a new
# runtime file needs no CMake edit
file(GLOB tnrt_sources CONFIGURE_DEPENDS "*.cpp")
add_library(tnrt SHARED ${tnrt_sources})
target_include_directories(tnrt PUBLIC ${PROJECT_SOURCE_DIR}/include)
# tn_ops: operator-mode AddNode computes the launch split through the
# ops module's host path (ops/ops.h); tn_ops_host: the split fits
# themselves (src/ops/host/) — host-only, never linked into the scalar
# image; tn_kernel_ref: the host-side golden model — spec capabilities
# (tn_spec_info) for the split logic and ref execution of the kernel
# sequence
target_link_libraries(tnrt PRIVATE ${CMAKE_DL_LIBS} tn_ops tn_ops_host tn_kernel_ref)
if(TN_PSEUDO_CODE)
  # per-core launch threads in the kernel-launch emulation
  target_link_libraries(tnrt PRIVATE Threads::Threads)
endif()
set_target_properties(tnrt PROPERTIES
  PREFIX "lib"
  SUFFIX ".so"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# LPU verify runtime (ported from the QEMU verification tree; own CMake
# tree with a ctest suite under tests/, gated by TN_BUILD_TESTS);
# device-side module, skipped in pseudo-code mode
if(NOT TN_PSEUDO_CODE)
  add_subdirectory(lpu_rt)
endif()
