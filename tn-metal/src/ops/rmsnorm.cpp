// Copyright (c) 2026 ThinkNative. All rights reserved.

#include "ops/ops.h"

/* Scalar body of one core's RMSNorm share, planned off the launch
 * message's split (host/rmsnorm_split.cpp fits it — the firmware image
 * never carries that code). */
tn_status_t tnop_rmsnorm(const tn_tensor_t *out, const tn_tensor_t *in, const tn_tensor_t *weight,
                         const tnop_rmsnorm_param_t *param, tnop_split_t *s) {
    (void)out;
    (void)in;
    (void)weight; // optional operand, consumed by the planning (TODO)
    (void)param;
    (void)s;
    // TODO: implement the scalar body for RMSNorm
    return TN_SUCCESS;
}
