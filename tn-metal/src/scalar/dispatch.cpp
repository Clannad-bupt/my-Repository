// Copyright (c) 2026 ThinkNative. All rights reserved.

#include "op_dispatch.h"

#include "kernel/tensor.h"
#include "lpu/lpu_fw_api.h"
#include "ops/ops.h"
#include "scalar/scalar.h"

#if TN_PSEUDO_CODE
#include <cstdio>
// host-emulation core identity: tnrt_kernel_launch spawns one thread per
// core and programs this slot through the hook below before the entry
// runs; tn_op_dispatch reads it as this core's core_idx. On device the
// identity comes from the fw (lpu_fw_core_id) instead
static thread_local uint32_t g_core_idx;

void tn_scalar_set_core_idx(uint32_t idx) { g_core_idx = idx; }
#endif

namespace {

// fixed operand slots, required mask and exact payload size per opcode
// (docs/tndnn-graph-format.md §7.2/§9); the payload size is the wire size,
// kept identical to the public param struct by the asserts below
struct OpTraits {
    uint32_t tensor_count;
    uint32_t required_mask;
    uint32_t param_size;
};

// slot order: out first, then inputs, optional slots trailing
// (docs/tndnn-graph-format.md §7.2)
constexpr OpTraits kOpTraits[TNOP_OPCODE_COUNT] = {
    /* MATMUL        */ {4, 0x7, sizeof(tnop_matmul_param_t)},
    /* MATMUL_DQ     */ {4, 0x7, sizeof(tnop_matmul_dq_param_t)},
    /* MATMUL_BQ     */ {5, 0xF, sizeof(tnop_matmul_bq_param_t)},
    /* MATMUL_BQ_GS  */ {5, 0xF, sizeof(tnop_matmul_bq_gs_param_t)},
    /* MATMUL_W4BF16 */ {5, 0xF, sizeof(tnop_matmul_w4bf16_param_t)},
    /* MATMUL_W8BF16 */ {5, 0xF, sizeof(tnop_matmul_w8bf16_param_t)},
    /* ELTWISE       */ {3, 0x7, sizeof(tnop_eltwise_param_t)},
    /* UNARY         */ {2, 0x3, sizeof(tnop_unary_param_t)},
    /* SCALE         */ {2, 0x3, sizeof(tnop_scale_param_t)},
    /* CLAMP         */ {2, 0x3, sizeof(tnop_clamp_param_t)},
    /* ACTIVATION    */ {2, 0x3, sizeof(tnop_activation_param_t)},
    /* GLU           */ {3, 0x7, sizeof(tnop_glu_param_t)},
    /* SOFTMAX       */ {2, 0x3, sizeof(tnop_softmax_param_t)},
    /* RMSNORM       */ {3, 0x3, sizeof(tnop_rmsnorm_param_t)},
    /* LAYERNORM     */ {4, 0x3, sizeof(tnop_layernorm_param_t)},
    /* ROPE          */ {4, 0x7, sizeof(tnop_rope_param_t)},
    /* FATTENTION    */ {5, 0xF, sizeof(tnop_fattention_param_t)},
    /* CONV2D        */ {4, 0x7, sizeof(tnop_conv2d_param_t)},
    /* PERMUTE       */ {2, 0x3, sizeof(tnop_permute_param_t)},
    /* CAST          */ {2, 0x3, 0},
    /* GATHER        */ {3, 0x7, 0},
    /* SCATTER       */ {3, 0x7, 0},
    /* REDUCE        */ {2, 0x3, sizeof(tnop_reduce_param_t)},
};

// wire payload sizes (docs/tndnn-graph-format.md §9)
TN_STATIC_ASSERT(sizeof(tnop_matmul_param_t) == 4, "matmul payload ABI");
TN_STATIC_ASSERT(sizeof(tnop_matmul_dq_param_t) == 12, "matmul_dq payload ABI");
TN_STATIC_ASSERT(sizeof(tnop_matmul_bq_param_t) == 4, "matmul_bq payload ABI");
TN_STATIC_ASSERT(sizeof(tnop_matmul_bq_gs_param_t) == 12, "matmul_bq_gs payload ABI");
TN_STATIC_ASSERT(sizeof(tnop_matmul_w4bf16_param_t) == 8, "matmul_w4bf16 payload ABI");
TN_STATIC_ASSERT(sizeof(tnop_matmul_w8bf16_param_t) == 8, "matmul_w8bf16 payload ABI");
TN_STATIC_ASSERT(sizeof(tnop_eltwise_param_t) == 8, "eltwise payload ABI");
TN_STATIC_ASSERT(sizeof(tnop_unary_param_t) == 4, "unary payload ABI");
TN_STATIC_ASSERT(sizeof(tnop_scale_param_t) == 8, "scale payload ABI");
TN_STATIC_ASSERT(sizeof(tnop_clamp_param_t) == 8, "clamp payload ABI");
TN_STATIC_ASSERT(sizeof(tnop_activation_param_t) == 4, "activation payload ABI");
TN_STATIC_ASSERT(sizeof(tnop_glu_param_t) == 4, "glu payload ABI");
TN_STATIC_ASSERT(sizeof(tnop_softmax_param_t) == 4, "softmax payload ABI");
TN_STATIC_ASSERT(sizeof(tnop_rmsnorm_param_t) == 4, "rmsnorm payload ABI");
TN_STATIC_ASSERT(sizeof(tnop_layernorm_param_t) == 4, "layernorm payload ABI");
TN_STATIC_ASSERT(sizeof(tnop_rope_param_t) == 36, "rope payload ABI");
TN_STATIC_ASSERT(sizeof(tnop_fattention_param_t) == 8, "fattention payload ABI");
TN_STATIC_ASSERT(sizeof(tnop_conv2d_param_t) == 36, "conv2d payload ABI");
TN_STATIC_ASSERT(sizeof(tnop_permute_param_t) == 6, "permute payload ABI");
TN_STATIC_ASSERT(sizeof(tnop_reduce_param_t) == 8, "reduce payload ABI");
TN_STATIC_ASSERT(sizeof(tnop_split_t) == 8, "split op-header ABI");

// operand slot i of the view, NULL when the optional slot is absent
const tn_tensor_t *slot(const tn_op_view_t *view, uint32_t i) {
    return (view->tensor_mask & (1u << i)) ? &view->tensors[i] : nullptr;
}

template <typename P> const P *param(const tn_op_view_t *view) {
    return reinterpret_cast<const P *>(view->param);
}

#define TN_EXEC_STUB(op)                                                                           \
    tn_status_t scalar_exec_##op(const tn_op_view_t *view) {                                       \
        /* TODO: validate operands and lower to the kernel sequence */                             \
        (void)view;                                                                                \
        return TN_SUCCESS;                                                                         \
    }

TN_EXEC_STUB(matmul)
TN_EXEC_STUB(matmul_dq)
TN_EXEC_STUB(matmul_bq)
TN_EXEC_STUB(matmul_bq_gs)
TN_EXEC_STUB(matmul_w4bf16)
TN_EXEC_STUB(matmul_w8bf16)
TN_EXEC_STUB(unary)
TN_EXEC_STUB(scale)
TN_EXEC_STUB(clamp)
TN_EXEC_STUB(activation)
TN_EXEC_STUB(glu)
TN_EXEC_STUB(softmax)
TN_EXEC_STUB(layernorm)
TN_EXEC_STUB(rope)
TN_EXEC_STUB(fattention)
TN_EXEC_STUB(conv2d)
TN_EXEC_STUB(permute)
TN_EXEC_STUB(cast)
TN_EXEC_STUB(gather)
TN_EXEC_STUB(scatter)
TN_EXEC_STUB(reduce)

// the dual-mode op entries share one signature (host path writes the
// split); the scalar path only reads it — const_cast stays at this
// boundary
tn_status_t scalar_exec_eltwise(const tn_op_view_t *view) {
    return tnop_eltwise(slot(view, 0), slot(view, 1), slot(view, 2),
                        param<tnop_eltwise_param_t>(view),
                        const_cast<tnop_split_t *>(&view->split));
}

tn_status_t scalar_exec_rmsnorm(const tn_op_view_t *view) {
    return tnop_rmsnorm(slot(view, 0), slot(view, 1), slot(view, 2),
                        param<tnop_rmsnorm_param_t>(view),
                        const_cast<tnop_split_t *>(&view->split));
}

tn_status_t dispatch_op(const tn_op_view_t *view) {
    switch (view->opcode) {
    case TNOP_OPCODE_MATMUL:
        return scalar_exec_matmul(view);
    case TNOP_OPCODE_MATMUL_DQ:
        return scalar_exec_matmul_dq(view);
    case TNOP_OPCODE_MATMUL_BQ:
        return scalar_exec_matmul_bq(view);
    case TNOP_OPCODE_MATMUL_BQ_GS:
        return scalar_exec_matmul_bq_gs(view);
    case TNOP_OPCODE_MATMUL_W4BF16:
        return scalar_exec_matmul_w4bf16(view);
    case TNOP_OPCODE_MATMUL_W8BF16:
        return scalar_exec_matmul_w8bf16(view);
    case TNOP_OPCODE_ELTWISE:
        return scalar_exec_eltwise(view);
    case TNOP_OPCODE_UNARY:
        return scalar_exec_unary(view);
    case TNOP_OPCODE_SCALE:
        return scalar_exec_scale(view);
    case TNOP_OPCODE_CLAMP:
        return scalar_exec_clamp(view);
    case TNOP_OPCODE_ACTIVATION:
        return scalar_exec_activation(view);
    case TNOP_OPCODE_GLU:
        return scalar_exec_glu(view);
    case TNOP_OPCODE_SOFTMAX:
        return scalar_exec_softmax(view);
    case TNOP_OPCODE_RMSNORM:
        return scalar_exec_rmsnorm(view);
    case TNOP_OPCODE_LAYERNORM:
        return scalar_exec_layernorm(view);
    case TNOP_OPCODE_ROPE:
        return scalar_exec_rope(view);
    case TNOP_OPCODE_FATTENTION:
        return scalar_exec_fattention(view);
    case TNOP_OPCODE_CONV2D:
        return scalar_exec_conv2d(view);
    case TNOP_OPCODE_PERMUTE:
        return scalar_exec_permute(view);
    case TNOP_OPCODE_CAST:
        return scalar_exec_cast(view);
    case TNOP_OPCODE_GATHER:
        return scalar_exec_gather(view);
    case TNOP_OPCODE_SCATTER:
        return scalar_exec_scatter(view);
    case TNOP_OPCODE_REDUCE:
        return scalar_exec_reduce(view);
    default:
        return TN_ERR_PARAM;
    }
}

} // namespace

tn_status_t tn_op_dispatch(const tn_op_view_t *view) {
    if (view == nullptr || view->opcode >= TNOP_OPCODE_COUNT) {
        return TN_ERR_PARAM;
    }
    const OpTraits &traits = kOpTraits[view->opcode];
    if (view->tensor_count != traits.tensor_count ||
        (view->tensor_mask & traits.required_mask) != traits.required_mask ||
        view->param_size != traits.param_size ||
        (traits.param_size != 0 && view->param == nullptr)) {
        return TN_ERR_PARAM;
    }
    // scalar mode for the ops module: consume the split from the launch
    // message, never recompute it; the core identity is per execution
    // context — host emulation: this thread's slot programmed by
    // tnrt_kernel_launch (one thread per core); device: the calling
    // core's fw slot (lpu_fw_core_id)
#if TN_PSEUDO_CODE
    tnop_ctx_init(view->num_cores, g_core_idx, true);
#else
    tnop_ctx_init(view->num_cores, lpu_fw_core_id(), true);
#endif
    const tn_status_t st = dispatch_op(view);
#if TN_PSEUDO_CODE
    printf("core %u:%u, opcode %u\n", view->num_cores, g_core_idx, view->opcode);
#endif
    // op-scoped TDR slots and URF regions are reclaimed wholesale here,
    // never by the op itself — error exits cannot leak either
    tnkr_tensor_release();
    return st;
}
