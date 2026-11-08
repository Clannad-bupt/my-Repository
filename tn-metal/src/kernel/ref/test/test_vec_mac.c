// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/test/test_vec_mac.c
 *
 * top/vec mac cases: out = out + left * right (the output doubles as the
 * accumulator), FP only (BF16/FP32 output; integers are unsupported and
 * carry no ovf mode). Plus violation negatives for the mul/mac legality
 * rules: FP output must be BF16/FP32 (fp_widen, also covers mul), the
 * output never narrows, and mac rejects integer inputs.
 * Expectations are computed independently on the test side (in double
 * with BF16-exact values).
 * ========================================================================== */
#include "smoke.h"

typedef void (*mac_fn)(tdr_idx_t, tdr_idx_t, tdr_idx_t);

/* mechanical BF16 pattern of a float (top 16 bits of the FP32 image) */
static uint16_t bf16_of(float f) {
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return (uint16_t)(u >> 16);
}

/* Standard mac case: A/B/C go through the global->URF->op->global loop,
 * C prefills the accumulator; the result lands in OUT */
static void run_mac(tnkr_ctx_t *ctx, mac_fn fn, tn_dtype_t idt, tn_dtype_t odt, const void *A,
                    const void *B, const void *C, void *OUT) {
    size_t isz = NELEM * tn_dtype_size(idt);
    size_t osz = NELEM * tn_dtype_size(odt);
    tdr_t d;
    uint8_t sga, sgb, sgo, sua, sub_, suo;

    memcpy(ctx->global + GA, A, isz);
    memcpy(ctx->global + GB, B, isz);
    memcpy(ctx->global + GO, C, osz); /* accumulator prefill */

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, idt, GA, 1, 1, 4, 8);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, idt, GB, 1, 1, 4, 8);
    sgb = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, odt, GO, 1, 1, 4, 8);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, idt, UA, 1, 1, 4, 8, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, idt, UB, 1, 1, 4, 8, 0);
    sub_ = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, odt, UO, 1, 1, 4, 8, 0);
    suo = mk_desc(ctx, &d);

    top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
    top_ldst_load(sub_, sgb, 0, LDST_MODE_NORMAL);
    top_ldst_load(suo, sgo, 0, LDST_MODE_NORMAL); /* prefill the accumulator */
    fn(suo, sua, sub_);
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(OUT, ctx->global + GO, osz);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgb);
    tdr_free(ctx, sgo);
    tdr_free(ctx, sua);
    tdr_free(ctx, sub_);
    tdr_free(ctx, suo);
}

/* One-illegality mac/mul pipeline: valid (1,1,4,8) descriptors except the
 * selected flaw; the poisoned output must survive */
static void check_mac_reject(tnkr_ctx_t *ctx, const char *name, int use_mul, tn_dtype_t idt,
                             tn_dtype_t odt) {
    size_t isz = NELEM * tn_dtype_size(idt), osz = NELEM * tn_dtype_size(odt);
    uint8_t out[NELEM * 4], poison[NELEM * 4];
    tdr_t d;
    uint8_t sga, sgb, sgo, sua, sub_, suo;

    memset(ctx->global + GA, 0x11, isz);
    memset(ctx->global + GB, 0x22, isz);
    memset(poison, 0xAA, osz);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, idt, GA, 1, 1, 4, 8);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, idt, GB, 1, 1, 4, 8);
    sgb = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, odt, GO, 1, 1, 4, 8);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, idt, UA, 1, 1, 4, 8, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, idt, UB, 1, 1, 4, 8, 0);
    sub_ = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, odt, UO, 1, 1, 4, 8, 0);
    suo = mk_desc(ctx, &d);

    top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
    top_ldst_load(sub_, sgb, 0, LDST_MODE_NORMAL);
    memset(ctx->urf + UO, 0xAA, osz); /* poison the output plane */
    if (use_mul)
        TNVI_BEGIN("vec_") { top_vec_mul(suo, sua, sub_, OVF_MODE_SATURATE); }
    TNVI_END();
    else TNVI_BEGIN("vec_") {
        top_vec_mac(suo, sua, sub_);
    }
    TNVI_END();
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(out, ctx->global + GO, osz);
    CHECK(memcmp(out, poison, osz) == 0, name);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgb);
    tdr_free(ctx, sgo);
    tdr_free(ctx, sua);
    tdr_free(ctx, sub_);
    tdr_free(ctx, suo);
}

void suite_vec_mac(tnkr_ctx_t *ctx) {
    uint32_t i;

    printf("== vec_mac ==\n");

    /* ---- FP32: out = a*b + c, computed in double on both sides ---- */
    {
        float fa[NELEM], fb[NELEM], fc[NELEM], fout[NELEM], fexp[NELEM];
        static const float A[4] = {1.5f, -2.0f, 0.25f, 3.0f};
        static const float B[4] = {2.0f, 0.5f, 8.0f, -1.0f};
        static const float C[4] = {0.25f, 1.0f, -1.0f, 2.5f};

        memset(fa, 0, sizeof(fa));
        memset(fb, 0, sizeof(fb));
        memset(fc, 0, sizeof(fc));
        memset(fexp, 0, sizeof(fexp));
        for (i = 0; i < 4; i++) {
            fa[i] = A[i];
            fb[i] = B[i];
            fc[i] = C[i];
            fexp[i] = (float)((double)A[i] * (double)B[i] + (double)C[i]);
        }
        run_mac(ctx, top_vec_mac, TN_DTYPE_FP32, TN_DTYPE_FP32, fa, fb, fc, fout);
        CHECK(memcmp(fout, fexp, sizeof(fexp)) == 0, "mac FP32");
    }

    /* ---- BF16: values stay BF16-exact (truncation == RNE here) ---- */
    {
        static const float A[3] = {1.5f, -2.0f, 0.5f};
        static const float B[3] = {2.0f, 0.25f, 3.0f};
        static const float C[3] = {0.25f, 1.0f, -0.5f};
        uint16_t fa[NELEM], fb[NELEM], fc[NELEM], fout[NELEM], fexp[NELEM];

        memset(fa, 0, sizeof(fa));
        memset(fb, 0, sizeof(fb));
        memset(fc, 0, sizeof(fc));
        memset(fexp, 0, sizeof(fexp));
        for (i = 0; i < 3; i++) {
            float r = A[i] * B[i] + C[i];
            fa[i] = bf16_of(A[i]);
            fb[i] = bf16_of(B[i]);
            fc[i] = bf16_of(C[i]);
            fexp[i] = bf16_of(r);
        }
        run_mac(ctx, top_vec_mac, TN_DTYPE_BF16, TN_DTYPE_BF16, fa, fb, fc, fout);
        CHECK(memcmp(fout, fexp, sizeof(fexp)) == 0, "mac BF16");
    }

    /* ---- negatives: violation raised, output keeps its poison pattern ---- */
    check_mac_reject(ctx, "mac FP8 -> FP8 (fp_widen) -> violation", 0, TN_DTYPE_FP8_E4M3,
                     TN_DTYPE_FP8_E4M3);
    check_mac_reject(ctx, "mul FP8 -> FP8 (fp_widen) -> violation", 1, TN_DTYPE_FP8_E4M3,
                     TN_DTYPE_FP8_E4M3);
    check_mac_reject(ctx, "mul S16 -> S8 (narrowing) -> violation", 1, TN_DTYPE_S16, TN_DTYPE_S8);
    check_mac_reject(ctx, "mac S16 (integers unsupported) -> violation", 0, TN_DTYPE_S16,
                     TN_DTYPE_S16);

    /* mac with a Constant left operand: Constant goes on right (opd1)
     * only (kernel.h) -> violation before any write, poison survives */
    {
        float fb[NELEM], fout[NELEM];
        uint8_t poison[NELEM * 4];
        tdr_t d;
        uint8_t sgb, sc, sub_, suo, sgo;

        memset(fb, 0, sizeof(fb));
        fb[0] = 1.5f;
        fb[1] = -2.0f;
        memset(poison, 0xAA, sizeof(poison));
        memcpy(ctx->global + GB, fb, sizeof(fb));

        d = tdr_make_urf_constant(TN_DTYPE_FP32, 0x40000000u); /* 2.0f */
        sc = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GB, 1, 1, 4, 8);
        sgb = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UB, 1, 1, 4, 8, 0);
        sub_ = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UO, 1, 1, 4, 8, 0);
        suo = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GO, 1, 1, 4, 8);
        sgo = mk_desc(ctx, &d);

        top_ldst_load(sub_, sgb, 0, LDST_MODE_NORMAL);
        memset(ctx->urf + UO, 0xAA, NELEM * 4u); /* poison the output plane */
        TNVI_BEGIN("vec_mac") { top_vec_mac(suo, sc, sub_); }
        TNVI_END();
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(fout, ctx->global + GO, sizeof(fout));
        CHECK(memcmp(fout, poison, sizeof(fout)) == 0, "mac left Constant -> violation");

        tdr_free(ctx, sgb);
        tdr_free(ctx, sc);
        tdr_free(ctx, sub_);
        tdr_free(ctx, suo);
        tdr_free(ctx, sgo);
    }
}
