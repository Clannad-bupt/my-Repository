// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/ops/eltwise.cpp
 *
 * Element-wise binary op (add/sub/mul/div/max/min) lowering.
 *
 * Launch contract (the caller's — the op body validates nothing):
 * any rank the tensor type admits (bounded by TN_MAX_DIMS), extents
 * nonzero, and all three operands share one element count — a
 * same-shape launch (broadcast is deliberately out of this op's
 * scope). Per-dim extents never enter a descriptor — the launch is
 * re-chunked linearly — so no uint16 bound applies to them. Dtype
 * rules (promotion on add/sub/mul, div FP-only, max/min same dtype)
 * are the vec engine's contract, not re-checked here. The param's
 * ovf mode selects integer overflow handling of add/sub/mul
 * (div/max/min and FP operands have no use for it).
 *
 * Scalar: run this core's share once, in the host-fitted w block the
 * launch message carries (s->B0, fitted by host/eltwise_split.cpp —
 * the firmware image never carries that code). The share is one
 * contiguous slice of the launch (equal shares in core order, the
 * last clamped to what remains, cores past the launch idle), striped
 * over the greatest common lane count its size allows: c_slice lanes,
 * so every chunk — tail included — is an even stripe. The walk is a
 * sequence of contiguous (cur_w, 1, c_slice) tiles, min-clamped at
 * the share's end, each just a fresh base address on the operand; the
 * resident tile set is allocated once at the block shape and lived at
 * the chunk shape — set_shape's bump guard keeps it honest. Chunks
 * cover the share exactly: every element computed, no access past
 * the end.
 * ==========================================================================
 */
#include "ops/ops.h"
#include "tndef.h"

#include <algorithm>

#include "common.h"
#include "kernel/kernel.h"

// params.h mirrors the kernel encoding without including kernel headers
static_assert((int)TNOP_OVF_SATURATE == (int)OVF_MODE_SATURATE &&
                  (int)TNOP_OVF_TRUNCATE == (int)OVF_MODE_TRUNCATE,
              "tnop_ovf_mode_t must mirror the kernel's ovf_mode_t");

namespace {

using EwiseFn = void (*)(tdr_idx_t, tdr_idx_t, tdr_idx_t, ovf_mode_t);

// div/max/min take no ovf mode; wrapped into the shared signature
void wrap_div(tdr_idx_t o, tdr_idx_t a, tdr_idx_t b, ovf_mode_t) { top_vec_div(o, a, b); }
void wrap_max(tdr_idx_t o, tdr_idx_t a, tdr_idx_t b, ovf_mode_t) { top_vec_max(o, a, b); }
void wrap_min(tdr_idx_t o, tdr_idx_t a, tdr_idx_t b, ovf_mode_t) { top_vec_min(o, a, b); }

EwiseFn ewise_fn(tnop_ewise_op_t op) {
    switch (op) {
    case TNOP_EWISE_ADD:
        return top_vec_add;
    case TNOP_EWISE_SUB:
        return top_vec_sub;
    case TNOP_EWISE_MUL:
        return top_vec_mul;
    case TNOP_EWISE_DIV:
        return wrap_div;
    case TNOP_EWISE_MAX:
        return wrap_max;
    case TNOP_EWISE_MIN:
        return wrap_min;
    default:
        return nullptr;
    }
}

} // namespace

/* one launch of this core's share in the split's block_w-wide chunks.
 * block_w is a descriptor field: only uint16-representable values ever
 * arrive (the host's fit loop guards the rest). */
tn_status_t tnop_eltwise(const tn_tensor_t *out, const tn_tensor_t *in1, const tn_tensor_t *in2,
                         const tnop_eltwise_param_t *param, tnop_split_t *s) {
    const uint16_t block_w = s->B0;
    const tnkr_chip_info_t *info = tnkr_spec_info();
    const uint64_t total = get_elem_num(out);
    const uint64_t per_core = (total + g_op_ctx.num_cores - 1) / g_op_ctx.num_cores;
    const tn_tensor_t *operands[3] = {out, in1, in2};
    const uint64_t start = g_op_ctx.core_idx * per_core;
    if (start >= total) {
        return TN_SUCCESS; // this core's share is empty: idle
    }
    const uint64_t share = std::min(per_core, total - start);
    const uint64_t c_slice = get_max_common_div((int)share, (int)info->smc_num);
    const uint64_t w_slice = share / c_slice;
    const EwiseFn compute = ewise_fn(param->op);

    /* the share is a sequence of contiguous (cur_w, 1, c_slice) tiles:
     * the chunk at idx_w covers cur_w*c_slice elements from
     * idx_w*c_slice into the share, striped over c_slice lanes */
    const dim4 block_shape{block_w, 1, (uint16_t)info->smc_num, 1};
    const dim4 first_chunk{(uint16_t)std::min((uint64_t)block_w, w_slice), 1, (uint16_t)c_slice, 1};
    tdr_idx_t gdesc[3], ldesc[3];

    for (int k = 0; k < 3; ++k) {
        TNOP_TRY(gtensor(operands[k]->addr + start * tn_dtype_size(operands[k]->dtype), first_chunk,
                         operands[k]->dtype, &gdesc[k]));
        TNOP_TRY(ltensor(block_shape, operands[k]->dtype, &ldesc[k]));
    }

    dim4 live{0, 0, 0, 0};
    for (uint64_t idx_w = 0; idx_w < w_slice; idx_w += block_w) {
        const dim4 chunk{(uint16_t)std::min((uint64_t)block_w, w_slice - idx_w), 1,
                         (uint16_t)c_slice, 1};
        for (int k = 0; k < 3; ++k) {
            if (chunk != live) {
                TNOP_TRY(set_shape(gdesc[k], chunk));
                TNOP_TRY(set_shape(ldesc[k], chunk));
            }
            TNOP_TRY(set_addr(gdesc[k], operands[k]->addr + (start + idx_w * c_slice) *
                                                                tn_dtype_size(operands[k]->dtype)));
        }
        live = chunk;
        top_ldst_load(ldesc[1], gdesc[1], 0, LDST_MODE_NORMAL);
        top_ldst_load(ldesc[2], gdesc[2], 0, LDST_MODE_NORMAL);
        compute(ldesc[0], ldesc[1], ldesc[2], (ovf_mode_t)param->ovf);
        top_ldst_store(gdesc[0], 0, ldesc[0], LDST_MODE_NORMAL);
    }
    return TN_SUCCESS;
}
