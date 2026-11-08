// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/test/test_vec_unary.c
 *
 * top/vec unary arith cases: square / abs / floor
 * (FP32 float arithmetic, FP8/BF16 explicit patterns, INT abs with
 * saturation, plus violation negatives for illegal dtype rules).
 * Expectations are computed independently on the test side.
 * ========================================================================== */
#include "smoke.h"

#include <math.h>

/* Shared negative: load patterns, poison the output plane, the op must
 * raise a violation (illegal dtype rule) */
static void check_unary_reject(tnkr_ctx_t *ctx, const char *name, unop_fn fn, tn_dtype_t idt,
                               tn_dtype_t odt) {
    uint8_t in_a[NELEM * 4], out[NELEM * 4], poison[NELEM * 4];
    unsigned isz = tn_dtype_size(idt), osz = tn_dtype_size(odt);
    tdr_t d;
    uint8_t sga, sgo, sua, suo;
    uint32_t i;

    for (i = 0; i < NELEM * isz; i++)
        in_a[i] = (uint8_t)i;
    for (i = 0; i < NELEM * osz; i++)
        poison[i] = 0xAA;
    memcpy(ctx->global + GA, in_a, NELEM * isz);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, idt, GA, 1, 1, 4, 8);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, odt, GO, 1, 1, 4, 8);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, idt, UA, 1, 1, 4, 8, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, odt, UO, 1, 1, 4, 8, 0);
    suo = mk_desc(ctx, &d);

    top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
    memset(ctx->urf + UO, 0xAA, NELEM * osz); /* poison the output plane */
    TNVI_BEGIN("vec_") { fn(suo, sua); }
    TNVI_END();
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(out, ctx->global + GO, NELEM * osz);
    CHECK(memcmp(out, poison, NELEM * osz) == 0, name);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgo);
    tdr_free(ctx, sua);
    tdr_free(ctx, suo);
}

void suite_vec_unary(tnkr_ctx_t *ctx) {
    uint32_t i;

    printf("== vec_unary ==\n");

    /* ---- square: FP in, BF16/FP32 out ---- */
    /* FP8 -> BF16, exact patterns: 1.5^2=2.25, -2^2=4, 3^2=9, 0.5^2=0.25 */
    {
        uint8_t fa[NELEM], fout[NELEM];
        uint16_t fexp[NELEM];
        memset(fa, 0, sizeof(fa));
        memset(fexp, 0, sizeof(fexp));
        fa[0] = 0x3C; /* 1.5 -> 2.25 */
        fexp[0] = 0x4010;
        fa[1] = 0xC0; /* -2.0 -> 4.0 */
        fexp[1] = 0x4080;
        fa[2] = 0x44; /* 3.0 -> 9.0 */
        fexp[2] = 0x4110;
        fa[3] = 0x30; /* 0.5 -> 0.25 */
        fexp[3] = 0x3E80;
        run_unary(ctx, top_vec_square, TN_DTYPE_FP8_E4M3, TN_DTYPE_BF16, fa, fout);
        CHECK(memcmp(fout, fexp, sizeof(fexp)) == 0, "square FP8 -> BF16");
    }

    /* BF16 -> FP32 via float arithmetic (exact dyadic values) */
    {
        float fa[NELEM], fout[NELEM], fexp[NELEM];
        uint16_t ba[NELEM];
        for (i = 0; i < NELEM; i++)
            fa[i] = (float)((int)i - 16) * 0.25f;
        for (i = 0; i < NELEM; i++) { /* BF16 patterns of the same values */
            uint32_t u;
            memcpy(&u, &fa[i], sizeof(u));
            ba[i] = (uint16_t)(u >> 16);
            fexp[i] = fa[i] * fa[i];
        }
        run_unary(ctx, top_vec_square, TN_DTYPE_BF16, TN_DTYPE_FP32, ba, fout);
        CHECK(memcmp(fout, fexp, sizeof(fout)) == 0, "square BF16 -> FP32");
    }

    /* ---- abs ---- */
    /* S8, includes the -128 -> saturate-to-127 corner */
    {
        int64_t a64[NELEM];
        uint8_t in_a[NELEM], out[NELEM], expct[NELEM];
        for (i = 0; i < NELEM; i++)
            a64[i] = (int64_t)i - 16;
        a64[0] = -128;
        for (i = 0; i < NELEM; i++) {
            int64_t v = a64[i] < 0 ? -a64[i] : a64[i];
            in_a[i] = (uint8_t)(int8_t)a64[i];
            expct[i] = (uint8_t)t_expect_int(v, 8, 1, OVF_MODE_SATURATE);
        }
        run_unary(ctx, top_vec_abs, TN_DTYPE_S8, TN_DTYPE_S8, in_a, out);
        CHECK(memcmp(out, expct, sizeof(out)) == 0, "abs S8 (incl. -128 saturate)");
    }

    /* FP32 via float arithmetic */
    {
        float fa[NELEM], fout[NELEM], fexp[NELEM];
        for (i = 0; i < NELEM; i++) {
            fa[i] = (float)((int)i - 16) * 0.75f;
            fexp[i] = fabsf(fa[i]);
        }
        run_unary(ctx, top_vec_abs, TN_DTYPE_FP32, TN_DTYPE_FP32, fa, fout);
        CHECK(memcmp(fout, fexp, sizeof(fout)) == 0, "abs FP32");
    }

    /* ---- floor: FP in, out = in ---- */
    /* FP32 via float arithmetic (half steps, floor exact) */
    {
        float fa[NELEM], fout[NELEM], fexp[NELEM];
        for (i = 0; i < NELEM; i++) {
            fa[i] = (float)((int)i - 16) * 0.5f;
            fexp[i] = floorf(fa[i]);
        }
        run_unary(ctx, top_vec_floor, TN_DTYPE_FP32, TN_DTYPE_FP32, fa, fout);
        CHECK(memcmp(fout, fexp, sizeof(fout)) == 0, "floor FP32");
    }

    /* BF16 explicit patterns: 1.5->1.0, -1.5->-2.0, 2.0->2.0 */
    {
        uint16_t fa[NELEM], fout[NELEM], fexp[NELEM];
        memset(fa, 0, sizeof(fa));
        memset(fexp, 0, sizeof(fexp));
        fa[0] = 0x3FC0;
        fexp[0] = 0x3F80;
        fa[1] = 0xBFC0;
        fexp[1] = 0xC000;
        fa[2] = 0x4000;
        fexp[2] = 0x4000;
        run_unary(ctx, top_vec_floor, TN_DTYPE_BF16, TN_DTYPE_BF16, fa, fout);
        CHECK(memcmp(fout, fexp, sizeof(fexp)) == 0, "floor BF16");
    }

    /* ---- negatives: violation raised, output keeps its poison pattern ---- */
    check_unary_reject(ctx, "square S8 (int) -> violation", top_vec_square, TN_DTYPE_S8,
                       TN_DTYPE_S8);
    check_unary_reject(ctx, "abs U8 (unsigned) -> violation", top_vec_abs, TN_DTYPE_U8,
                       TN_DTYPE_U8);
    check_unary_reject(ctx, "floor S16 (int) -> violation", top_vec_floor, TN_DTYPE_S16,
                       TN_DTYPE_S16);
    check_unary_reject(ctx, "square FP8 -> FP8 (narrow out) -> violation", top_vec_square,
                       TN_DTYPE_FP8_E4M3, TN_DTYPE_FP8_E4M3);
}
