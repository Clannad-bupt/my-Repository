// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/test/test_mm_fp.c
 *
 * top/mm matmul fp-family cases: nnn / ntn / ntt (BF16 x BF16 -> FP32),
 * per-channel bias (BF16 + FP32), accumulator prefill, plus violation
 * negatives for every new legality rule. Expectations are computed
 * independently on the test side (own loops over value arrays, float
 * arithmetic on exact dyadic values).
 * ========================================================================== */
#include "smoke.h"

#include <stdlib.h>

typedef void (*mm_fp_fn)(tdr_idx_t, tdr_idx_t, tdr_idx_t, tdr_idx_t, acc_mode_t, bias_mode_t);

/* mechanical BF16 pattern of a float (top 16 bits of the FP32 image) */
static uint16_t bf16_of(float f) {
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return (uint16_t)(u >> 16);
}

/* poison every lane window at addr so a rejected call (violation raised
 * before any write) provably leaves the output plane untouched */
static void poison_urf(tnkr_ctx_t *ctx, uint32_t addr, uint32_t bytes) {
    uint32_t l, lane = ctx->info.urf_bytes / ctx->info.smc_num;
    for (l = 0; l < ctx->info.smc_num; l++)
        memset(ctx->urf + (uint64_t)l * lane + addr, 0xAA, bytes);
}

/* Standard fp matmul case: A (1,M,1,K), B (1,K,1,N) nnn / (1,N,1,K)
 * otherwise, output (1,M,1,N) nnn/ntn / (1,N,1,M) ntt (ODT = FP32/BF16),
 * all through the global->URF->op->global loop. BIAS16/BIAS32 (exactly
 * one, per-channel: nnn/ntn a (1,SMC_NUM,1,N) per-lane table, ntt a
 * (1,N,1,1) column) or NULL (no bias: the bias argument is then an
 * allocated but never-configured slot — BIAS_MODE_NONE must not consult
 * it); PRE non-NULL feeds ACCUMULATE (FP32 output only, so PRE implies
 * ODT=FP32). */
static void run_mm_fp(tnkr_ctx_t *ctx, mm_fp_fn fn, int mode, uint32_t M, uint32_t K, uint32_t N,
                      const uint16_t *A, const uint16_t *B, const uint16_t *BIAS16,
                      const float *BIAS32, acc_mode_t acc, const float *PRE, tn_dtype_t odt,
                      void *OUT) {
    const uint32_t out_elems = (mode == 2) ? N * M : M * N;
    const size_t out_bytes = out_elems * ((odt == TN_DTYPE_FP32) ? 4u : 2u);
    const uint32_t smc = ctx->info.smc_num;
    tdr_t d;
    uint8_t sga, sgb, sgo, sgbias, sua, sub_, suo, subias;

    memcpy(ctx->global + GA, A, M * K * 2);
    memcpy(ctx->global + GB, B, ((mode == 0) ? K * N : N * K) * 2);
    if (BIAS16)
        memcpy(ctx->global + GB + 4096u, BIAS16, ((mode == 2) ? N : smc * N) * 2);
    if (BIAS32)
        memcpy(ctx->global + GB + 4096u, BIAS32, ((mode == 2) ? N : smc * N) * 4);
    if (PRE)
        memcpy(ctx->global + GO, PRE, out_bytes);
    else
        memset(ctx->global + GO, 0xAA, out_bytes);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_BF16, GA, 1, (uint16_t)M, 1, (uint16_t)K);
    sga = mk_desc(ctx, &d);
    if (mode == 0)
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_BF16, GB, 1, (uint16_t)K, 1,
                            (uint16_t)N);
    else
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_BF16, GB, 1, (uint16_t)N, 1,
                            (uint16_t)K);
    sgb = mk_desc(ctx, &d);
    if (mode == 2)
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, odt, GO, 1, (uint16_t)N, 1, (uint16_t)M);
    else
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, odt, GO, 1, (uint16_t)M, 1, (uint16_t)N);
    sgo = mk_desc(ctx, &d);
    if (BIAS16 || BIAS32) {
        if (mode == 2)
            d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, BIAS32 ? TN_DTYPE_FP32 : TN_DTYPE_BF16,
                                GB + 4096u, 1, (uint16_t)N, 1, 1);
        else
            d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, BIAS32 ? TN_DTYPE_FP32 : TN_DTYPE_BF16,
                                GB + 4096u, 1, (uint16_t)smc, 1, (uint16_t)N);
        sgbias = mk_desc(ctx, &d);
    } else {
        sgbias = 0; /* unused: no bias load below */
    }
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_BF16, UA, 1, (uint16_t)M, 1, (uint16_t)K, 0);
    sua = mk_desc(ctx, &d);
    if (mode == 0)
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_BF16, UB, 1, (uint16_t)K, 1, (uint16_t)N, 0);
    else
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_BF16, UB, 1, (uint16_t)N, 1, (uint16_t)K, 0);
    sub_ = mk_desc(ctx, &d);
    if (mode == 2)
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, odt, UO, 1, (uint16_t)N, 1, (uint16_t)M, 0);
    else
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, odt, UO, 1, (uint16_t)M, 1, (uint16_t)N, 0);
    suo = mk_desc(ctx, &d);
    if (BIAS16 || BIAS32) {
        if (mode == 2)
            d = tdr_make_urf(TDR_LAYOUT_HWALIGN, BIAS32 ? TN_DTYPE_FP32 : TN_DTYPE_BF16, UA + 2048u,
                             1, (uint16_t)N, 1, 1, 0);
        else
            d = tdr_make_urf(TDR_LAYOUT_HWALIGN, BIAS32 ? TN_DTYPE_FP32 : TN_DTYPE_BF16, UA + 2048u,
                             1, (uint16_t)smc, 1, (uint16_t)N, 0);
        subias = mk_desc(ctx, &d);
    } else {
        /* BIAS_MODE_NONE: bias does not exist — pass an allocated but
         * never-configured slot; the call must not consult the index */
        int bidx = tdr_alloc(ctx);
        if (bidx < 0) {
            printf("  FATAL: tdr_alloc failed\n");
            g_fail++;
            bidx = 0;
        }
        subias = (uint8_t)bidx;
    }

    top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
    top_ldst_load(sub_, sgb, 0, LDST_MODE_NORMAL);
    if (BIAS16 || BIAS32)
        top_ldst_load(subias, sgbias, 0, LDST_MODE_NORMAL);
    if (PRE)
        top_ldst_load(suo, sgo, 0, LDST_MODE_NORMAL); /* accumulator prefill */
    fn(suo, sua, sub_, subias, acc, BIAS16 || BIAS32 ? BIAS_MODE_PER_CHANNEL : BIAS_MODE_NONE);
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(OUT, ctx->global + GO, out_bytes);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgb);
    tdr_free(ctx, sgo);
    if (BIAS16 || BIAS32)
        tdr_free(ctx, sgbias);
    tdr_free(ctx, sua);
    tdr_free(ctx, sub_);
    tdr_free(ctx, suo);
    tdr_free(ctx, subias);
}

/* Negative selector for check_mm_fp_reject */
enum {
    RJ_ACCBIAS,  /* ACCUMULATE + PER_CHANNEL bias (kernel.h: exclusive) */
    RJ_LEFTDT,   /* left operand FP32 (fp family is BF16-only) */
    RJ_OUTDT,    /* output dtype outside {BF16, FP32} (S8) */
    RJ_ACCBF16,  /* ACCUMULATE with a BF16 output (kernel.h: FP32 only) */
    RJ_KMISM,    /* ntn with left/right K mismatch */
    RJ_BIASSH,   /* nnn with bias dw != N */
    RJ_BIASDC,   /* nnn with bias dc != SMC_NUM */
    RJ_LEFTLYS,  /* left operand ZEROSTRIDE (operands are HWAlign) */
    RJ_BIASMODE, /* bias_mode outside {NONE, PER_CHANNEL} (reserved) */
    RJ_ACCMODE   /* acc outside {OVERWRITE, ACCUMULATE} (reserved) */
};

/* One-illegality fp pipeline (ntn skeleton, M=2 K=4 N=3): build valid
 * descriptors except the selected flaw, poison the output, run, store and
 * require the poison pattern to survive */
static void check_mm_fp_reject(tnkr_ctx_t *ctx, const char *name, int why) {
    const uint32_t M = 2, K = 4, N = 3;
    const uint32_t smc = ctx->info.smc_num;
    acc_mode_t acc = (why == RJ_ACCBIAS || why == RJ_ACCBF16) ? ACC_MODE_ACCUMULATE
                     : (why == RJ_ACCMODE)                    ? (acc_mode_t)2
                                                              : ACC_MODE_OVERWRITE;
    bias_mode_t bm = (why == RJ_ACCBIAS || why == RJ_BIASSH || why == RJ_BIASDC)
                         ? BIAS_MODE_PER_CHANNEL
                     : (why == RJ_BIASMODE) ? (bias_mode_t)2
                                            : BIAS_MODE_NONE;
    tn_dtype_t ldt = (why == RJ_LEFTDT) ? TN_DTYPE_FP32 : TN_DTYPE_BF16;
    tn_dtype_t odt = (why == RJ_OUTDT)     ? TN_DTYPE_S8
                     : (why == RJ_ACCBF16) ? TN_DTYPE_BF16
                                           : TN_DTYPE_FP32;
    tdr_layout_t llys = (why == RJ_LEFTLYS) ? TDR_LAYOUT_ZEROSTRIDE : TDR_LAYOUT_HWALIGN;
    uint8_t lc = (uint8_t)(why == RJ_LEFTLYS ? (TDR_BCAST_NB | TDR_BCAST_HB) : 0);
    uint32_t rK = (why == RJ_KMISM) ? K - 1 : K;
    uint32_t biasW = (why == RJ_BIASSH) ? N + 1 : N;
    uint32_t biasC = (why == RJ_BIASDC) ? smc + 1 : smc;
    int mode = (why == RJ_BIASSH || why == RJ_BIASDC) ? 0 : 1; /* nnn for bias-shape cases */
    const char *vexp;
    uint8_t out[24], poison[24];
    tdr_t d;
    uint8_t sga, sgb, sgo, sgbias, sua, sub_, suo, subias;

    /* expected violation substring per flaw (matches the impl messages) */
    switch (why) {
    case RJ_ACCBIAS:
        vexp = "mutually exclusive";
        break;
    case RJ_ACCBF16:
        vexp = "ACCUMULATE restricts";
        break;
    case RJ_LEFTDT:
        vexp = "BF16 left/right";
        break;
    case RJ_OUTDT:
        vexp = "output dtype must be";
        break;
    case RJ_KMISM:
        vexp = "shape mismatch";
        break;
    case RJ_BIASSH:
    case RJ_BIASDC:
        vexp = "bias must be";
        break;
    case RJ_BIASMODE:
        vexp = "bias mode must be";
        break;
    case RJ_ACCMODE:
        vexp = "acc mode must be";
        break;
    default: /* RJ_LEFTLYS */
        vexp = "operands must be HWAlign";
        break;
    }

    memset(ctx->global + GA, 0x11, M * K * 2);
    memset(ctx->global + GB, 0x22, N * rK * 2);
    memset(ctx->global + GB + 4096u, 0x33, biasC * biasW * 2);
    memset(poison, 0xAA, sizeof(poison));
    poison_urf(ctx, UO, 32);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, ldt, GA, 1, (uint16_t)M, 1, (uint16_t)K);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_BF16, GB, 1, (uint16_t)(mode == 0 ? K : N),
                        1, (uint16_t)(mode == 0 ? N : rK));
    sgb = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, odt, GO, 1, (uint16_t)M, 1, (uint16_t)N);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_BF16, GB + 4096u, 1, (uint16_t)biasC, 1,
                        (uint16_t)biasW);
    sgbias = mk_desc(ctx, &d);
    d = tdr_make_urf(llys, ldt, UA, 1, (uint16_t)M, 1, (uint16_t)K, lc);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_BF16, UB, 1, (uint16_t)(mode == 0 ? K : N), 1,
                     (uint16_t)(mode == 0 ? N : rK), 0);
    sub_ = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, odt, UO, 1, (uint16_t)M, 1, (uint16_t)N, 0);
    suo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_BF16, UA + 2048u, 1, (uint16_t)biasC, 1,
                     (uint16_t)biasW, 0);
    subias = mk_desc(ctx, &d);

    if (why != RJ_LEFTLYS)
        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL); /* ZS left is built in URF */
    top_ldst_load(sub_, sgb, 0, LDST_MODE_NORMAL);
    top_ldst_load(subias, sgbias, 0, LDST_MODE_NORMAL);
    TNVI_BEGIN(vexp) {
        if (mode == 0)
            top_mm_matmul_fp_nnn(suo, sua, sub_, subias, acc, bm);
        else
            top_mm_matmul_fp_ntn(suo, sua, sub_, subias, acc, bm);
    }
    TNVI_END();
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(out, ctx->global + GO, sizeof(out));
    CHECK(memcmp(out, poison,
                 M * N * ((odt == TN_DTYPE_FP32) ? 4u : (odt == TN_DTYPE_BF16 ? 2u : 1u))) == 0,
          name);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgb);
    tdr_free(ctx, sgo);
    tdr_free(ctx, sgbias);
    tdr_free(ctx, sua);
    tdr_free(ctx, sub_);
    tdr_free(ctx, suo);
    tdr_free(ctx, subias);
}

void suite_mm_fp(tnkr_ctx_t *ctx) {
    /* value mirrors of the BF16 pattern arrays; every value is a small
     * dyadic so float arithmetic on both sides is bit-exact */
    static const float AV1[8] = {1.0f, 1.5f, 0.5f, 2.0f, -1.0f, 2.0f, 0.25f, 3.0f};
    static const float BV1[12] = {2.0f, 1.0f,  0.5f, 0.5f, 1.5f,  -1.0f,
                                  4.0f, 0.25f, 2.0f, 1.0f, -0.5f, 1.5f};
    uint16_t A1[8], B1[12];
    uint32_t m, k, n;

    printf("== mm_fp ==\n");

    for (k = 0; k < 8; k++)
        A1[k] = bf16_of(AV1[k]);
    for (k = 0; k < 12; k++)
        B1[k] = bf16_of(BV1[k]);

    /* ---- nnn, no bias: (1,2,1,4) x (1,4,1,3) -> (1,2,1,3) ---- */
    {
        float out[6], expct[6];
        for (m = 0; m < 2; m++)
            for (n = 0; n < 3; n++) {
                float e = 0.0f;
                for (k = 0; k < 4; k++)
                    e += AV1[m * 4 + k] * BV1[k * 3 + n];
                expct[m * 3 + n] = e;
            }
        run_mm_fp(ctx, top_mm_matmul_fp_nnn, 0, 2, 4, 3, A1, B1, NULL, NULL, ACC_MODE_OVERWRITE,
                  NULL, TN_DTYPE_FP32, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "fp nnn BF16 (no bias)");
    }

    /* ---- ntn + PER_CHANNEL BF16 bias: bias (1,smc,1,N), replicated per
     * lane; each output column picks its bias value ---- */
    {
        const uint32_t smc = ctx->info.smc_num;
        static const float COLB[3] = {0.5f, 2.0f, -1.0f};
        uint16_t *bias16 = malloc((size_t)smc * 3u * 2u);
        float out[6], expct[6];
        if (!bias16) {
            CHECK(0, "fp ntn + BF16 per-channel bias");
        } else {
            for (uint32_t l = 0; l < smc; l++)
                for (n = 0; n < 3; n++)
                    bias16[l * 3 + n] = bf16_of(COLB[n] + (float)l * 0.25f);
            for (m = 0; m < 2; m++)
                for (n = 0; n < 3; n++) {
                    float e = 0.0f;
                    for (k = 0; k < 4; k++)
                        e += AV1[m * 4 + k] * BV1[n * 4 + k]; /* ntn: B (1,N,1,K) */
                    expct[m * 3 + n] = e + COLB[n] + (float)(m % smc) * 0.25f;
                }
            run_mm_fp(ctx, top_mm_matmul_fp_ntn, 1, 2, 4, 3, A1, B1, bias16, NULL,
                      ACC_MODE_OVERWRITE, NULL, TN_DTYPE_FP32, out);
            CHECK(memcmp(out, expct, sizeof(expct)) == 0, "fp ntn + BF16 per-channel bias");
        }
        free(bias16);
    }

    /* ---- ntt + PER_CHANNEL bias (1,N,1,1): output (1,N,1,M) transpose
     * mapping — the same dot products land transposed ---- */
    {
        static const float AV3[6] = {1.0f, 0.5f, 1.5f, -1.0f, 2.0f, 0.25f};
        static const float BV3[4] = {2.0f, 4.0f, 0.5f, 1.0f};
        static const float ROWB[2] = {0.5f, 1.5f};
        uint16_t A3[6], B3[4], bias16[2];
        float out[6], expct[6];
        for (k = 0; k < 6; k++)
            A3[k] = bf16_of(AV3[k]);
        for (k = 0; k < 4; k++)
            B3[k] = bf16_of(BV3[k]);
        for (k = 0; k < 2; k++)
            bias16[k] = bf16_of(ROWB[k]);
        for (n = 0; n < 2; n++)
            for (m = 0; m < 3; m++) {
                float e = 0.0f;
                for (k = 0; k < 2; k++)
                    e += AV3[m * 2 + k] * BV3[n * 2 + k];
                expct[n * 3 + m] = e + ROWB[n];
            }
        run_mm_fp(ctx, top_mm_matmul_fp_ntt, 2, 3, 2, 2, A3, B3, bias16, NULL, ACC_MODE_OVERWRITE,
                  NULL, TN_DTYPE_FP32, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "fp ntt + bias (transpose out)");
    }

    /* ---- ntn + FP32 bias (bias (1,smc,1,N) replicated per lane) ---- */
    {
        const uint32_t smc = ctx->info.smc_num;
        static const float COLB[3] = {0.5f, 2.0f, -1.0f};
        float *bias32 = malloc((size_t)smc * 3u * 4u);
        float out[6], expct[6];
        if (!bias32) {
            CHECK(0, "fp ntn + FP32 per-channel bias");
        } else {
            for (uint32_t l = 0; l < smc; l++)
                for (n = 0; n < 3; n++)
                    bias32[l * 3 + n] = COLB[n] + (float)l * 0.25f;
            for (m = 0; m < 2; m++)
                for (n = 0; n < 3; n++) {
                    float e = 0.0f;
                    for (k = 0; k < 4; k++)
                        e += AV1[m * 4 + k] * BV1[n * 4 + k];
                    expct[m * 3 + n] = e + COLB[n] + (float)(m % smc) * 0.25f;
                }
            run_mm_fp(ctx, top_mm_matmul_fp_ntn, 1, 2, 4, 3, A1, B1, NULL, bias32,
                      ACC_MODE_OVERWRITE, NULL, TN_DTYPE_FP32, out);
            CHECK(memcmp(out, expct, sizeof(expct)) == 0, "fp ntn + FP32 per-channel bias");
        }
        free(bias32);
    }

    /* ---- ntn -> BF16 output: the FP32 accumulation is cast (RNE) at
     * write-out; the A1/B1 data keeps every dot product BF16-exact, so
     * the test-side bf16_of image of the float expectation is the
     * engine's RNE result ---- */
    {
        uint16_t out16[6], exp16[6];
        for (m = 0; m < 2; m++)
            for (n = 0; n < 3; n++) {
                float e = 0.0f;
                for (k = 0; k < 4; k++)
                    e += AV1[m * 4 + k] * BV1[n * 4 + k];
                exp16[m * 3 + n] = bf16_of(e);
            }
        run_mm_fp(ctx, top_mm_matmul_fp_ntn, 1, 2, 4, 3, A1, B1, NULL, NULL, ACC_MODE_OVERWRITE,
                  NULL, TN_DTYPE_BF16, out16);
        CHECK(memcmp(out16, exp16, sizeof(exp16)) == 0, "fp ntn -> BF16 out (cast)");
    }

    /* ---- nnn + ACCUMULATE: the output holds the running sum before the
     * instruction and gains the products ---- */
    {
        static const float PRE[6] = {0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f};
        float out[6], expct[6];
        for (m = 0; m < 2; m++)
            for (n = 0; n < 3; n++) {
                float e = PRE[m * 3 + n];
                for (k = 0; k < 4; k++)
                    e += AV1[m * 4 + k] * BV1[k * 3 + n];
                expct[m * 3 + n] = e;
            }
        run_mm_fp(ctx, top_mm_matmul_fp_nnn, 0, 2, 4, 3, A1, B1, NULL, NULL, ACC_MODE_ACCUMULATE,
                  PRE, TN_DTYPE_FP32, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "fp nnn ACCUMULATE (prefill)");
    }

    /* ---- negatives: violation raised, output keeps its poison pattern ---- */
    check_mm_fp_reject(ctx, "fp ACCUMULATE + PER_CHANNEL bias -> violation", RJ_ACCBIAS);
    check_mm_fp_reject(ctx, "fp left FP32 -> violation", RJ_LEFTDT);
    check_mm_fp_reject(ctx, "fp out S8 -> violation", RJ_OUTDT);
    check_mm_fp_reject(ctx, "fp ACCUMULATE + BF16 out -> violation", RJ_ACCBF16);
    check_mm_fp_reject(ctx, "fp ntn K mismatch -> violation", RJ_KMISM);
    check_mm_fp_reject(ctx, "fp bias dw != N -> violation", RJ_BIASSH);
    check_mm_fp_reject(ctx, "fp bias dc != SMC_NUM -> violation", RJ_BIASDC);
    check_mm_fp_reject(ctx, "fp left ZEROSTRIDE -> violation", RJ_LEFTLYS);
    check_mm_fp_reject(ctx, "fp bias mode 2 (reserved) -> violation", RJ_BIASMODE);
    check_mm_fp_reject(ctx, "fp acc mode 2 (reserved) -> violation", RJ_ACCMODE);
}
