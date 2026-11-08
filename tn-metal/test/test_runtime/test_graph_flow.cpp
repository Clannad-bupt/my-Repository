// Copyright (c) 2026 ThinkNative. All rights reserved.

#include <stdio.h>

#include <runtime/tndnn.h>
#include <runtime/tnrt.h>

// explicit check instead of assert: the flow must stay checked under
// NDEBUG (Release builds compile assert out)
#define CHECK_OK(call)                                                                             \
    do {                                                                                           \
        tn_status_t rc_ = (call);                                                                  \
        if (rc_ != TN_SUCCESS) {                                                                   \
            fprintf(stderr, "FAIL %s:%d: %s -> %s\n", __FILE__, __LINE__, #call,                   \
                    tnrt_get_error_string(rc_));                                                   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

#define CHECK_STATUS(call, expect)                                                                 \
    do {                                                                                           \
        tn_status_t rc_ = (call);                                                                  \
        if (rc_ != (expect)) {                                                                     \
            fprintf(stderr, "FAIL %s:%d: %s -> %s (want %s)\n", __FILE__, __LINE__, #call,         \
                    tnrt_get_error_string(rc_), tnrt_get_error_string(expect));                    \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

int main() {
    // init device and stream
    tnrt_stream_t stream;
    CHECK_OK(tnrt_dev_request(0));
    CHECK_OK(tnrt_stream_create(&stream));
    tnrt_kernel_t kernel;
    CHECK_OK(tnrt_kernel_load_file(TN_SCALAR_SO_PATH, &kernel));

    tndnn_handle_t handle;
    CHECK_OK(tndnn_create(&handle, kernel, stream, TNDNN_RUNMODE_GRAPH, 8));

    tn_tensor_t in, out, weight;
    CHECK_OK(tnrt_tensor_alloc_4d(&in, TN_DTYPE_BF16, 1024, 1, 32, 1));
    CHECK_OK(tnrt_tensor_alloc_4d(&out, TN_DTYPE_BF16, 1024, 1, 32, 1));
    uint32_t weight_dims[] = {1024}; // 1D {dims[0]} per the tndnn_rmsnorm contract
    CHECK_OK(tnrt_tensor_alloc(&weight, TN_DTYPE_BF16, weight_dims, 1));

    // record a two-node graph: rmsnorm followed by an in-place scale
    tnop_rmsnorm_param_t rms_param = {.eps = 1e-5f};
    CHECK_OK(tndnn_rmsnorm(handle, &out, &in, &weight, &rms_param));
    tnop_scale_param_t scale_param = {.scale = 2.0f, .bias = 0.0f};
    CHECK_OK(tndnn_scale(handle, &out, &out, &scale_param));

    // finalize: the handle turns read-only after a successful build
    CHECK_OK(tndnn_graph_build(handle));
    CHECK_STATUS(tndnn_rmsnorm(handle, &out, &in, &weight, &rms_param), TN_ERR_STATE);
    CHECK_STATUS(tndnn_graph_build(handle), TN_ERR_STATE);

    // repeated execution rewrites buffer contents, not addresses
    CHECK_OK(tndnn_graph_execute(handle));
    CHECK_OK(tndnn_graph_execute(handle));
    CHECK_OK(tnrt_stream_sync(stream));

    // free resources
    CHECK_OK(tnrt_tensor_free(in));
    CHECK_OK(tnrt_tensor_free(out));
    CHECK_OK(tnrt_tensor_free(weight));
    CHECK_OK(tndnn_destroy(handle));
    CHECK_OK(tnrt_kernel_unload(kernel, stream));
    CHECK_OK(tnrt_stream_destroy(stream));
    CHECK_OK(tnrt_dev_release());
    printf("PASS test_graph_flow\n");
    return 0;
}
