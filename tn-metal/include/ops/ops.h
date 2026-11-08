// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: include/ops/ops.h
 *
 * Ops module — typed operator entries plus their host-side split fits:
 *   host   : tnop_<op>_split (or the generic tnop_host_split below) fits
 *            the launch's local-memory plan and returns the core-0
 *            (worst-case) work share in tnop_split_t; the runtime packs
 *            it into the op launch message (runtime/tnmsg.h).
 *            Local-memory fit is settled by probing the allocator; no
 *            allocation survives the path. All host-side split code
 *            lives in src/ops/host/ and never ships in the scalar /
 *            firmware image — the firmware consumes the split instead.
 *   scalar : tnop_<op> consumes the split from the launch message to
 *            plan this core's local-memory (URF) tiles and run its
 *            share; the split logic never runs on this path.
 * tnop_split_t lives in ops/params.h: it is part of the launch-message
 * ABI shared with runtime/tnmsg.h.
 * ========================================================================== */
#pragma once

#include "ops/params.h"
#include "tndef.h"

#ifdef __cplusplus
extern "C" {
#endif

// ================================================
// Context
// ================================================
typedef struct tnop_ctx {
    uint32_t num_cores;
    uint32_t core_idx;
    bool is_scalar; // legacy: the mode is implied by the entry called
} tnop_ctx_t;

extern thread_local tnop_ctx_t g_op_ctx;
// One context per thread: the host side (re)initializes it via
// tnop_host_split() before each split computation, the scalar image
// (re)initializes it per dispatch (src/scalar/dispatch.cpp).
// Thread-local because host emulation runs one thread per core
// (tnrt_kernel_launch), each with its own core_idx
void tnop_ctx_init(uint32_t num_cores, uint32_t core_idx, bool is_scalar);

// ================================================
// op implementations
// ================================================
// launch contract (the caller's — the bodies validate nothing): operand
// pointers and s are non-NULL; tnop_<op>'s s is input, carried by the
// launch message (scalar path); tnop_<op>_split's s is pure output, the
// fitted launch-message contract — implemented in src/ops/host/,
// absent from the scalar/firmware image

tn_status_t tnop_rmsnorm(const tn_tensor_t *out, const tn_tensor_t *in, const tn_tensor_t *weight,
                         const tnop_rmsnorm_param_t *param, tnop_split_t *s);
tn_status_t tnop_eltwise(const tn_tensor_t *out, const tn_tensor_t *in1, const tn_tensor_t *in2,
                         const tnop_eltwise_param_t *param, tnop_split_t *s);

tn_status_t tnop_rmsnorm_split(const tn_tensor_t *out, const tn_tensor_t *in,
                               const tn_tensor_t *weight, const tnop_rmsnorm_param_t *param,
                               tnop_split_t *s);
tn_status_t tnop_eltwise_split(const tn_tensor_t *out, const tn_tensor_t *in1,
                               const tn_tensor_t *in2, const tnop_eltwise_param_t *param,
                               tnop_split_t *s);

// generic host-side split entry for the runtime's operator-mode message
// assembly: initializes the context (is_scalar=false, core 0) and
// dispatches to the typed op by opcode; tensors follow the opcode's
// fixed operand slot order (out first, absent optional slots NULL).
// Opcodes without a typed implementation yet yield an all-zero split.
tn_status_t tnop_host_split(uint32_t opcode, const tn_tensor_t *const *tensors,
                            uint32_t tensor_count, const void *param, uint32_t num_cores,
                            tnop_split_t *split);

#ifdef __cplusplus
}
#endif
