// Copyright (c) 2026 ThinkNative. All rights reserved.

#include "ops/ops.h"

thread_local tnop_ctx_t g_op_ctx;

void tnop_ctx_init(uint32_t num_cores, uint32_t core_idx, bool is_scalar) {
    g_op_ctx.num_cores = num_cores;
    g_op_ctx.core_idx = core_idx;
    g_op_ctx.is_scalar = is_scalar;
}

uint32_t tnop_ctx_num_cores(void) { return g_op_ctx.num_cores; }

uint32_t tnop_ctx_core_idx(void) { return g_op_ctx.core_idx; }
