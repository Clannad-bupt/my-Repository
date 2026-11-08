# Copyright (c) 2026 ThinkNative. All rights reserved.

# Scalar entry points: host emulation loads this image through tnrt.
# Links the ops archive (op lowering) and the ref kernel archive (the
# backend that executes on host; chip only encodes instructions)
# Sources globbed (CONFIGURE_DEPENDS re-globs every build): a new
# entry-point file needs no CMake edit
file(GLOB tn_scalar_sources CONFIGURE_DEPENDS "*.cpp")
add_library(tn_scalar SHARED ${tn_scalar_sources})
target_include_directories(tn_scalar PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tn_scalar PRIVATE tn_ops tn_kernel_ref)
set_target_properties(tn_scalar PROPERTIES
  PREFIX "lib"
  SUFFIX ".so"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
