# Copyright (c) 2026 ThinkNative. All rights reserved.

# One STATIC target per backend; both backends share context.c
# (lifecycle/shadow/allocator) and tensor.c (op-facing tensor helpers —
# slot + URF allocation, a pure software convention); the chip spec is
# the selected spec_<chip>.h's count macros, pulled in via
# TN_CHIP_SPEC_HDR; the backend macro is injected per target via
# target_compile_definitions, exactly satisfying kernel_backend.h's
# "define exactly one TN_BACKEND_*"
function(tn_add_kernel_backend backend)
  string(TOUPPER "${backend}" backend_upper)
  add_library(tn_kernel_${backend} STATIC common/context.c
                                          common/tensor.c
                                          ${ARGN})
  target_include_directories(tn_kernel_${backend} PRIVATE ${PROJECT_SOURCE_DIR}/include
                                                          ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(tn_kernel_${backend} PRIVATE
    TN_BACKEND_${backend_upper}
    TN_CHIP_SPEC_HDR=\"kernel/common/spec_${TN_CHIP}.h\"
  )
  # PIC: keeps the archives linkable into shared objects
  # hidden: every consumer (libtnrt.so, libtn_scalar.so) links its own
  # private copy — no dynamic-symbol interposition between the images'
  # global kernel contexts (tnkr_ctx_default) in one emulation process
  set_target_properties(tn_kernel_${backend} PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${backend}/lib"
  )
endfunction()

# Per-backend sources are globbed so a new engine file needs no CMake
# edit (CONFIGURE_DEPENDS re-globs on every build). ref/ holds the whole
# golden model, regops.cpp (sys/tdr-class register rewrites) included;
# the chip backend encodes and issues instructions instead, so chip/
# stays pure C (a pure C target automatically picks the C linker)
file(GLOB tn_kernel_ref_sources  CONFIGURE_DEPENDS "ref/*.cpp")
file(GLOB tn_kernel_chip_sources CONFIGURE_DEPENDS "chip/*.c")
tn_add_kernel_backend(ref    ${tn_kernel_ref_sources})
tn_add_kernel_backend(chip   ${tn_kernel_chip_sources})

# ---- smoke tests (<backend>/test/) ----
# Cases poke ctx->urf/global directly via the internal header
# context_internal.h (the real host memory API is a separate future
# design); one smoke_<backend> target per backend, test families split
# per kernel.h section (see smoke.h)
option(TN_BUILD_TESTS "build backend smoke tests" ON)
if(TN_BUILD_TESTS)
  # Shared families: ref/test/*.c; chip-difference families beyond the
  # shared ones: ref/test/<chip>/*.c, globbed in only for the matching
  # -DTN_CHIP (family entry suite_<chip>_<topic>(), registered in
  # smoke.c's main under #ifdef TN_CHIP_<CHIP>)
  file(GLOB tn_smoke_ref_sources CONFIGURE_DEPENDS
       "ref/test/*.c" "ref/test/${TN_CHIP}/*.c")
  add_executable(smoke_ref ${tn_smoke_ref_sources})
  target_include_directories(smoke_ref PRIVATE ${PROJECT_SOURCE_DIR}/include
                                               ${PROJECT_SOURCE_DIR}/src)
  # TN_CHIP_<CHIP> macro: smoke.c registers chip-specific families with it
  # (see below)
  string(TOUPPER "${TN_CHIP}" tn_chip_upper)
  target_compile_definitions(smoke_ref PRIVATE
    TN_BACKEND_REF
    TN_CHIP_SPEC_HDR=\"kernel/common/spec_${TN_CHIP}.h\"
    TN_CHIP_${tn_chip_upper})
  target_link_libraries(smoke_ref PRIVATE tn_kernel_ref)
endif()
