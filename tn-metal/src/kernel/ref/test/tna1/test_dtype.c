// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/test/tna1/test_dtype.c
 *
 * tna1-specific cases: the dtype capability set (a chip identity choice;
 * each generation may differ)
 *
 * Shared families test only "behavioral invariants" (derived from
 * ctx->info, automatically adapting across chips); this file pins tna1's
 * concrete choices: which dtypes tdr_shadow_write accepts/rejects — if a
 * later generation changes the capability set, it writes its own
 * assertions under its own test/<chip>/ and leaves this file untouched.
 * Compiled only with -DTN_CHIP=tna1 (the CMakeLists chip-specific block).
 * ========================================================================== */
#include "../smoke.h"

/* Single-dtype acceptance assertion: build a minimal legal URF descriptor
 * and walk the host configuration path */
static void check_dtype(tnkr_ctx_t *ctx, tn_dtype_t dt, int expect_ok, const char *name) {
    tdr_t d = tdr_make_urf(TDR_LAYOUT_HWALIGN, dt, UA, 1, 1, 4, 8, 0);
    int idx = tdr_alloc(ctx);
    int ret;

    if (idx < 0) {
        printf("  FATAL: descriptor setup failed\n");
        g_fail++;
        return;
    }
    ret = tdr_shadow_write(ctx, (uint8_t)idx, &d);
    CHECK((ret == TN_SUCCESS) == expect_ok, name);
    tdr_free(ctx, (uint8_t)idx);
}

void suite_tna1_dtype(tnkr_ctx_t *ctx) {
    printf("== tna1 dtype capability set ==\n");

    /* in the capability set: accepted */
    check_dtype(ctx, TN_DTYPE_U8, 1, "dtype U8       -> accept");
    check_dtype(ctx, TN_DTYPE_S8, 1, "dtype S8       -> accept");
    check_dtype(ctx, TN_DTYPE_U16, 1, "dtype U16      -> accept");
    check_dtype(ctx, TN_DTYPE_S16, 1, "dtype S16      -> accept");
    check_dtype(ctx, TN_DTYPE_U32, 1, "dtype U32      -> accept");
    check_dtype(ctx, TN_DTYPE_S32, 1, "dtype S32      -> accept");
    check_dtype(ctx, TN_DTYPE_FP8_E4M3, 1, "dtype FP8_E4M3 -> accept");
    check_dtype(ctx, TN_DTYPE_FP4, 1, "dtype FP4      -> accept");
    check_dtype(ctx, TN_DTYPE_UE8M0, 1, "dtype UE8M0    -> accept");
    check_dtype(ctx, TN_DTYPE_UE5M3, 1, "dtype UE5M3    -> accept");
    check_dtype(ctx, TN_DTYPE_BF16, 1, "dtype BF16     -> accept");
    check_dtype(ctx, TN_DTYPE_FP32, 1, "dtype FP32     -> accept");

    /* defined but unsupported on tna1: tdr_shadow_write rejects with
     * TN_ERR_DTYPE */
    check_dtype(ctx, TN_DTYPE_U4, 0, "dtype U4       -> reject");
    check_dtype(ctx, TN_DTYPE_S4, 0, "dtype S4       -> reject");
    check_dtype(ctx, TN_DTYPE_FP8_E5M2, 0, "dtype FP8_E5M2 -> reject");
    check_dtype(ctx, TN_DTYPE_FP16, 0, "dtype FP16     -> reject");
}
