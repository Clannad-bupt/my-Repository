// Copyright (c) 2026 ThinkNative. All rights reserved.

#include <new>

#include "tndnn_ctx.h"

// ================================================
// handle lifecycle
// ================================================

tn_status_t tndnn_create(tndnn_handle_t *handle, const tnrt_kernel_t kernel,
                         const tnrt_stream_t stream, tndnn_runmode_t runmode, uint32_t num_cores) {
    if (handle == nullptr) {
        return TN_ERR_PARAM;
    }
    *handle = nullptr;
    if (kernel == nullptr || stream == nullptr || num_cores == 0 ||
        (runmode != TNDNN_RUNMODE_GRAPH && runmode != TNDNN_RUNMODE_OP)) {
        return TN_ERR_PARAM;
    }
    auto ctx = new (std::nothrow) tndnn_ctx(kernel, stream, runmode, num_cores);
    if (ctx == nullptr) {
        return TN_ERR_NOMEM;
    }
    *handle = ctx;
    return TN_SUCCESS;
}

tn_status_t tndnn_destroy(tndnn_handle_t handle) {
    if (handle == nullptr) {
        return TN_ERR_PARAM;
    }
    delete handle;
    return TN_SUCCESS;
}

// ================================================
// graph mode
// ================================================

tn_status_t tndnn_graph_build(tndnn_handle_t handle) {
    if (handle == nullptr) {
        return TN_ERR_PARAM;
    }
    return handle->BuildGraph();
}

tn_status_t tndnn_graph_execute(tndnn_handle_t handle) {
    if (handle == nullptr) {
        return TN_ERR_PARAM;
    }
    return handle->ExecuteGraph();
}

// ================================================
// matmul nodes
// ================================================

tn_status_t tndnn_matmul(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *left,
                         const tn_tensor_t *right, const tn_tensor_t *bias,
                         const tnop_matmul_param_t *param) {
    if (handle == nullptr || out == nullptr || left == nullptr || right == nullptr ||
        param == nullptr) {
        return TN_ERR_PARAM;
    }
    const tn_tensor_t *tensors[] = {out, left, right, bias};
    return handle->AddNode(TNOP_OPCODE_MATMUL, tensors, 4, param, sizeof(*param));
}

tn_status_t tndnn_matmul_dq(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *left,
                            const tn_tensor_t *right, const tn_tensor_t *bias,
                            const tnop_matmul_dq_param_t *param) {
    if (handle == nullptr || out == nullptr || left == nullptr || right == nullptr ||
        param == nullptr) {
        return TN_ERR_PARAM;
    }
    const tn_tensor_t *tensors[] = {out, left, right, bias};
    return handle->AddNode(TNOP_OPCODE_MATMUL_DQ, tensors, 4, param, sizeof(*param));
}

tn_status_t tndnn_matmul_bq(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *left,
                            const tn_tensor_t *right, const tn_tensor_t *right_scale,
                            const tn_tensor_t *bias, const tnop_matmul_bq_param_t *param) {
    if (handle == nullptr || out == nullptr || left == nullptr || right == nullptr ||
        right_scale == nullptr || param == nullptr) {
        return TN_ERR_PARAM;
    }
    const tn_tensor_t *tensors[] = {out, left, right, right_scale, bias};
    return handle->AddNode(TNOP_OPCODE_MATMUL_BQ, tensors, 5, param, sizeof(*param));
}

tn_status_t tndnn_matmul_bq_gs(tndnn_handle_t handle, const tn_tensor_t *out,
                               const tn_tensor_t *left, const tn_tensor_t *right,
                               const tn_tensor_t *right_scale, const tn_tensor_t *bias,
                               const tnop_matmul_bq_gs_param_t *param) {
    if (handle == nullptr || out == nullptr || left == nullptr || right == nullptr ||
        right_scale == nullptr || param == nullptr) {
        return TN_ERR_PARAM;
    }
    const tn_tensor_t *tensors[] = {out, left, right, right_scale, bias};
    return handle->AddNode(TNOP_OPCODE_MATMUL_BQ_GS, tensors, 5, param, sizeof(*param));
}

tn_status_t tndnn_matmul_w4bf16(tndnn_handle_t handle, const tn_tensor_t *out,
                                const tn_tensor_t *left, const tn_tensor_t *right,
                                const tn_tensor_t *right_scale_off, const tn_tensor_t *bias,
                                const tnop_matmul_w4bf16_param_t *param) {
    if (handle == nullptr || out == nullptr || left == nullptr || right == nullptr ||
        right_scale_off == nullptr || param == nullptr) {
        return TN_ERR_PARAM;
    }
    const tn_tensor_t *tensors[] = {out, left, right, right_scale_off, bias};
    return handle->AddNode(TNOP_OPCODE_MATMUL_W4BF16, tensors, 5, param, sizeof(*param));
}

tn_status_t tndnn_matmul_w8bf16(tndnn_handle_t handle, const tn_tensor_t *out,
                                const tn_tensor_t *left, const tn_tensor_t *right,
                                const tn_tensor_t *right_scale_off, const tn_tensor_t *bias,
                                const tnop_matmul_w8bf16_param_t *param) {
    if (handle == nullptr || out == nullptr || left == nullptr || right == nullptr ||
        right_scale_off == nullptr || param == nullptr) {
        return TN_ERR_PARAM;
    }
    const tn_tensor_t *tensors[] = {out, left, right, right_scale_off, bias};
    return handle->AddNode(TNOP_OPCODE_MATMUL_W8BF16, tensors, 5, param, sizeof(*param));
}

// ================================================
// element-wise nodes
// ================================================

tn_status_t tndnn_eltwise(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *a,
                          const tn_tensor_t *b, const tnop_eltwise_param_t *param) {
    if (handle == nullptr || out == nullptr || a == nullptr || b == nullptr || param == nullptr) {
        return TN_ERR_PARAM;
    }
    const tn_tensor_t *tensors[] = {out, a, b};
    return handle->AddNode(TNOP_OPCODE_ELTWISE, tensors, 3, param, sizeof(*param));
}

tn_status_t tndnn_unary(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *in,
                        const tnop_unary_param_t *param) {
    if (handle == nullptr || out == nullptr || in == nullptr || param == nullptr) {
        return TN_ERR_PARAM;
    }
    const tn_tensor_t *tensors[] = {out, in};
    return handle->AddNode(TNOP_OPCODE_UNARY, tensors, 2, param, sizeof(*param));
}

tn_status_t tndnn_scale(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *in,
                        const tnop_scale_param_t *param) {
    if (handle == nullptr || out == nullptr || in == nullptr || param == nullptr) {
        return TN_ERR_PARAM;
    }
    const tn_tensor_t *tensors[] = {out, in};
    return handle->AddNode(TNOP_OPCODE_SCALE, tensors, 2, param, sizeof(*param));
}

tn_status_t tndnn_clamp(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *in,
                        const tnop_clamp_param_t *param) {
    if (handle == nullptr || out == nullptr || in == nullptr || param == nullptr) {
        return TN_ERR_PARAM;
    }
    const tn_tensor_t *tensors[] = {out, in};
    return handle->AddNode(TNOP_OPCODE_CLAMP, tensors, 2, param, sizeof(*param));
}

// ================================================
// fused NN nodes
// ================================================

tn_status_t tndnn_activation(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *in,
                             const tnop_activation_param_t *param) {
    if (handle == nullptr || out == nullptr || in == nullptr || param == nullptr) {
        return TN_ERR_PARAM;
    }
    const tn_tensor_t *tensors[] = {out, in};
    return handle->AddNode(TNOP_OPCODE_ACTIVATION, tensors, 2, param, sizeof(*param));
}

tn_status_t tndnn_glu(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *gate,
                      const tn_tensor_t *value, const tnop_glu_param_t *param) {
    if (handle == nullptr || out == nullptr || gate == nullptr || value == nullptr ||
        param == nullptr) {
        return TN_ERR_PARAM;
    }
    const tn_tensor_t *tensors[] = {out, gate, value};
    return handle->AddNode(TNOP_OPCODE_GLU, tensors, 3, param, sizeof(*param));
}

tn_status_t tndnn_softmax(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *in,
                          const tnop_softmax_param_t *param) {
    if (handle == nullptr || out == nullptr || in == nullptr || param == nullptr) {
        return TN_ERR_PARAM;
    }
    const tn_tensor_t *tensors[] = {out, in};
    return handle->AddNode(TNOP_OPCODE_SOFTMAX, tensors, 2, param, sizeof(*param));
}

tn_status_t tndnn_rmsnorm(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *in,
                          const tn_tensor_t *weight, const tnop_rmsnorm_param_t *param) {
    if (handle == nullptr || out == nullptr || in == nullptr || param == nullptr) {
        return TN_ERR_PARAM;
    }
    const tn_tensor_t *tensors[] = {out, in, weight};
    return handle->AddNode(TNOP_OPCODE_RMSNORM, tensors, 3, param, sizeof(*param));
}

tn_status_t tndnn_layernorm(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *in,
                            const tn_tensor_t *weight, const tn_tensor_t *bias,
                            const tnop_layernorm_param_t *param) {
    if (handle == nullptr || out == nullptr || in == nullptr || param == nullptr) {
        return TN_ERR_PARAM;
    }
    const tn_tensor_t *tensors[] = {out, in, weight, bias};
    return handle->AddNode(TNOP_OPCODE_LAYERNORM, tensors, 4, param, sizeof(*param));
}

tn_status_t tndnn_rope(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *in,
                       const tn_tensor_t *pos, const tn_tensor_t *freq_factors,
                       const tnop_rope_param_t *param) {
    if (handle == nullptr || out == nullptr || in == nullptr || pos == nullptr ||
        param == nullptr) {
        return TN_ERR_PARAM;
    }
    const tn_tensor_t *tensors[] = {out, in, pos, freq_factors};
    return handle->AddNode(TNOP_OPCODE_ROPE, tensors, 4, param, sizeof(*param));
}

tn_status_t tndnn_fattention(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *q,
                             const tn_tensor_t *k, const tn_tensor_t *v, const tn_tensor_t *mask,
                             const tnop_fattention_param_t *param) {
    if (handle == nullptr || out == nullptr || q == nullptr || k == nullptr || v == nullptr ||
        param == nullptr) {
        return TN_ERR_PARAM;
    }
    const tn_tensor_t *tensors[] = {out, q, k, v, mask};
    return handle->AddNode(TNOP_OPCODE_FATTENTION, tensors, 5, param, sizeof(*param));
}

// ================================================
// conv nodes
// ================================================

tn_status_t tndnn_conv2d(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *in,
                         const tn_tensor_t *weight, const tn_tensor_t *bias,
                         const tnop_conv2d_param_t *param) {
    if (handle == nullptr || out == nullptr || in == nullptr || weight == nullptr ||
        param == nullptr) {
        return TN_ERR_PARAM;
    }
    const tn_tensor_t *tensors[] = {out, in, weight, bias};
    return handle->AddNode(TNOP_OPCODE_CONV2D, tensors, 4, param, sizeof(*param));
}

// ================================================
// data movement nodes
// ================================================

tn_status_t tndnn_permute(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *in,
                          const tnop_permute_param_t *param) {
    if (handle == nullptr || out == nullptr || in == nullptr || param == nullptr) {
        return TN_ERR_PARAM;
    }
    const tn_tensor_t *tensors[] = {out, in};
    return handle->AddNode(TNOP_OPCODE_PERMUTE, tensors, 2, param, sizeof(*param));
}

tn_status_t tndnn_cast(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *in) {
    if (handle == nullptr || out == nullptr || in == nullptr) {
        return TN_ERR_PARAM;
    }
    const tn_tensor_t *tensors[] = {out, in};
    return handle->AddNode(TNOP_OPCODE_CAST, tensors, 2, nullptr, 0);
}

tn_status_t tndnn_gather(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *in,
                         const tn_tensor_t *index) {
    if (handle == nullptr || out == nullptr || in == nullptr || index == nullptr) {
        return TN_ERR_PARAM;
    }
    const tn_tensor_t *tensors[] = {out, in, index};
    return handle->AddNode(TNOP_OPCODE_GATHER, tensors, 3, nullptr, 0);
}

tn_status_t tndnn_scatter(tndnn_handle_t handle, const tn_tensor_t *dst, const tn_tensor_t *src,
                          const tn_tensor_t *index) {
    if (handle == nullptr || dst == nullptr || src == nullptr || index == nullptr) {
        return TN_ERR_PARAM;
    }
    const tn_tensor_t *tensors[] = {dst, src, index};
    return handle->AddNode(TNOP_OPCODE_SCATTER, tensors, 3, nullptr, 0);
}

// ================================================
// reduce nodes
// ================================================

tn_status_t tndnn_reduce(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *in,
                         const tnop_reduce_param_t *param) {
    if (handle == nullptr || out == nullptr || in == nullptr || param == nullptr) {
        return TN_ERR_PARAM;
    }
    const tn_tensor_t *tensors[] = {out, in};
    return handle->AddNode(TNOP_OPCODE_REDUCE, tensors, 2, param, sizeof(*param));
}
