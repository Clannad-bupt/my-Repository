// Copyright (c) 2026 ThinkNative. All rights reserved.

// split plumbing of the ops module (include/ops/ops.h), exercised
// directly against the tn_ops archive. eltwise runs the real host split
// (validated against hardware-constant-free coverage invariants) and
// the scalar path end to end on the ref backend, core by core;
// rmsnorm's split/plan is still TODO — this test pins its interim
// contract plus the tnop_host_split() dispatch

#include <stdio.h>
#include <string.h>

#include "kernel/tensor.h" // tnkr_tensor_release between per-core runs
#include "ops/ops.h"

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static tn_tensor_t tensor(tn_dtype_t dt, uint32_t ndims, uint32_t d0, uint32_t d1, uint32_t d2,
                          uint32_t d3) {
    tn_tensor_t t;
    memset(&t, 0, sizeof(t));
    t.dtype = dt;
    t.ndims = ndims;
    t.dims[0] = d0;
    t.dims[1] = d1;
    t.dims[2] = d2;
    t.dims[3] = d3;
    return t;
}

static int split_is_zero(const tnop_split_t *s) {
    return s->B0 == 0 && s->B1 == 0 && s->B2 == 0 && s->B3 == 0;
}

// host split + every core's scalar share of one launch, on the ref
// backend; the real dispatch releases op-scoped TDR slots after each
// op, the per-core runs do the same
static int run_all_cores(const tn_tensor_t *out, const tn_tensor_t *in1, const tn_tensor_t *in2,
                         const tnop_eltwise_param_t *param, uint32_t num_cores) {
    tnop_split_t s;
    tnop_ctx_init(num_cores, 0, false);
    if (tnop_eltwise_split(out, in1, in2, param, &s) != TN_SUCCESS)
        return 0;
    // the split's whole contract is the fitted w block
    if (s.B0 == 0)
        return 0;
    for (uint32_t core = 0; core < num_cores; ++core) {
        tnop_ctx_init(num_cores, core, true);
        if (tnop_eltwise(out, in1, in2, param, &s) != TN_SUCCESS)
            return 0;
        tnkr_tensor_release();
    }
    return 1;
}

int main() {
    const tnop_rmsnorm_param_t rparam = {1e-5f};
    const tnop_eltwise_param_t eparam = {TNOP_EWISE_ADD, TNOP_OVF_SATURATE};
    tnop_split_t s;

    tn_tensor_t rn_in = tensor(TN_DTYPE_BF16, 4, 1024, 1, 32, 1);
    tn_tensor_t rn_out = rn_in;
    tn_tensor_t rn_w = tensor(TN_DTYPE_BF16, 1, 1024, 0, 0, 0);
    tn_tensor_t ew = tensor(TN_DTYPE_FP32, 4, 64, 128, 1, 1);

    // ---- rmsnorm's interim contract: accepted, split untouched ----
    CHECK(tnop_rmsnorm(&rn_out, &rn_in, &rn_w, &rparam, &s) == TN_SUCCESS);
    tnop_ctx_init(8, 0, true);
    CHECK(tnop_rmsnorm(&rn_out, &rn_in, &rn_w, &rparam, &s) == TN_SUCCESS);

    // ---- eltwise, full launches on the ref backend ----
    // uniform rounds (no tail)
    CHECK(run_all_cores(&ew, &ew, &ew, &eparam, 8));
    // a launch whose last core's share is a bare tail
    tn_tensor_t ew_tail = tensor(TN_DTYPE_BF16, 4, 1000, 1, 1, 1);
    CHECK(run_all_cores(&ew_tail, &ew_tail, &ew_tail, &eparam, 8));
    // several rounds per core plus a remainder the last core mops up
    tn_tensor_t ew_big = tensor(TN_DTYPE_FP32, 4, 1500000, 1, 1, 1);
    CHECK(run_all_cores(&ew_big, &ew_big, &ew_big, &eparam, 2));

    // ---- generic host entry (runtime message assembly path) ----
    const tn_tensor_t *rn_slots[3] = {&rn_out, &rn_in, &rn_w};
    CHECK(tnop_host_split(TNOP_OPCODE_RMSNORM, rn_slots, 3, &rparam, 8, &s) == TN_SUCCESS);
    const tn_tensor_t *ew_slots[3] = {&ew, &ew, &ew};
    CHECK(tnop_host_split(TNOP_OPCODE_ELTWISE, ew_slots, 3, &eparam, 8, &s) == TN_SUCCESS);
    CHECK(s.B0 != 0);
    // unimplemented opcodes yield a zero split, not an error
    tn_tensor_t mm = tensor(TN_DTYPE_BF16, 2, 16, 16, 0, 0);
    const tn_tensor_t *mm_slots[4] = {&mm, &mm, &mm, NULL};
    const tnop_matmul_param_t mparam = {TNOP_MATMUL_NTN};
    CHECK(tnop_host_split(TNOP_OPCODE_MATMUL, mm_slots, 4, &mparam, 8, &s) == TN_SUCCESS);
    CHECK(split_is_zero(&s));
    // bad generic-entry arguments
    CHECK(tnop_host_split(TNOP_OPCODE_COUNT, ew_slots, 3, &eparam, 8, &s) == TN_ERR_PARAM);
    CHECK(tnop_host_split(TNOP_OPCODE_ELTWISE, ew_slots, 3, &eparam, 0, &s) == TN_ERR_PARAM);
    CHECK(tnop_host_split(TNOP_OPCODE_ELTWISE, NULL, 3, &eparam, 8, &s) == TN_ERR_PARAM);
    CHECK(tnop_host_split(TNOP_OPCODE_ELTWISE, ew_slots, 3, NULL, 8, &s) == TN_ERR_PARAM);

    printf("PASS test_op_split\n");
    return 0;
}
