// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/ops/host/rmsnorm_split.cpp
 *
 * Host-side split fit for RMSNorm. Host-only translation unit: the
 * scalar/firmware image never compiles this directory.
 * ==========================================================================
 */
#include "ops/ops.h"
#include "tndef.h"

#include <string.h>

#include "../common.h"

tn_status_t tnop_rmsnorm_split(const tn_tensor_t *out, const tn_tensor_t *in,
                               const tn_tensor_t *weight, const tnop_rmsnorm_param_t *param,
                               tnop_split_t *s) {
    (void)out;
    (void)in;
    (void)weight; // optional operand, consumed by the fit (TODO)
    (void)param;
    // TODO: implement the host-side split logic for RMSNorm; *s is
    // zeroed until then
    memset(s, 0, sizeof(*s));
    return TN_SUCCESS;
}
