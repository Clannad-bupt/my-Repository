# Copyright (c) 2026 ThinkNative. All rights reserved.

# Driver module: host transport below tnrt (include/driver/driver.h) —
# copies launch/load messages and issues them to the device (real path)
# or to the scalar image (TN_PSEUDO_CODE emulation). Backend-independent
# STATIC archive consumed by the shared runtime image.
# Sources globbed (CONFIGURE_DEPENDS re-globs every build): a new
# driver file needs no CMake edit
file(GLOB tn_driver_sources CONFIGURE_DEPENDS "*.cpp")
add_library(tn_driver STATIC ${tn_driver_sources})
target_include_directories(tn_driver PUBLIC ${PROJECT_SOURCE_DIR}/include)
set_target_properties(tn_driver PROPERTIES
  # PIC: the archive is linked into the shared runtime image
  POSITION_INDEPENDENT_CODE ON
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
