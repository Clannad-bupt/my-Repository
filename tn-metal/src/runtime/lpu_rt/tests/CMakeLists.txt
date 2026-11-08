# runtime tests: API smoke + vecadd E2E (sim backend) + rt_prelink (loader self-check)
add_executable(test_api test_api.c)
target_link_libraries(test_api PRIVATE lpu_rt)
add_test(NAME api COMMAND test_api)

add_executable(test_vecadd test_vecadd.c)
target_link_libraries(test_vecadd PRIVATE lpu_rt)
add_test(NAME vecadd COMMAND test_vecadd)

# rt_prelink: loader-logic host ctest + QEMU-smoke prelinked-image producer (one binary).
# Depends on real artifacts (run "make -C src" first); missing file = FAIL with guidance,
# never silently skipped.
add_executable(rt_prelink ../tools/rt_prelink.c)
target_link_libraries(rt_prelink PRIVATE lpu_rt)
target_include_directories(rt_prelink PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../../include)
target_compile_definitions(rt_prelink PRIVATE
    RT_PRELINK_FW_ELF="${CMAKE_CURRENT_SOURCE_DIR}/../../../scalar/build-selftest/scalar_fw_selftest.elf"
    RT_PRELINK_DSO="${CMAKE_CURRENT_SOURCE_DIR}/../../../scalar/sample_op/sample_op.so"
    RT_PRELINK_OUT="${CMAKE_CURRENT_BINARY_DIR}/sample_op.prelink.bin")
add_test(NAME prelink COMMAND rt_prelink)

# vecadd E2E (device backend + dual-QEMU rig): runs inside the VM (/dev/lpu0 +
# vfio-user + dual card-side fw instances); on a host with no device,
# exit(77) = ctest skip. The fw ELF must be the card build (exported symbol
# addresses = actual scalar instance locations). Paths can be overridden via
# the LPU_TEST_FW_ELF / LPU_TEST_DSO env vars (runs from any cwd inside the VM).
add_executable(test_vecadd_dev test_vecadd_dev.c)
target_link_libraries(test_vecadd_dev PRIVATE lpu_rt)
target_include_directories(test_vecadd_dev PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../include)
target_compile_definitions(test_vecadd_dev PRIVATE
    TEST_VECADD_FW_ELF="${CMAKE_CURRENT_SOURCE_DIR}/../../../scalar/build-card/scalar_fw.elf"
    TEST_VECADD_DSO="${CMAKE_CURRENT_SOURCE_DIR}/../../../scalar/sample_op/sample_op.so")
add_test(NAME vecadd_dev COMMAND test_vecadd_dev)
set_tests_properties(vecadd_dev PROPERTIES SKIP_RETURN_CODE 77)
