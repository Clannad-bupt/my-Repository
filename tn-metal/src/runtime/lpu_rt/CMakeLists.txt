# liblpu_rt — LPU user-space runtime (minimal CUDA-style API, sim/device dual backends)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_library(lpu_rt SHARED
    src/rt_init.c
    src/rt_error.c
    src/rt_mem.c
    src/rt_memcpy.c
    src/rt_module.c
    src/rt_launch.c
    src/rt_stream.c
    src/rt_ring.c
    src/rt_cq.c
    src/rt_event.c
    src/backend_sim.c
    src/backend_dev.c
)
set_target_properties(lpu_rt PROPERTIES
    VERSION   0.1.0
    SOVERSION 0
    C_VISIBILITY_PRESET hidden          # export only symbols annotated LPU_RT_API
)
target_include_directories(lpu_rt
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/../../../include   # shared ABI headers
)
target_link_libraries(lpu_rt PRIVATE Threads::Threads)

# Tests (default sim backend; switchable with LPU_RT_BACKEND=device)
option(TN_BUILD_TESTS "build tests" ON)
if(TN_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Install layout + deb (cpack -G DEB)
install(TARGETS lpu_rt LIBRARY DESTINATION lib)
install(FILES include/lpu_rt.h include/lpu_rt_sim.h DESTINATION include)

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "liblpu-rt")
set(CPACK_PACKAGE_VERSION "0.1")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "ThinkNative LPU accelerator user-space runtime (sim/device backends)")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "ThinkNative")
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
include(CPack)
