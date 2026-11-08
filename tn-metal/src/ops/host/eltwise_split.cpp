// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/ops/host/eltwise_split.cpp
 *
 * Host-side split fit for the element-wise op. Host-only translation
 * unit: the scalar/firmware image never compiles this directory.
 *
 * Fits the one number the scalar half cannot derive — the w block,
 * s->B0 — ppl host-wrapper style: run this core's own launch per
 * candidate block, opening with the whole per-lane run (the largest
 * useful tile) and halving on the allocator's TN_ERR_NOMEM;
 * TN_ERR_NOMEM when a minimum block cannot fit. Every candidate runs
 * on a released op scope, so the next faces a fresh window — nothing
 * survives the fit.
 * ==========================================================================
 */
#include "ops/ops.h"
#include "tndef.h"

#include <algorithm>

#include "../common.h"

/* fit the w block by running this core's launch per candidate (see the
 * banner); the fitted split is the op's whole launch-message contract */
tn_status_t tnop_eltwise_split(const tn_tensor_t *out, const tn_tensor_t *in1,
                               const tn_tensor_t *in2, const tnop_eltwise_param_t *param,
                               tnop_split_t *s) {
    const tnkr_chip_info_t *info = tnkr_spec_info();
    const uint64_t per_core = (get_elem_num(out) + g_op_ctx.num_cores - 1) / g_op_ctx.num_cores;
    const uint64_t w_slice = per_core / get_max_common_div((int)per_core, (int)info->smc_num);

    for (uint64_t block_w = w_slice; block_w != 0; block_w >>= 1) {
        if (block_w > UINT16_MAX) {
            continue; // unrepresentable in the split's uint16 field
        }
        s->B0 = (uint16_t)block_w;
        const tn_status_t status = tnop_eltwise(out, in1, in2, param, s);
        tensor_release(); // every candidate starts from a fresh op scope
        if (status == TN_SUCCESS) {
            return TN_SUCCESS; // s->B0 already holds the fitted block
        }
        if (status != TN_ERR_NOMEM) {
            return status; // a final, non-fit error
        }
    }
    return TN_ERR_NOMEM; // a minimum block cannot fit one lane window
}
