// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/common/context.c
 *
 * Shared context implementation: lifecycle / TDR shadow / slot allocator
 *
 * Single static context: g_ctx lives in BSS, tnkr_init initializes it,
 * tnkr_deinit deactivates it; kernel.h instructions bind it via
 * tnkr_ctx_default(). No dynamic allocation anywhere — the library runs on
 * the chip's scalar core, where the context is firmware-resident.
 * ========================================================================== */
#include "kernel/common/context_internal.h"

#include <string.h>

/* The one context; BSS zero-init covers the first boot, tnkr_init re-clears
 * the mutable state so a deinit -> init cycle needs no BSS re-zero */
static tnkr_ctx_t g_ctx;
static int g_active;

/* Capabilities are compile-time constants, statically initialized from
 * the selected chip's count macros (spec_<chip>.h, the single source of
 * truth) — there is nothing to query at runtime */
static const tnkr_chip_info_t g_info = {
    .spec_id = TN_SPEC_ID,
    .smc_num = TN_SPEC_SMC_NUM,
    .core_num = TN_SPEC_CORE_NUM,
    .tdr_num = TN_SPEC_TDR_NUM,
    .gpr_num = TN_SPEC_GPR_NUM,
    .sys_num = TN_SPEC_SYS_NUM,
    .urf_bytes = TN_SPEC_URF_BYTES,
    .bank_num = TN_SPEC_BANK_NUM,
    .urf_width_bytes = TN_SPEC_URF_WIDTH_BYTES,
    .dtype_mask = TN_SPEC_DTYPE_MASK,
};

tnkr_ctx_t *tnkr_ctx_default(void) { return g_active ? &g_ctx : NULL; }

const tnkr_chip_info_t *tnkr_spec_info(void) { return &g_info; }

/* ==========================================================================
 * Lifecycle
 * ========================================================================== */
tnkr_ctx_t *tnkr_init(int *err) {
    if (g_active) {
        if (err != NULL)
            *err = TN_ERR_STATE; /* double init */
        return NULL;
    }

    /* capabilities come from the compile-time selected spec; conformance
     * with the ISA limits is already asserted at compile time by the
     * spec's own header (spec_<chip>.h) */
    g_ctx.info = g_info;

    /* reset the mutable state (BSS covered the first boot; simulated
     * urf/global content is deliberately left alone — unspecified at
     * power-on, like real memory) */
    memset(g_ctx.shadow, 0, sizeof(g_ctx.shadow));
    memset(g_ctx.valid, 0, sizeof(g_ctx.valid));
    memset(g_ctx.is_stride, 0, sizeof(g_ctx.is_stride));
    memset(g_ctx.alloc, 0, sizeof(g_ctx.alloc));
    memset(g_ctx.gpr, 0, sizeof(g_ctx.gpr));
    memset(g_ctx.sys, 0, sizeof(g_ctx.sys));
    g_ctx.urf_used = 0;
    memset(g_ctx.urf_region, 0, sizeof(g_ctx.urf_region));
    memset(g_ctx.tdr_kind, 0, sizeof(g_ctx.tdr_kind));
    memset(g_ctx.tdr_dtype, 0, sizeof(g_ctx.tdr_dtype));

    g_active = 1;
    if (err != NULL)
        *err = TN_SUCCESS;
    return &g_ctx;
}

void tnkr_deinit(tnkr_ctx_t *ctx) {
    if (ctx == NULL || ctx != &g_ctx)
        return; /* accept only the one context */
    g_active = 0;
}

uint32_t tnkr_urf_bank_size(const tnkr_ctx_t *ctx) {
    if (ctx == NULL || ctx->info.smc_num == 0 || ctx->info.bank_num == 0)
        return 0;
    return ctx->info.urf_bytes / ctx->info.smc_num / ctx->info.bank_num;
}

/* ==========================================================================
 * TDR shadow (scalar-side mirror)
 * ========================================================================== */
int tdr_shadow_write(tnkr_ctx_t *ctx, uint8_t idx, const tdr_t *desc) {
    tn_status_t ret;

    if (ctx == NULL || desc == NULL)
        return TN_ERR_PARAM;
    if (idx >= ctx->info.tdr_num)
        return TN_ERR_PARAM;
    if (!TNKR_BIT_TEST(ctx->alloc, idx))
        return TN_ERR_STATE; /* slot not allocated, config forbidden */

    /* semantic validation reuses kernel/common/tdr.h's legality check
     * (dtype/loc/layout/bcast/bcast extent/addr) */
    ret = tdr_check(desc);
    if (ret != TN_SUCCESS)
        return ret;

    /* dtype must be in the chip capability set (dtype_mask is the single
     * source of truth); both backends share one spec, so both accept
     * the same set */
    if (!(ctx->info.dtype_mask & TNKR_DTYPE_BIT(tdr_get_dtype(desc))))
        return TN_ERR_DTYPE;

    ctx->shadow[idx].desc = *desc;
    TNKR_BIT_SET(ctx->valid, idx);
    TNKR_BIT_CLR(ctx->is_stride, idx);

#if defined(TN_BACKEND_CHIP)
    tnkr_chip_tdr_program(ctx, idx, TNKR_TDR_PROGRAM_DESC);
#endif
    return TN_SUCCESS;
}

int tdr_shadow_write_stride(tnkr_ctx_t *ctx, uint8_t idx, const tdr_stride_t *s) {
    if (ctx == NULL || s == NULL)
        return TN_ERR_PARAM;
    if (idx >= ctx->info.tdr_num)
        return TN_ERR_PARAM;
    if (!TNKR_BIT_TEST(ctx->alloc, idx))
        return TN_ERR_STATE; /* slot not allocated, config forbidden */

    /* stride is raw bit-field data (4 x 32-bit), no descriptor semantics,
     * no tdr_check */
    ctx->shadow[idx].stride = *s;
    TNKR_BIT_SET(ctx->valid, idx);
    TNKR_BIT_SET(ctx->is_stride, idx);

#if defined(TN_BACKEND_CHIP)
    tnkr_chip_tdr_program(ctx, idx, TNKR_TDR_PROGRAM_STRIDE);
#endif
    return TN_SUCCESS;
}

void tdr_shadow_mark_stride(tnkr_ctx_t *ctx, uint8_t idx) {
    if (ctx == NULL || idx >= ctx->info.tdr_num)
        return;
    TNKR_BIT_SET(ctx->is_stride, idx);
}

/* ==========================================================================
 * TDR slot allocator (pure software convention, invisible to hardware)
 * ========================================================================== */
int tdr_alloc(tnkr_ctx_t *ctx) {
    uint32_t i;

    if (ctx == NULL)
        return TN_ERR_PARAM;

    for (i = 0; i < ctx->info.tdr_num; i++) {
        if (!TNKR_BIT_TEST(ctx->alloc, i)) {
            TNKR_BIT_SET(ctx->alloc, i);
            return (int)i; /* first-fit */
        }
    }
    return TN_ERR_NOMEM; /* slots exhausted: ops must free first */
}

int tdr_free(tnkr_ctx_t *ctx, uint8_t idx) {
    if (ctx == NULL || idx >= ctx->info.tdr_num)
        return TN_ERR_PARAM;
    if (!TNKR_BIT_TEST(ctx->alloc, idx))
        return TN_ERR_STATE; /* unallocated slot */

    TNKR_BIT_CLR(ctx->alloc, idx);
    TNKR_BIT_CLR(ctx->valid, idx); /* slot content expires with it */
    TNKR_BIT_CLR(ctx->is_stride, idx);
    return TN_SUCCESS;
}
