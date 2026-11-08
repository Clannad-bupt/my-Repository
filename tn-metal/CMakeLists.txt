# Copyright (c) 2026 ThinkNative. All rights reserved.

# tn-metal build script
# Usage: cmake -B build [-DTN_CHIP=tna1] [-DTN_PSEUDO_CODE=ON] && cmake --build build
#   Artifacts: build/<backend>/lib/libtn_kernel_<backend>.a (backend = ref/chip)
#              build/lib/libtn_ops.a (op lowering, kernel-backed)
#              build/lib/libtn_driver.a (device transport under tnrt;
#                not built when TN_PSEUDO_CODE=ON)
#              build/lib/libtnrt.so (host runtime)
#              build/lib/libtn_scalar.so (scalar entry-point stubs)
#   One backend only: cmake --build build --target tn_kernel_ref
#   One build tree is pinned to one chip; side-by-side chips:
#     cmake -B build-<chip> -DTN_CHIP=<chip>
cmake_minimum_required(VERSION 3.16)
project(tn-metal VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- chip spec selection (compile time) ----
# Adding a chip = 1) define the count macros in src/kernel/common/spec_<name>.h
#                2) append <name> to the TN_CHIPS list
#               see skill: tn-add-chip
set(TN_CHIPS tna1)
set(TN_CHIP "tna1" CACHE STRING "chip spec: ${TN_CHIPS}")
set_property(CACHE TN_CHIP PROPERTY STRINGS ${TN_CHIPS})
if(NOT TN_CHIP IN_LIST TN_CHIPS)
  message(FATAL_ERROR "TN_CHIP must be one of: ${TN_CHIPS}")
endif()

add_compile_options(-Wall -Wextra)

# ---- pseudo-code mode ----
# OFF (default): real device path — driver and lpu_rt are built, no
#     TN_PSEUDO_CODE define (runtime device paths are still TODO stubs).
# ON: host-only emulation — TN_PSEUDO_CODE is defined for every module and
#     the device-side pieces (driver transport, lpu_rt verify runtime) are
#     not built; tnrt dlopens the scalar image instead.
option(TN_PSEUDO_CODE "host pseudo-code emulation; skips driver and lpu_rt" OFF)
if(TN_PSEUDO_CODE)
  add_compile_definitions(TN_PSEUDO_CODE=1)
  # std::thread in tnrt's multi-core launch emulation (tnrt_kernel_launch)
  find_package(Threads REQUIRED)
endif()

# ---- tests (ctest) ----
# lpu_rt ctest suite hooks in through enable_testing() when lpu_rt is built
# (TN_BUILD_TESTS option)
enable_testing()

# ---- modules ----
add_subdirectory(src/kernel)
add_subdirectory(src/ops)
add_subdirectory(src/scalar)
if(NOT TN_PSEUDO_CODE)
  add_subdirectory(src/driver)
endif()
add_subdirectory(src/runtime)

# ---- tests ----
add_subdirectory(test)
