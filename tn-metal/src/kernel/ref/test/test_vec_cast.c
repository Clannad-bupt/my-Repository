// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/test/test_vec_cast.c
 *
 * top/vec cast cases: plain dtype conversion. Matrix (kernel.h):
 * int->int (widen exact, S->U clamps negatives to 0, U->S and narrowing
 * saturate), int<->FP32 (RNE/RTZ on FP32->int, then saturate), FP<->FP
 * (low->high exact, high->low saturate + RNE). INT <-> low-precision FP
 * (FP8/BF16/FP4) is illegal. Expectations are hand-computed bit patterns,
 * no codec reuse. Plus violation negatives.
 * ========================================================================== */
#include "smoke.h"

/* Standard cast case: A goes through the global->URF->op->global loop */
static void run_cast(tnkr_ctx_t *ctx, tn_dtype_t idt, tn_dtype_t odt, round_mode_t round,
                     const void *A, void *OUT) {
    size_t isz = NELEM * tn_dtype_size(idt);
    size_t osz = NELEM * tn_dtype_size(odt);
    tdr_t d;
    uint8_t sga, sgo, sua, suo;

    memcpy(ctx->global + GA, A, isz);
    memset(ctx->global + GO, 0xAA, osz);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, idt, GA, 1, 1, 4, 8);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, odt, GO, 1, 1, 4, 8);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, idt, UA, 1, 1, 4, 8, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, odt, UO, 1, 1, 4, 8, 0);
    suo = mk_desc(ctx, &d);

    top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
    top_vec_cast(suo, sua, round);
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(OUT, ctx->global + GO, osz);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgo);
    tdr_free(ctx, sua);
    tdr_free(ctx, suo);
}

/* One-illegality cast pipeline; the poisoned output must survive */
static void check_cast_reject(tnkr_ctx_t *ctx, const char *name, tn_dtype_t idt, tn_dtype_t odt,
                              int out_shrinks_w) {
    /* compare exactly the stored extent (the mismatch case stores less) */
    size_t osz = (out_shrinks_w ? NELEM / 2 : NELEM) * tn_dtype_size(odt);
    uint8_t in_a[NELEM * 4], out[NELEM * 4], poison[NELEM * 4];
    tdr_t d;
    uint8_t sga, sgo, sua, suo;

    memset(in_a, 0x11, sizeof(in_a));
    memset(poison, 0xAA, osz);
    memcpy(ctx->global + GA, in_a, NELEM * tn_dtype_size(idt));

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, idt, GA, 1, 1, 4, 8);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, odt, GO, 1, 1, 4, out_shrinks_w ? 4 : 8);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, idt, UA, 1, 1, 4, 8, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, odt, UO, 1, 1, 4, out_shrinks_w ? 4 : 8, 0);
    suo = mk_desc(ctx, &d);

    top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
    memset(ctx->urf + UO, 0xAA, osz); /* poison the output plane */
    TNVI_BEGIN("vec_cast") { top_vec_cast(suo, sua, ROUND_MODE_RNE); }
    TNVI_END();
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(out, ctx->global + GO, osz);
    CHECK(memcmp(out, poison, osz) == 0, name);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgo);
    tdr_free(ctx, sua);
    tdr_free(ctx, suo);
}

void suite_vec_cast(tnkr_ctx_t *ctx) {
    uint32_t i;

    printf("== vec_cast ==\n");

    /* ---- S32 -> S8: narrowing saturates ---- */
    {
        static const int32_t A[5] = {200, -200, 50, -128, 127};
        static const int8_t E[5] = {127, -128, 50, -128, 127};
        int32_t in_a[NELEM];
        int8_t out[NELEM], expct[NELEM];

        memset(in_a, 0, sizeof(in_a));
        memset(expct, 0, sizeof(expct));
        for (i = 0; i < 5; i++) {
            in_a[i] = A[i];
            expct[i] = E[i];
        }
        run_cast(ctx, TN_DTYPE_S32, TN_DTYPE_S8, ROUND_MODE_RNE, in_a, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "cast S32 -> S8 (saturate)");
    }

    /* ---- S8 -> U8: negatives clamp to 0 ---- */
    {
        int8_t in_a[NELEM];
        uint8_t out[NELEM], expct[NELEM];
        static const int8_t A[4] = {-1, 5, -128, 127};
        static const uint8_t E[4] = {0, 5, 0, 127};

        memset(in_a, 0, sizeof(in_a));
        memset(expct, 0, sizeof(expct));
        for (i = 0; i < 4; i++) {
            in_a[i] = A[i];
            expct[i] = E[i];
        }
        run_cast(ctx, TN_DTYPE_S8, TN_DTYPE_U8, ROUND_MODE_RNE, in_a, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "cast S8 -> U8 (clamp 0)");
    }

    /* ---- U8 -> S8: saturates ---- */
    {
        uint8_t in_a[NELEM];
        int8_t out[NELEM], expct[NELEM];
        static const uint8_t A[3] = {200, 100, 255};
        static const int8_t E[3] = {127, 100, 127};

        memset(in_a, 0, sizeof(in_a));
        memset(expct, 0, sizeof(expct));
        for (i = 0; i < 3; i++) {
            in_a[i] = A[i];
            expct[i] = E[i];
        }
        run_cast(ctx, TN_DTYPE_U8, TN_DTYPE_S8, ROUND_MODE_RNE, in_a, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "cast U8 -> S8 (saturate)");
    }

    /* ---- S16 -> FP32: exact ---- */
    {
        int16_t in_a[NELEM];
        float out[NELEM], expct[NELEM];
        static const int16_t A[3] = {-3, 1000, 32767};

        memset(in_a, 0, sizeof(in_a));
        memset(expct, 0, sizeof(expct));
        for (i = 0; i < 3; i++) {
            in_a[i] = A[i];
            expct[i] = (float)A[i];
        }
        run_cast(ctx, TN_DTYPE_S16, TN_DTYPE_FP32, ROUND_MODE_RNE, in_a, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "cast S16 -> FP32 (exact)");
    }

    /* ---- FP32 -> S16, RNE: ties go to even, out-of-range saturates ---- */
    {
        float in_a[NELEM];
        int16_t out[NELEM], expct[NELEM];
        static const float A[6] = {2.5f, 3.5f, -2.5f, -3.5f, 70000.0f, -70000.0f};
        static const int16_t E[6] = {2, 4, -2, -4, 32767, -32768};

        memset(in_a, 0, sizeof(in_a));
        memset(expct, 0, sizeof(expct));
        for (i = 0; i < 6; i++) {
            in_a[i] = A[i];
            expct[i] = E[i];
        }
        run_cast(ctx, TN_DTYPE_FP32, TN_DTYPE_S16, ROUND_MODE_RNE, in_a, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "cast FP32 -> S16 RNE (ties even)");
    }

    /* ---- FP32 -> S16, RTZ: truncation toward zero ---- */
    {
        float in_a[NELEM];
        int16_t out[NELEM], expct[NELEM];
        static const float A[3] = {2.9f, -2.9f, 3.5f};
        static const int16_t E[3] = {2, -2, 3};

        memset(in_a, 0, sizeof(in_a));
        memset(expct, 0, sizeof(expct));
        for (i = 0; i < 3; i++) {
            in_a[i] = A[i];
            expct[i] = E[i];
        }
        run_cast(ctx, TN_DTYPE_FP32, TN_DTYPE_S16, ROUND_MODE_RTZ, in_a, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "cast FP32 -> S16 RTZ (trunc)");
    }

    /* ---- BF16 -> FP32: exact ---- */
    {
        uint16_t in_a[NELEM];
        float out[NELEM], expct[NELEM];

        memset(in_a, 0, sizeof(in_a));
        memset(expct, 0, sizeof(expct));
        in_a[0] = 0x3F80; /* 1.0 */
        expct[0] = 1.0f;
        in_a[1] = 0xC080; /* -4.0 */
        expct[1] = -4.0f;
        run_cast(ctx, TN_DTYPE_BF16, TN_DTYPE_FP32, ROUND_MODE_RNE, in_a, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "cast BF16 -> FP32 (exact)");
    }

    /* ---- FP32 -> BF16: RNE tie goes even; overflow saturates ---- */
    {
        uint32_t in_a[NELEM];
        uint16_t out[NELEM], expct[NELEM];

        memset(in_a, 0, sizeof(in_a));
        memset(expct, 0, sizeof(expct));
        in_a[0] = 0x3F808000u; /* 1.00390625: tie between 0x3F80/0x3F81 */
        expct[0] = 0x3F80;     /* RNE -> even */
        in_a[1] = 0x7F000000u; /* ~1.7e38 < bf16 max: converts */
        expct[1] = 0x7F00;
        run_cast(ctx, TN_DTYPE_FP32, TN_DTYPE_BF16, ROUND_MODE_RNE, in_a, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "cast FP32 -> BF16 (RNE tie)");
    }

    /* ---- FP32 -> FP8: RNE; overflow saturates to +-448 ---- */
    {
        float in_a[NELEM];
        uint8_t out[NELEM], expct[NELEM];

        memset(in_a, 0, sizeof(in_a));
        memset(expct, 0, sizeof(expct));
        in_a[0] = 1.5f;
        expct[0] = 0x3C; /* 1.5 = 2^0 * 1.100b */
        in_a[1] = 500.0f;
        expct[1] = 0x7E; /* saturate 448 */
        in_a[2] = -500.0f;
        expct[2] = 0xFE;
        run_cast(ctx, TN_DTYPE_FP32, TN_DTYPE_FP8_E4M3, ROUND_MODE_RNE, in_a, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "cast FP32 -> FP8 (saturate)");
    }

    /* ---- FP8 -> FP32: exact ---- */
    {
        uint8_t in_a[NELEM];
        float out[NELEM], expct[NELEM];

        memset(in_a, 0, sizeof(in_a));
        memset(expct, 0, sizeof(expct));
        in_a[0] = 0x3C; /* 1.5 */
        expct[0] = 1.5f;
        in_a[1] = 0xC0; /* -2.0 */
        expct[1] = -2.0f;
        run_cast(ctx, TN_DTYPE_FP8_E4M3, TN_DTYPE_FP32, ROUND_MODE_RNE, in_a, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "cast FP8 -> FP32 (exact)");
    }

    /* ---- negatives: violation raised, output keeps its poison pattern ---- */
    check_cast_reject(ctx, "cast S8 -> BF16 (int<->low-prec FP) -> violation", TN_DTYPE_S8,
                      TN_DTYPE_BF16, 0);
    check_cast_reject(ctx, "cast BF16 -> S8 (int<->low-prec FP) -> violation", TN_DTYPE_BF16,
                      TN_DTYPE_S8, 0);
    check_cast_reject(ctx, "cast shape mismatch -> violation", TN_DTYPE_FP32, TN_DTYPE_FP32, 1);
}
