# Copyright (c) 2026 ThinkNative. All rights reserved.

# Ops module: per-op lowering on top of the kernel API (include/ops/ops.h);
# backend-independent STATIC archives consumed by the scalar image and
# the host runtime.
# One .cpp per op: globbed so a new op needs no CMake edit;
# CONFIGURE_DEPENDS re-globs on every build (Make/Ninja).
# The top-level glob is non-recursive, so src/ops/host/ (the host-only
# split fits — never shipped in the scalar/firmware image) stays out of
# tn_ops and forms its own archive below.
file(GLOB tn_ops_sources CONFIGURE_DEPENDS "*.cpp")
add_library(tn_ops STATIC ${tn_ops_sources})
target_include_directories(tn_ops PUBLIC ${PROJECT_SOURCE_DIR}/include)
set_target_properties(tn_ops PROPERTIES
  # PIC: the archive is linked into the shared scalar image
  POSITION_INDEPENDENT_CODE ON
  # hidden: every consumer (libtnrt.so, libtn_scalar.so, tests) links its
  # own private copy — no dynamic-symbol interposition between the two
  # images' g_op_ctx when both are loaded in one emulation process
  C_VISIBILITY_PRESET hidden
  CXX_VISIBILITY_PRESET hidden
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# host-only split fits + the generic tnop_host_split dispatch (src/ops/host/)
# — linked by the host runtime and the direct ops tests, never by the
# scalar/firmware image (which consumes the fitted split instead)
file(GLOB tn_ops_host_sources CONFIGURE_DEPENDS "host/*.cpp")
add_library(tn_ops_host STATIC ${tn_ops_host_sources})
target_include_directories(tn_ops_host PUBLIC ${PROJECT_SOURCE_DIR}/include)
# same posture as tn_ops: PIC for the shared host runtime, hidden symbols
set_target_properties(tn_ops_host PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  C_VISIBILITY_PRESET hidden
  CXX_VISIBILITY_PRESET hidden
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
