// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/test/test_vec_matmul.c
 *
 * top/vec matmul cases: nnn only, FP32 x FP32 -> FP32, per-channel FP32
 * bias, ACCUMULATE prefill, plus violation negatives for every
 * legality rule. Expectations are computed independently on the test
 * side with the same strict FP32-step chain (each step rounds once to
 * float); the "step semantics" case (1e8, 1, -1e8) separates the FP32
 * chain (0) from the mm-style double-tail golden (1), pinning the
 * kernel.h numeric rule.
 * ========================================================================== */
#include "smoke.h"

#include <stdlib.h>

/* poison every lane window at addr so a rejected call (violation raised
 * before any write) provably leaves the output plane untouched */
static void poison_urf(tnkr_ctx_t *ctx, uint32_t addr, uint32_t bytes) {
    uint32_t l, lane = ctx->info.urf_bytes / ctx->info.smc_num;
    for (l = 0; l < ctx->info.smc_num; l++)
        memset(ctx->urf + (uint64_t)l * lane + addr, 0xAA, bytes);
}

/* Standard nnn case: A (1,M,1,K) FP32, B (1,K,1,N) FP32, bias
 * (1,SMC_NUM,1,N) FP32 when BIAS != NULL, output
 * (1,M,1,N) FP32, all through the global->URF->op->global loop. PRE
 * non-NULL feeds ACCUMULATE. With BIAS == NULL the bias argument is an
 * allocated but never-configured slot — BIAS_MODE_NONE must not consult
 * it. */
static void run_vec_mm(tnkr_ctx_t *ctx, uint32_t M, uint32_t K, uint32_t N, const float *A,
                       const float *B, const float *BIAS, acc_mode_t acc, const float *PRE,
                       float *OUT) {
    const size_t out_bytes = (size_t)M * N * 4u;
    const uint32_t smc = ctx->info.smc_num;
    tdr_t d;
    uint8_t sga, sgb, sgo, sgbias, sua, sub_, suo, subias;

    memcpy(ctx->global + GA, A, (size_t)M * K * 4u);
    memcpy(ctx->global + GB, B, (size_t)K * N * 4u);
    if (BIAS)
        memcpy(ctx->global + GB + 4096u, BIAS, (size_t)smc * N * 4u);
    if (PRE)
        memcpy(ctx->global + GO, PRE, out_bytes);
    else
        memset(ctx->global + GO, 0xAA, out_bytes);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GA, 1, (uint16_t)M, 1, (uint16_t)K);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GB, 1, (uint16_t)K, 1, (uint16_t)N);
    sgb = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GO, 1, (uint16_t)M, 1, (uint16_t)N);
    sgo = mk_desc(ctx, &d);
    if (BIAS) {
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GB + 4096u, 1, (uint16_t)smc, 1,
                            (uint16_t)N);
        sgbias = mk_desc(ctx, &d);
    } else {
        sgbias = 0; /* unused: no bias load below */
    }
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UA, 1, (uint16_t)M, 1, (uint16_t)K, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UB, 1, (uint16_t)K, 1, (uint16_t)N, 0);
    sub_ = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UO, 1, (uint16_t)M, 1, (uint16_t)N, 0);
    suo = mk_desc(ctx, &d);
    if (BIAS) {
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UA + 2048u, 1, (uint16_t)smc, 1,
                         (uint16_t)N, 0);
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
    if (BIAS)
        top_ldst_load(subias, sgbias, 0, LDST_MODE_NORMAL);
    if (PRE)
        top_ldst_load(suo, sgo, 0, LDST_MODE_NORMAL); /* accumulator prefill */
    top_vec_matmul_nnn(suo, sua, sub_, subias, acc, BIAS ? BIAS_MODE_PER_CHANNEL : BIAS_MODE_NONE);
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(OUT, ctx->global + GO, out_bytes);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgb);
    tdr_free(ctx, sgo);
    if (BIAS)
        tdr_free(ctx, sgbias);
    tdr_free(ctx, sua);
    tdr_free(ctx, sub_);
    tdr_free(ctx, suo);
    tdr_free(ctx, subias);
}

/* Negative selector for check_vec_mm_reject */
enum {
    RJ_LEFTDT,   /* left BF16 (FP32 only) */
    RJ_RIGHTDT,  /* right BF16 */
    RJ_OUTDT,    /* output BF16 */
    RJ_BIASDT,   /* bias BF16 (FP32 only) */
    RJ_KMISM,    /* right.dc != left.dw */
    RJ_OUTSH,    /* output.dw != N */
    RJ_DH,       /* left.dh != 1 (operands are (1,X,1,Y)) */
    RJ_BIASSH,   /* bias dc != SMC_NUM */
    RJ_ACCBIAS,  /* ACCUMULATE + PER_CHANNEL bias (kernel.h: exclusive) */
    RJ_LEFTLYS,  /* left ZEROSTRIDE (operands are HWAlign) */
    RJ_BIASMODE, /* bias_mode outside {NONE, PER_CHANNEL} (reserved) */
    RJ_ACCMODE   /* acc outside {OVERWRITE, ACCUMULATE} (reserved) */
};

/* One-illegality pipeline (M=2 K=4 N=3, FP32): build valid descriptors
 * except the selected flaw, poison the output, run, store and require
 * the poison pattern to survive */
static void check_vec_mm_reject(tnkr_ctx_t *ctx, const char *name, int why) {
    const uint32_t M = 2, K = 4, N = 3;
    const uint32_t smc = ctx->info.smc_num;
    const uint32_t out_bytes = M * N * ((why == RJ_OUTDT) ? 2u : 4u);
    acc_mode_t acc = (why == RJ_ACCBIAS)   ? ACC_MODE_ACCUMULATE
                     : (why == RJ_ACCMODE) ? (acc_mode_t)2
                                           : ACC_MODE_OVERWRITE;
    bias_mode_t bm = (why == RJ_ACCBIAS || why == RJ_BIASDT || why == RJ_BIASSH)
                         ? BIAS_MODE_PER_CHANNEL
                     : (why == RJ_BIASMODE) ? (bias_mode_t)2
                                            : BIAS_MODE_NONE;
    tn_dtype_t ldt = (why == RJ_LEFTDT) ? TN_DTYPE_BF16 : TN_DTYPE_FP32;
    tn_dtype_t rdt = (why == RJ_RIGHTDT) ? TN_DTYPE_BF16 : TN_DTYPE_FP32;
    tn_dtype_t odt = (why == RJ_OUTDT) ? TN_DTYPE_BF16 : TN_DTYPE_FP32;
    tn_dtype_t bdt = (why == RJ_BIASDT) ? TN_DTYPE_BF16 : TN_DTYPE_FP32;
    tdr_layout_t llys = (why == RJ_LEFTLYS) ? TDR_LAYOUT_ZEROSTRIDE : TDR_LAYOUT_HWALIGN;
    uint8_t lc = (uint8_t)(why == RJ_LEFTLYS ? (TDR_BCAST_NB | TDR_BCAST_HB) : 0);
    uint32_t rK = (why == RJ_KMISM) ? K - 1 : K;
    uint32_t outW = (why == RJ_OUTSH) ? N + 1 : N;
    uint32_t lH = (why == RJ_DH) ? 2 : 1;
    uint32_t biasC = (why == RJ_BIASSH) ? smc + 1 : smc;
    uint8_t out[32], poison[32];
    tdr_t d;
    uint8_t sga, sgb, sgo, sgbias, sua, sub_, suo, subias;

    memset(ctx->global + GA, 0x11, M * K * 4);
    memset(ctx->global + GB, 0x22, rK * N * 4);
    memset(ctx->global + GB + 4096u, 0x33, biasC * N * 4);
    memset(ctx->global + GO, 0xAA, sizeof(out));
    memset(poison, 0xAA, sizeof(poison));
    poison_urf(ctx, UO, 64);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, ldt, GA, 1, (uint16_t)M, (uint16_t)lH, (uint16_t)K);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, rdt, GB, 1, (uint16_t)rK, 1, (uint16_t)N);
    sgb = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, odt, GO, 1, (uint16_t)M, 1, (uint16_t)outW);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, bdt, GB + 4096u, 1, (uint16_t)biasC, 1, (uint16_t)N);
    sgbias = mk_desc(ctx, &d);
    d = tdr_make_urf(llys, ldt, UA, 1, (uint16_t)M, (uint16_t)lH, (uint16_t)K, lc);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, rdt, UB, 1, (uint16_t)rK, 1, (uint16_t)N, 0);
    sub_ = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, odt, UO, 1, (uint16_t)M, 1, (uint16_t)outW, 0);
    suo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, bdt, UA + 2048u, 1, (uint16_t)biasC, 1, (uint16_t)N, 0);
    subias = mk_desc(ctx, &d);

    if (why != RJ_LEFTLYS)
        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL); /* ZS left is built in URF */
    top_ldst_load(sub_, sgb, 0, LDST_MODE_NORMAL);
    top_ldst_load(subias, sgbias, 0, LDST_MODE_NORMAL);
    TNVI_BEGIN("vec_matmul.nnn:") { top_vec_matmul_nnn(suo, sua, sub_, subias, acc, bm); }
    TNVI_END();
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(out, ctx->global + GO, sizeof(out));
    CHECK(memcmp(out, poison, out_bytes) == 0, name);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgb);
    tdr_free(ctx, sgo);
    tdr_free(ctx, sgbias);
    tdr_free(ctx, sua);
    tdr_free(ctx, sub_);
    tdr_free(ctx, suo);
    tdr_free(ctx, subias);
}

void suite_vec_matmul(tnkr_ctx_t *ctx) {
    /* small dyadic values: float/double arithmetic on both sides is
     * bit-exact, so memcmp pins the implementation bit for bit */
    static const float AV1[8] = {1.0f, 1.5f, 0.5f, 2.0f, -1.0f, 2.0f, 0.25f, 3.0f};
    static const float BV1[12] = {2.0f, 1.0f,  0.5f, 0.5f, 1.5f,  -1.0f,
                                  4.0f, 0.25f, 2.0f, 1.0f, -0.5f, 1.5f};
    uint32_t m, k, n;

    printf("== vec_matmul ==\n");

    /* ---- nnn, no bias: (1,2,1,4) x (1,4,1,3) -> (1,2,1,3) ---- */
    {
        float out[6], expct[6];
        for (m = 0; m < 2; m++)
            for (n = 0; n < 3; n++) {
                float e = 0.0f;
                for (k = 0; k < 4; k++)
                    e = (float)((double)e + (double)AV1[m * 4 + k] * (double)BV1[k * 3 + n]);
                expct[m * 3 + n] = e;
            }
        run_vec_mm(ctx, 2, 4, 3, AV1, BV1, NULL, ACC_MODE_OVERWRITE, NULL, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "nnn FP32 (no bias)");
    }

    /* ---- step semantics: the FP32 chain rounds every step, so
     * ((1e8 + 1) - 1e8) collapses to 0; the mm-style double-tail golden
     * would produce 1 ---- */
    {
        static const float A2[3] = {1e8f, 1.0f, -1e8f};
        static const float B2[3] = {1.0f, 1.0f, 1.0f};
        float out[1], expct[1] = {0.0f};
        run_vec_mm(ctx, 1, 3, 1, A2, B2, NULL, ACC_MODE_OVERWRITE, NULL, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "nnn strict FP32-step chain (1e8,1,-1e8)");
    }

    /* ---- PER_CHANNEL bias: (1,SMC_NUM,1,N), a per-lane table; row m
     * adds lane (m % SMC_NUM)'s row in one FP32 step after the reduction
     * chain. Lane rows are pairwise distinct ---- */
    {
        const uint32_t smc = ctx->info.smc_num;
        static const float COLB[3] = {0.5f, 2.0f, -1.0f};
        float out[6], expct[6];
        float *bias32 = malloc((size_t)smc * 3u * 4u);
        if (!bias32) {
            CHECK(0, "nnn bias alloc");
        } else {
            for (uint32_t l = 0; l < smc; l++)
                for (n = 0; n < 3; n++)
                    bias32[l * 3 + n] = COLB[n] + (float)l * 0.25f;
            for (m = 0; m < 2; m++)
                for (n = 0; n < 3; n++) {
                    float e = 0.0f;
                    for (k = 0; k < 4; k++)
                        e = (float)((double)e + (double)AV1[m * 4 + k] * (double)BV1[k * 3 + n]);
                    expct[m * 3 + n] =
                        (float)((double)e + (double)(COLB[n] + (float)(m % smc) * 0.25f));
                }
            run_vec_mm(ctx, 2, 4, 3, AV1, BV1, bias32, ACC_MODE_OVERWRITE, NULL, out);
            CHECK(memcmp(out, expct, sizeof(expct)) == 0, "nnn + FP32 per-channel bias");
        }
        free(bias32);
    }

    /* ---- ACCUMULATE: the output holds the running sum before the
     * instruction and gains the products in one further FP32 step ---- */
    {
        static const float PRE[6] = {0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f};
        float out[6], expct[6];
        for (m = 0; m < 2; m++)
            for (n = 0; n < 3; n++) {
                float e = 0.0f;
                for (k = 0; k < 4; k++)
                    e = (float)((double)e + (double)AV1[m * 4 + k] * (double)BV1[k * 3 + n]);
                expct[m * 3 + n] = (float)((double)e + (double)PRE[m * 3 + n]);
            }
        run_vec_mm(ctx, 2, 4, 3, AV1, BV1, NULL, ACC_MODE_ACCUMULATE, PRE, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "nnn ACCUMULATE (prefill)");
    }

    /* ---- M crosses the lane count: channel blocks stack in each lane
     * window (cb = c / smc_num) ---- */
    {
        const uint32_t smc = ctx->info.smc_num;
        const uint32_t M = smc + 2, K = 2, N = 2;
        float *A3 = malloc((size_t)M * K * 4u);
        float *B3 = malloc((size_t)K * N * 4u);
        float *out = malloc((size_t)M * N * 4u);
        float *expct = malloc((size_t)M * N * 4u);
        if (!A3 || !B3 || !out || !expct) {
            CHECK(0, "nnn M>smc alloc");
        } else {
            B3[0] = 2.0f;
            B3[1] = -1.0f;
            B3[2] = 0.5f;
            B3[3] = 4.0f;
            for (m = 0; m < M; m++) {
                A3[m * 2] = (float)(m % 8) * 0.5f;
                A3[m * 2 + 1] = (float)(m % 4) * -0.25f;
                for (n = 0; n < N; n++) {
                    float e = 0.0f;
                    for (k = 0; k < K; k++)
                        e = (float)((double)e + (double)A3[m * K + k] * (double)B3[k * N + n]);
                    expct[m * N + n] = e;
                }
            }
            run_vec_mm(ctx, M, K, N, A3, B3, NULL, ACC_MODE_OVERWRITE, NULL, out);
            CHECK(memcmp(out, expct, (size_t)M * N * 4u) == 0, "nnn M > smc_num (c-blocks)");
        }
        free(A3);
        free(B3);
        free(out);
        free(expct);
    }

    /* ---- K = 1 edge ---- */
    {
        static const float A4[2] = {1.5f, -2.0f};
        static const float B4[2] = {4.0f, 0.5f};
        float out[4], expct[4];
        for (m = 0; m < 2; m++)
            for (n = 0; n < 2; n++) {
                float e = 0.0f;
                e = (float)((double)e + (double)A4[m] * (double)B4[n]);
                expct[m * 2 + n] = e;
            }
        run_vec_mm(ctx, 2, 1, 2, A4, B4, NULL, ACC_MODE_OVERWRITE, NULL, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "nnn K=1");
    }

    /* ---- negatives: violation raised, output keeps its poison pattern ---- */
    check_vec_mm_reject(ctx, "left BF16 -> violation", RJ_LEFTDT);
    check_vec_mm_reject(ctx, "right BF16 -> violation", RJ_RIGHTDT);
    check_vec_mm_reject(ctx, "output BF16 -> violation", RJ_OUTDT);
    check_vec_mm_reject(ctx, "bias BF16 -> violation", RJ_BIASDT);
    check_vec_mm_reject(ctx, "K mismatch (r.dc != l.dw) -> violation", RJ_KMISM);
    check_vec_mm_reject(ctx, "output dw != N -> violation", RJ_OUTSH);
    check_vec_mm_reject(ctx, "left dh != 1 -> violation", RJ_DH);
    check_vec_mm_reject(ctx, "bias dc != SMC_NUM -> violation", RJ_BIASSH);
    check_vec_mm_reject(ctx, "ACCUMULATE + PER_CHANNEL bias -> violation", RJ_ACCBIAS);
    check_vec_mm_reject(ctx, "left ZEROSTRIDE -> violation", RJ_LEFTLYS);
    check_vec_mm_reject(ctx, "bias mode 2 (reserved) -> violation", RJ_BIASMODE);
    check_vec_mm_reject(ctx, "acc mode 2 (reserved) -> violation", RJ_ACCMODE);
}
