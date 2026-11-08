// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/test/test_mm_bq.c
 *
 * top/mm matmul bq-family cases: FP8/FP4 data with UE8M0 (FP8/mxfp4) and
 * UE5M3 (nxfp4) block scales, modes ntn / ntt, group indexing, bias and
 * accumulator, plus violation negatives. Every case carries several
 * groups per row with a distinct scale per (row, group), so a broken
 * scale index (row om/on or group kk/g) changes the result. Scale bytes
 * are picked from documented pattern<->value tables; expectations are
 * computed independently on the test side with the spec's block-scaling
 * model: per-group dot product (no scales), then one scale multiply per
 * group — UE8M0 scales are exact powers of two, the FP4+UE5M3 scale is
 * the ls*rs product rounded in the UE5M3 domain (tnkr_encode_fp RNE) —
 * accumulated in double, one float cast at the end (engine alignment).
 * ========================================================================== */
#include "smoke.h"

#include "kernel/ref/dtype.h" /* codec-level pins below (pure functions) */

#include <stdlib.h>

typedef void (*mm_bq_fn)(tdr_idx_t, tdr_idx_t, tdr_idx_t, tdr_idx_t, tdr_idx_t, tdr_idx_t,
                         group_size_t, acc_mode_t, bias_mode_t);

/* pattern <-> value tables (E4M3 per kernel.h dtype.h; UE8M0/UE5M3 per
 * dtype.h's ref-model interpretation) */
typedef struct {
    uint8_t pat;
    float val;
} pat_val_t;

/* FP8 E4M3: 0x38=1, 0x30=0.5, 0x3C=1.5, 0x40=2, 0x44=3, 0x48=4, 0xB0=-0.5,
 * 0xB8=-1, 0xC0=-2 */
static const pat_val_t FP8_TAB[] = {{0x38, 1.0f},  {0x30, 0.5f},  {0x3C, 1.5f},
                                    {0x40, 2.0f},  {0x44, 3.0f},  {0x48, 4.0f},
                                    {0xB0, -0.5f}, {0xB8, -1.0f}, {0xC0, -2.0f}};
/* FP4 E2M1 (all 16 patterns finite, OCP/NVIDIA semantics: 0x7=+6, 0xF=-6):
 * 0x1=0.5, 0x2=1, 0x3=1.5, 0x4=2, 0x5=3, 0x6=4, 0x7=6, 0x9=-0.5, 0xA=-1,
 * 0xB=-1.5, 0xC=-2, 0xD=-3, 0xE=-4, 0xF=-6 */
static const pat_val_t FP4_TAB[] = {{0x1, 0.5f},  {0x2, 1.0f},  {0x3, 1.5f},  {0x4, 2.0f},
                                    {0x5, 3.0f},  {0x6, 4.0f},  {0x7, 6.0f},  {0x9, -0.5f},
                                    {0xA, -1.0f}, {0xB, -1.5f}, {0xC, -2.0f}, {0xD, -3.0f},
                                    {0xE, -4.0f}, {0xF, -6.0f}};
/* UE8M0: v = 2^(e-127); 0x7F/0xFF = NaN (unused) */
static const pat_val_t UE8M0_TAB[] = {{0x7D, 0.25f}, {0x7E, 0.5f}, {0x80, 2.0f}, {0x81, 4.0f}};
/* UE5M3: bias 15, E4M3-style all-ones-exp finite; 0x78=1, 0x79=1.125,
 * 0x80=2, 0x81=2.25, 0x88=4, 0x90=8 */
static const pat_val_t UE5M3_TAB[] = {{0x78, 1.0f},  {0x79, 1.125f}, {0x80, 2.0f},
                                      {0x81, 2.25f}, {0x88, 4.0f},   {0x90, 8.0f}};

#define FP8_N (sizeof(FP8_TAB) / sizeof(FP8_TAB[0]))
#define FP4_N (sizeof(FP4_TAB) / sizeof(FP4_TAB[0]))

/* test-side echo of the kernel_types.h group_size_t convention */
static uint32_t group_width(group_size_t gs) { return (gs == GROUP_SIZE_32) ? 32u : 16u; }

/* float value of a BF16 pattern (top 16 bits of the FP32 image) */
static float bias_of(uint16_t b) {
    uint32_t u = (uint32_t)b << 16;
    float f;
    memcpy(&f, &u, sizeof(f));
    return f;
}

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

/* Standard bq matmul case: A (1,M,1,K), B (1,N,1,K), scales (1,M,1,K//g)
 * / (1,N,1,K//g), output (1,M,1,N) ntn / (1,N,1,M) ntt, all through the
 * global->URF->op->global loop. A/B are raw byte images (FP8 unpacked or
 * FP4 packed, abytes/bbytes long). BIAS16/BIAS32 (exactly one): ntn a
 * (1,SMC_NUM,1,N) per-lane table, ntt a (1,N,1,1) column. */
static void run_mm_bq(tnkr_ctx_t *ctx, mm_bq_fn fn, int mode, uint32_t M, uint32_t K, uint32_t N,
                      tn_dtype_t adt, tn_dtype_t lsdt, tn_dtype_t rsdt, const uint8_t *A,
                      size_t abytes, const uint8_t *B, size_t bbytes, const uint8_t *LS,
                      const uint8_t *RS, const uint16_t *BIAS16, const float *BIAS32,
                      group_size_t gs, acc_mode_t acc, const float *PRE, tn_dtype_t odt,
                      void *OUT) {
    const uint32_t g = group_width(gs);
    const uint32_t KG = K / g;
    const uint32_t out_elems = (mode == 2) ? N * M : M * N;
    const size_t out_bytes = out_elems * ((odt == TN_DTYPE_FP32) ? 4u : 2u);
    const uint32_t smc = ctx->info.smc_num;
    tdr_t d;
    uint8_t sga, sgb, sgls, sgrs, sgo, sgbias, sua, sub_, suls, surs, suo, subias;

    memcpy(ctx->global + GA, A, abytes);
    memcpy(ctx->global + GB, B, bbytes);
    memcpy(ctx->global + GB + 512u, LS, M * KG);
    memcpy(ctx->global + GB + 1024u, RS, N * KG);
    if (BIAS16)
        memcpy(ctx->global + GB + 4096u, BIAS16, ((mode == 2) ? N : smc * N) * 2);
    if (BIAS32)
        memcpy(ctx->global + GB + 4096u, BIAS32, ((mode == 2) ? N : smc * N) * 4);
    if (PRE)
        memcpy(ctx->global + GO, PRE, out_bytes);
    else
        memset(ctx->global + GO, 0xAA, out_bytes);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, adt, GA, 1, (uint16_t)M, 1, (uint16_t)K);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, adt, GB, 1, (uint16_t)N, 1, (uint16_t)K);
    sgb = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, lsdt, GB + 512u, 1, (uint16_t)M, 1, (uint16_t)KG);
    sgls = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, rsdt, GB + 1024u, 1, (uint16_t)N, 1, (uint16_t)KG);
    sgrs = mk_desc(ctx, &d);
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
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, adt, UA, 1, (uint16_t)M, 1, (uint16_t)K, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, adt, UB, 1, (uint16_t)N, 1, (uint16_t)K, 0);
    sub_ = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, lsdt, UB + 1024u, 1, (uint16_t)M, 1, (uint16_t)KG, 0);
    suls = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, rsdt, UB + 2048u, 1, (uint16_t)N, 1, (uint16_t)KG, 0);
    surs = mk_desc(ctx, &d);
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
    top_ldst_load(suls, sgls, 0, LDST_MODE_NORMAL);
    top_ldst_load(surs, sgrs, 0, LDST_MODE_NORMAL);
    if (BIAS16 || BIAS32)
        top_ldst_load(subias, sgbias, 0, LDST_MODE_NORMAL);
    if (PRE)
        top_ldst_load(suo, sgo, 0, LDST_MODE_NORMAL); /* accumulator prefill */
    fn(suo, sua, sub_, subias, suls, surs, gs, acc,
       BIAS16 || BIAS32 ? BIAS_MODE_PER_CHANNEL : BIAS_MODE_NONE);
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(OUT, ctx->global + GO, out_bytes);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgb);
    tdr_free(ctx, sgls);
    tdr_free(ctx, sgrs);
    tdr_free(ctx, sgo);
    if (BIAS16 || BIAS32)
        tdr_free(ctx, sgbias);
    tdr_free(ctx, sua);
    tdr_free(ctx, sub_);
    tdr_free(ctx, suls);
    tdr_free(ctx, surs);
    tdr_free(ctx, suo);
    tdr_free(ctx, subias);
}

/* Negative selector for check_mm_bq_reject */
enum {
    BJ_SCALEDT,  /* FP8 data with a UE5M3 scale (dtype pairing rule) */
    BJ_KMOD,     /* K not a multiple of the GROUP_SIZE_32 width */
    BJ_ACCBIAS,  /* ACCUMULATE + PER_CHANNEL bias */
    BJ_OUTDT,    /* output dtype outside {BF16, FP32} (S8) */
    BJ_ACCBF16,  /* ACCUMULATE with a BF16 output (kernel.h: FP32 only) */
    BJ_BIASMODE, /* bias_mode outside {NONE, PER_CHANNEL} (reserved) */
    BJ_ACCMODE   /* acc outside {OVERWRITE, ACCUMULATE} (reserved) */
};

/* One-illegality bq pipeline (ntn skeleton, M=2 K=64 N=3): valid except
 * the selected flaw; the op must raise a violation, poison intact */
static void check_mm_bq_reject(tnkr_ctx_t *ctx, const char *name, int why) {
    const uint32_t M = 2, N = 3;
    const uint32_t smc = ctx->info.smc_num;
    uint32_t K = (why == BJ_KMOD) ? 48 : 64, KG = K / 32u;
    tn_dtype_t adt = TN_DTYPE_FP8_E4M3; /* the reject skeleton runs FP8 operands */
    tn_dtype_t rsdt = TN_DTYPE_UE8M0;
    const char *vexp;
    switch (why) {
    case BJ_SCALEDT:
        vexp = "FP8 requires UE8M0";
        break;
    case BJ_KMOD:
        vexp = "multiple of the group width";
        break;
    case BJ_ACCBIAS:
        vexp = "mutually exclusive";
        break;
    case BJ_OUTDT:
        vexp = "output dtype must be";
        break;
    case BJ_BIASMODE:
        vexp = "bias mode must be";
        break;
    case BJ_ACCMODE:
        vexp = "acc mode must be";
        break;
    default: /* BJ_ACCBF16 */
        vexp = "ACCUMULATE restricts";
        break;
    }
    tn_dtype_t lsdt = (why == BJ_SCALEDT) ? TN_DTYPE_UE5M3 : TN_DTYPE_UE8M0;
    tn_dtype_t odt = (why == BJ_OUTDT)     ? TN_DTYPE_S8
                     : (why == BJ_ACCBF16) ? TN_DTYPE_BF16
                                           : TN_DTYPE_FP32;
    acc_mode_t acc = (why == BJ_ACCBIAS || why == BJ_ACCBF16) ? ACC_MODE_ACCUMULATE
                     : (why == BJ_ACCMODE)                    ? (acc_mode_t)2
                                                              : ACC_MODE_OVERWRITE;
    bias_mode_t bm = (why == BJ_ACCBIAS)    ? BIAS_MODE_PER_CHANNEL
                     : (why == BJ_BIASMODE) ? (bias_mode_t)2
                                            : BIAS_MODE_NONE;
    uint8_t out[24], poison[24];
    tdr_t d;
    uint8_t sga, sgb, sgls, sgrs, sgo, sgbias, sua, sub_, suls, surs, suo, subias;

    memset(ctx->global + GA, 0x11, M * K);
    memset(ctx->global + GB, 0x22, N * K);
    memset(ctx->global + GB + 512u, 0x33, M * KG);
    memset(ctx->global + GB + 1024u, 0x44, N * KG);
    memset(ctx->global + GB + 4096u, 0x55, smc * N * 2);
    memset(poison, 0xAA, sizeof(poison));
    poison_urf(ctx, UO, 32);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, adt, GA, 1, (uint16_t)M, 1, (uint16_t)K);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, adt, GB, 1, (uint16_t)N, 1, (uint16_t)K);
    sgb = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, lsdt, GB + 512u, 1, (uint16_t)M, 1, (uint16_t)KG);
    sgls = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, rsdt, GB + 1024u, 1, (uint16_t)N, 1, (uint16_t)KG);
    sgrs = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, odt, GO, 1, (uint16_t)M, 1, (uint16_t)N);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_BF16, GB + 4096u, 1, (uint16_t)smc, 1,
                        (uint16_t)N);
    sgbias = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, adt, UA, 1, (uint16_t)M, 1, (uint16_t)K, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, adt, UB, 1, (uint16_t)N, 1, (uint16_t)K, 0);
    sub_ = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, lsdt, UB + 1024u, 1, (uint16_t)M, 1, (uint16_t)KG, 0);
    suls = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, rsdt, UB + 2048u, 1, (uint16_t)N, 1, (uint16_t)KG, 0);
    surs = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, odt, UO, 1, (uint16_t)M, 1, (uint16_t)N, 0);
    suo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_BF16, UA + 2048u, 1, (uint16_t)smc, 1,
                     (uint16_t)N, 0);
    subias = mk_desc(ctx, &d);

    top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
    top_ldst_load(sub_, sgb, 0, LDST_MODE_NORMAL);
    top_ldst_load(suls, sgls, 0, LDST_MODE_NORMAL);
    top_ldst_load(surs, sgrs, 0, LDST_MODE_NORMAL);
    top_ldst_load(subias, sgbias, 0, LDST_MODE_NORMAL);
    TNVI_BEGIN(vexp) {
        top_mm_matmul_bq_ntn(suo, sua, sub_, subias, suls, surs, GROUP_SIZE_32, acc, bm);
    }
    TNVI_END();
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(out, ctx->global + GO, sizeof(out));
    CHECK(memcmp(out, poison,
                 M * N * ((odt == TN_DTYPE_FP32) ? 4u : (odt == TN_DTYPE_BF16 ? 2u : 1u))) == 0,
          name);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgb);
    tdr_free(ctx, sgls);
    tdr_free(ctx, sgrs);
    tdr_free(ctx, sgo);
    tdr_free(ctx, sgbias);
    tdr_free(ctx, sua);
    tdr_free(ctx, sub_);
    tdr_free(ctx, suls);
    tdr_free(ctx, surs);
    tdr_free(ctx, suo);
    tdr_free(ctx, subias);
}

void suite_mm_bq(tnkr_ctx_t *ctx) {
    uint32_t m, k, n;

    printf("== mm_bq ==\n");

    /* ---- codec-level pins for the FP4 NaN-pattern fix (pure functions;
     * expectations are spec tables: E2M1 has no NaN, 0x7/0xF are +-6) ---- */
    CHECK(tnkr_decode_fp(0x7, TN_DTYPE_FP4) == 6.0 && tnkr_decode_fp(0xF, TN_DTYPE_FP4) == -6.0,
          "FP4 decode 0x7/0xF -> +-6 (no NaN)");
    CHECK(tnkr_encode_fp(6.0, TN_DTYPE_FP4) == 0x7 && tnkr_encode_fp(-6.0, TN_DTYPE_FP4) == 0xF,
          "FP4 encode +-6 roundtrip");
    CHECK(tnkr_encode_fp(7.0, TN_DTYPE_FP4) == 0x7 && tnkr_encode_fp(-7.0, TN_DTYPE_FP4) == 0xF,
          "FP4 finite overflow saturates");
    CHECK(tnkr_encode_fp(INFINITY, TN_DTYPE_FP4) == 0x7 &&
              tnkr_encode_fp(-INFINITY, TN_DTYPE_FP4) == 0xF,
          "FP4 inf saturates with sign");
    CHECK(tnkr_encode_fp(NAN, TN_DTYPE_FP4) == 0x7, "FP4 NaN saturates to +6");
    CHECK(tnkr_encode_fp(NAN, TN_DTYPE_FP8_E4M3) == 0x7F, "FP8 NaN keeps its pattern");

    /* ---- ntn FP8 + UE8M0, M=2 K=64 N=3: two scale groups; the zero-scale
     * cells (left m=1 g=1, right n=2 g=1) kill the second half of their
     * reductions ---- */
    {
        const uint32_t smc = ctx->info.smc_num;
        uint8_t A[2 * 64], B[3 * 64], LS[2 * 2], RS[3 * 2];
        uint16_t *BIAS = malloc((size_t)smc * 3u * 2u);
        float AV[2 * 64], BV[3 * 64], LSF[2 * 2], RSF[3 * 2];
        const uint32_t g = group_width(GROUP_SIZE_32);
        float out[6], expct[6];
        for (m = 0; m < 2; m++)
            for (k = 0; k < 64; k++) {
                const pat_val_t *e = &FP8_TAB[(k * 7 + m * 5) % FP8_N];
                A[m * 64 + k] = e->pat;
                AV[m * 64 + k] = e->val;
            }
        for (n = 0; n < 3; n++)
            for (k = 0; k < 64; k++) {
                const pat_val_t *e = &FP8_TAB[(k * 3 + n * 4 + 1) % FP8_N];
                B[n * 64 + k] = e->pat;
                BV[n * 64 + k] = e->val;
            }
        for (m = 0; m < 2; m++)
            for (k = 0; k < 2; k++) {
                /* m=1, group 1 gets a zero left scale (0x00 decodes to +0,
                 * zeroing the whole group) */
                const pat_val_t *e = (m == 1 && k == 1) ? NULL : &UE8M0_TAB[(m + k * 3) % 4];
                LS[m * 2 + k] = e ? e->pat : 0x00;
                LSF[m * 2 + k] = e ? e->val : 0.0f;
            }
        for (n = 0; n < 3; n++)
            for (k = 0; k < 2; k++) {
                /* n=2, group 1 gets a zero right scale, same convention */
                const pat_val_t *e =
                    (n == 2 && k == 1) ? NULL : &UE8M0_TAB[(n * 5 + k * 2 + 1) % 4];
                RS[n * 2 + k] = e ? e->pat : 0x00;
                RSF[n * 2 + k] = e ? e->val : 0.0f;
            }
        if (!BIAS) {
            CHECK(0, "bq ntn FP8 + UE8M0 + BF16 bias (2 groups, zero scale)");
        } else {
            for (uint32_t l = 0; l < smc; l++)
                for (n = 0; n < 3; n++)
                    BIAS[l * 3 + n] = bf16_of(0.5f + (float)l * 0.25f + (float)n * 2.0f);
            for (m = 0; m < 2; m++)
                for (n = 0; n < 3; n++) {
                    /* block-scaling accounting (spec): per-group dot
                     * product, then one scale multiply; UE8M0 scales are
                     * powers of two, the product is exact */
                    double e = 0.0;
                    uint32_t b, kk;
                    for (b = 0; b < 64 / g; b++) {
                        double blk = 0.0;
                        for (kk = b * g; kk < (b + 1) * g; kk++)
                            blk += (double)AV[m * 64 + kk] * (double)BV[n * 64 + kk];
                        e += (double)LSF[m * 2 + b] * (double)RSF[n * 2 + b] * blk;
                    }
                    /* engine: double accumulation, bias added in double, one
                     * RNE cast at write-out */
                    expct[m * 3 + n] = (float)(e + (double)bias_of(BIAS[(m % smc) * 3 + n]));
                }
            run_mm_bq(ctx, top_mm_matmul_bq_ntn, 1, 2, 64, 3, TN_DTYPE_FP8_E4M3, TN_DTYPE_UE8M0,
                      TN_DTYPE_UE8M0, A, sizeof(A), B, sizeof(B), LS, RS, BIAS, NULL, GROUP_SIZE_32,
                      ACC_MODE_OVERWRITE, NULL, TN_DTYPE_FP32, out);
            CHECK(memcmp(out, expct, sizeof(expct)) == 0,
                  "bq ntn FP8 + UE8M0 + BF16 bias (2 groups, zero scale)");
        }
        free(BIAS);
    }

    /* ---- ntn FP8 + UE8M0, GROUP_SIZE_16, M=2 K=48 N=3: three 16-wide
     * groups per row — the FP8 path with fine-grained scale indexing ---- */
    {
        uint8_t A[2 * 48], B[3 * 48], LS[2 * 3], RS[3 * 3];
        float AV[2 * 48], BV[3 * 48], LSF[2 * 3], RSF[3 * 3];
        /* per (row, group) indices into UE8M0_TAB, distinct within a row */
        static const int LSI[6] = {0, 1, 2, 3, 0, 1};
        static const int RSI[9] = {2, 0, 1, 3, 2, 0, 1, 3, 2};
        const uint32_t g = group_width(GROUP_SIZE_16);
        float out[6], expct[6];

        for (m = 0; m < 6; m++) {
            LS[m] = UE8M0_TAB[LSI[m]].pat;
            LSF[m] = UE8M0_TAB[LSI[m]].val;
        }
        for (n = 0; n < 9; n++) {
            RS[n] = UE8M0_TAB[RSI[n]].pat;
            RSF[n] = UE8M0_TAB[RSI[n]].val;
        }
        for (m = 0; m < 2; m++)
            for (k = 0; k < 48; k++) {
                const pat_val_t *e = &FP8_TAB[(k * 7 + m * 5 + 1) % FP8_N];
                A[m * 48 + k] = e->pat;
                AV[m * 48 + k] = e->val;
            }
        for (n = 0; n < 3; n++)
            for (k = 0; k < 48; k++) {
                const pat_val_t *e = &FP8_TAB[(k * 3 + n * 4 + 2) % FP8_N];
                B[n * 48 + k] = e->pat;
                BV[n * 48 + k] = e->val;
            }
        for (m = 0; m < 2; m++)
            for (n = 0; n < 3; n++) {
                /* block-scaling accounting (spec); UE8M0 exact */
                double e = 0.0;
                uint32_t b, kk;
                for (b = 0; b < 48 / g; b++) {
                    double blk = 0.0;
                    for (kk = b * g; kk < (b + 1) * g; kk++)
                        blk += (double)AV[m * 48 + kk] * (double)BV[n * 48 + kk];
                    e += (double)LSF[m * 3 + b] * (double)RSF[n * 3 + b] * blk;
                }
                expct[m * 3 + n] = (float)e;
            }
        run_mm_bq(ctx, top_mm_matmul_bq_ntn, 1, 2, 48, 3, TN_DTYPE_FP8_E4M3, TN_DTYPE_UE8M0,
                  TN_DTYPE_UE8M0, A, sizeof(A), B, sizeof(B), LS, RS, NULL, NULL, GROUP_SIZE_16,
                  ACC_MODE_OVERWRITE, NULL, TN_DTYPE_FP32, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "bq ntn FP8 GROUP_SIZE_16 (K=48)");
    }

    /* ---- ntt FP8 + UE8M0 + FP32 per-channel bias, K=96 (three groups per
     * row, a distinct scale per (row, group); output (1,N,1,M)) ---- */
    {
        uint8_t A[2 * 96], B[2 * 96], LS[2 * 3], RS[2 * 3];
        float AV[2 * 96], BV[2 * 96], LSF[2 * 3], RSF[2 * 3];
        static const float BIAS[2] = {0.5f, 1.5f};
        /* per (row, group) indices into UE8M0_TAB, distinct within a row */
        static const int LSI[6] = {0, 2, 1, 3, 0, 2};
        static const int RSI[6] = {2, 0, 3, 1, 2, 0};
        const uint32_t g = group_width(GROUP_SIZE_32);
        float out[4], expct[4];

        for (m = 0; m < 6; m++) {
            LS[m] = UE8M0_TAB[LSI[m]].pat;
            LSF[m] = UE8M0_TAB[LSI[m]].val;
            RS[m] = UE8M0_TAB[RSI[m]].pat;
            RSF[m] = UE8M0_TAB[RSI[m]].val;
        }
        for (m = 0; m < 2; m++)
            for (k = 0; k < 96; k++) {
                const pat_val_t *e = &FP8_TAB[(k * 5 + m * 2 + 3) % FP8_N];
                A[m * 96 + k] = e->pat;
                AV[m * 96 + k] = e->val;
            }
        for (n = 0; n < 2; n++)
            for (k = 0; k < 96; k++) {
                const pat_val_t *e = &FP8_TAB[(k * 3 + n * 7 + 2) % FP8_N];
                B[n * 96 + k] = e->pat;
                BV[n * 96 + k] = e->val;
            }
        for (n = 0; n < 2; n++)
            for (m = 0; m < 2; m++) {
                /* block-scaling accounting (spec); UE8M0 exact */
                double e = 0.0;
                uint32_t b, kk;
                for (b = 0; b < 96 / g; b++) {
                    double blk = 0.0;
                    for (kk = b * g; kk < (b + 1) * g; kk++)
                        blk += (double)AV[m * 96 + kk] * (double)BV[n * 96 + kk];
                    e += (double)LSF[m * 3 + b] * (double)RSF[n * 3 + b] * blk;
                }
                expct[n * 2 + m] = (float)e + BIAS[n];
            }
        run_mm_bq(ctx, top_mm_matmul_bq_ntt, 2, 2, 96, 2, TN_DTYPE_FP8_E4M3, TN_DTYPE_UE8M0,
                  TN_DTYPE_UE8M0, A, sizeof(A), B, sizeof(B), LS, RS, NULL, BIAS, GROUP_SIZE_32,
                  ACC_MODE_OVERWRITE, NULL, TN_DTYPE_FP32, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "bq ntt FP8 + UE8M0 3-group + FP32 bias");
    }

    /* ---- ntn FP8 + UE8M0 + ACCUMULATE prefill, K=64 (two groups per row,
     * distinct scale per group) ---- */
    {
        uint8_t A[2 * 64], B[2 * 64], LS[2 * 2], RS[2 * 2];
        float AV[2 * 64], BV[2 * 64], LSF[2 * 2], RSF[2 * 2];
        static const float PRE[4] = {0.5f, 1.0f, 1.5f, 2.0f};
        static const int LSI[4] = {0, 3, 1, 2};
        static const int RSI[4] = {2, 1, 3, 0};
        const uint32_t g = group_width(GROUP_SIZE_32);
        float out[4], expct[4];

        for (m = 0; m < 4; m++) {
            LS[m] = UE8M0_TAB[LSI[m]].pat;
            LSF[m] = UE8M0_TAB[LSI[m]].val;
            RS[m] = UE8M0_TAB[RSI[m]].pat;
            RSF[m] = UE8M0_TAB[RSI[m]].val;
        }
        for (m = 0; m < 2; m++)
            for (k = 0; k < 64; k++) {
                const pat_val_t *e = &FP8_TAB[(k * 4 + m * 3 + 1) % FP8_N];
                A[m * 64 + k] = e->pat;
                AV[m * 64 + k] = e->val;
            }
        for (n = 0; n < 2; n++)
            for (k = 0; k < 64; k++) {
                const pat_val_t *e = &FP8_TAB[(k * 6 + n * 5) % FP8_N];
                B[n * 64 + k] = e->pat;
                BV[n * 64 + k] = e->val;
            }
        for (m = 0; m < 2; m++)
            for (n = 0; n < 2; n++) {
                /* block-scaling accounting (spec); UE8M0 exact */
                double e = (double)PRE[m * 2 + n];
                uint32_t b, kk;
                for (b = 0; b < 64 / g; b++) {
                    double blk = 0.0;
                    for (kk = b * g; kk < (b + 1) * g; kk++)
                        blk += (double)AV[m * 64 + kk] * (double)BV[n * 64 + kk];
                    e += (double)LSF[m * 2 + b] * (double)RSF[n * 2 + b] * blk;
                }
                expct[m * 2 + n] = (float)e;
            }
        run_mm_bq(ctx, top_mm_matmul_bq_ntn, 1, 2, 64, 2, TN_DTYPE_FP8_E4M3, TN_DTYPE_UE8M0,
                  TN_DTYPE_UE8M0, A, sizeof(A), B, sizeof(B), LS, RS, NULL, NULL, GROUP_SIZE_32,
                  ACC_MODE_ACCUMULATE, PRE, TN_DTYPE_FP32, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "bq ntn FP8 ACCUMULATE (prefill, 2-group)");
    }

    /* ---- ntn mxfp4: FP4 + UE8M0, K=64 (two groups per row); FP4 operands
     * are packed (element 0 in the low nibble, see tensor.cpp bit_addr) ---- */
    {
        uint8_t AP[2 * 64 / 2], BP[2 * 64 / 2], LS[2 * 2], RS[2 * 2];
        float LSF[2 * 2], RSF[2 * 2];
        static const int LSI[4] = {0, 2, 1, 3};
        static const int RSI[4] = {3, 0, 2, 1};
        const uint32_t g = group_width(GROUP_SIZE_32);
        float out[4], expct[4];

        for (m = 0; m < 4; m++) {
            LS[m] = UE8M0_TAB[LSI[m]].pat;
            LSF[m] = UE8M0_TAB[LSI[m]].val;
            RS[m] = UE8M0_TAB[RSI[m]].pat;
            RSF[m] = UE8M0_TAB[RSI[m]].val;
        }
        for (m = 0; m < 2; m++)
            for (k = 0; k < 64; k++) {
                const pat_val_t *e = &FP4_TAB[(k * 5 + m * 3) % FP4_N];
                if ((m * 64 + k) % 2 == 0)
                    AP[(m * 64 + k) / 2] = e->pat; /* low nibble */
                else
                    AP[(m * 64 + k) / 2] |= (uint8_t)(e->pat << 4);
            }
        for (n = 0; n < 2; n++)
            for (k = 0; k < 64; k++) {
                const pat_val_t *e = &FP4_TAB[(k * 3 + n * 7 + 1) % FP4_N];
                if ((n * 64 + k) % 2 == 0)
                    BP[(n * 64 + k) / 2] = e->pat;
                else
                    BP[(n * 64 + k) / 2] |= (uint8_t)(e->pat << 4);
            }
        for (m = 0; m < 2; m++)
            for (n = 0; n < 2; n++) {
                /* block-scaling accounting (spec); UE8M0 exact */
                double e = 0.0;
                uint32_t b, kk;
                for (b = 0; b < 64 / g; b++) {
                    double blk = 0.0;
                    for (kk = b * g; kk < (b + 1) * g; kk++) {
                        const pat_val_t *ae = &FP4_TAB[(kk * 5 + m * 3) % FP4_N];
                        const pat_val_t *be = &FP4_TAB[(kk * 3 + n * 7 + 1) % FP4_N];
                        blk += (double)ae->val * (double)be->val;
                    }
                    e += (double)LSF[m * 2 + b] * (double)RSF[n * 2 + b] * blk;
                }
                expct[m * 2 + n] = (float)e;
            }
        run_mm_bq(ctx, top_mm_matmul_bq_ntn, 1, 2, 64, 2, TN_DTYPE_FP4, TN_DTYPE_UE8M0,
                  TN_DTYPE_UE8M0, AP, sizeof(AP), BP, sizeof(BP), LS, RS, NULL, NULL, GROUP_SIZE_32,
                  ACC_MODE_OVERWRITE, NULL, TN_DTYPE_FP32, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "bq ntn mxfp4 (FP4 + UE8M0, 2-group)");
    }

    /* ---- ntn mxfp4 with block-max elements: A is all +-6 (0x7/0xF), the
     * magnitudes the pre-fix codec decoded as NaN. Pins that the top FP4
     * slot survives decode and propagates through the reduction ---- */
    {
        uint8_t AP[2 * 32 / 2], BP[2 * 32 / 2], LS[2], RS[2];
        float BV[2 * 32], LSF[2], RSF[2];
        float out[4], expct[4];
        for (m = 0; m < 2; m++)
            for (k = 0; k < 32; k++) {
                const uint8_t p = ((k + m) % 2) ? 0xF : 0x7; /* -6 / +6 */
                if ((m * 32 + k) % 2 == 0)
                    AP[(m * 32 + k) / 2] = p; /* low nibble */
                else
                    AP[(m * 32 + k) / 2] |= (uint8_t)(p << 4);
            }
        for (n = 0; n < 2; n++)
            for (k = 0; k < 32; k++) {
                const pat_val_t *e = &FP4_TAB[(k * 3 + n * 7 + 1) % FP4_N];
                BV[n * 32 + k] = e->val;
                if ((n * 32 + k) % 2 == 0)
                    BP[(n * 32 + k) / 2] = e->pat;
                else
                    BP[(n * 32 + k) / 2] |= (uint8_t)(e->pat << 4);
            }
        for (m = 0; m < 2; m++) {
            const pat_val_t *e = &UE8M0_TAB[(m + 2) % 4];
            LS[m] = e->pat;
            LSF[m] = e->val;
        }
        for (n = 0; n < 2; n++) {
            const pat_val_t *e = &UE8M0_TAB[(n + 1) % 4];
            RS[n] = e->pat;
            RSF[n] = e->val;
        }
        for (m = 0; m < 2; m++)
            for (n = 0; n < 2; n++) {
                /* block-scaling accounting (spec); single group, UE8M0 exact */
                double blk = 0.0;
                uint32_t kk;
                for (kk = 0; kk < 32; kk++) {
                    const double av = ((kk + m) % 2) ? -6.0 : 6.0;
                    const pat_val_t *be = &FP4_TAB[(kk * 3 + n * 7 + 1) % FP4_N];
                    blk += av * (double)be->val;
                }
                expct[m * 2 + n] = (float)((double)LSF[m] * (double)RSF[n] * blk);
            }
        run_mm_bq(ctx, top_mm_matmul_bq_ntn, 1, 2, 32, 2, TN_DTYPE_FP4, TN_DTYPE_UE8M0,
                  TN_DTYPE_UE8M0, AP, sizeof(AP), BP, sizeof(BP), LS, RS, NULL, NULL, GROUP_SIZE_32,
                  ACC_MODE_OVERWRITE, NULL, TN_DTYPE_FP32, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "bq ntn mxfp4 block-max +-6");
    }

    /* ---- ntt nxfp4: FP4 + UE5M3, K=64, GROUP_SIZE_16 (nvfp4 pairing):
     * four groups; UE5M3 decode covers the 1.125 / 2.25 mantissa scales
     * ---- */
    {
        uint8_t AP[2 * 64 / 2], BP[2 * 64 / 2], LS[2 * 4], RS[2 * 4];
        float LSF[2 * 4], RSF[2 * 4];
        /* ls rows: m0 {1, 2, 2.25, 4}, m1 {2, 2.25, 4, 8};
         * rs rows: n0 {1.125, 2, 4, 8}, n1 {1, 1.125, 2, 2.25}
         * (UE5M3_TAB order: 1, 1.125, 2, 2.25, 4, 8) */
        static const int LSI[8] = {0, 2, 3, 4, 2, 3, 4, 5};
        static const int RSI[8] = {1, 2, 4, 5, 0, 1, 2, 3};
        const uint32_t g = group_width(GROUP_SIZE_16);
        float out[4], expct[4];

        for (m = 0; m < 8; m++) {
            LS[m] = UE5M3_TAB[LSI[m]].pat;
            LSF[m] = UE5M3_TAB[LSI[m]].val;
            RS[m] = UE5M3_TAB[RSI[m]].pat;
            RSF[m] = UE5M3_TAB[RSI[m]].val;
        }
        for (m = 0; m < 2; m++)
            for (k = 0; k < 64; k++) {
                const pat_val_t *e = &FP4_TAB[(k * 7 + m * 2 + 1) % FP4_N];
                if ((m * 64 + k) % 2 == 0)
                    AP[(m * 64 + k) / 2] = e->pat;
                else
                    AP[(m * 64 + k) / 2] |= (uint8_t)(e->pat << 4);
            }
        for (n = 0; n < 2; n++)
            for (k = 0; k < 64; k++) {
                const pat_val_t *e = &FP4_TAB[(k * 4 + n * 3 + 2) % FP4_N];
                if ((n * 64 + k) % 2 == 0)
                    BP[(n * 64 + k) / 2] = e->pat;
                else
                    BP[(n * 64 + k) / 2] |= (uint8_t)(e->pat << 4);
            }
        for (n = 0; n < 2; n++)
            for (m = 0; m < 2; m++) {
                /* block-scaling accounting (spec): per-group dot product,
                 * then scale = ls*rs rounded in the UE5M3 domain (e5m3
                 * multiply, RNE) before widening */
                double e = 0.0;
                uint32_t b, kk;
                for (b = 0; b < 64 / g; b++) {
                    double blk = 0.0;
                    for (kk = b * g; kk < (b + 1) * g; kk++) {
                        const pat_val_t *ae = &FP4_TAB[(kk * 7 + m * 2 + 1) % FP4_N];
                        const pat_val_t *be = &FP4_TAB[(kk * 4 + n * 3 + 2) % FP4_N];
                        blk += (double)ae->val * (double)be->val;
                    }
                    const double sc = tnkr_decode_fp(
                        tnkr_encode_fp((double)LSF[m * 4 + b] * (double)RSF[n * 4 + b],
                                       TN_DTYPE_UE5M3),
                        TN_DTYPE_UE5M3);
                    e += sc * blk;
                }
                expct[n * 2 + m] = (float)e; /* ntt: output (1,N,1,M) */
            }
        run_mm_bq(ctx, top_mm_matmul_bq_ntt, 2, 2, 64, 2, TN_DTYPE_FP4, TN_DTYPE_UE5M3,
                  TN_DTYPE_UE5M3, AP, sizeof(AP), BP, sizeof(BP), LS, RS, NULL, NULL, GROUP_SIZE_16,
                  ACC_MODE_OVERWRITE, NULL, TN_DTYPE_FP32, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "bq ntt nxfp4 (FP4 + UE5M3, g16)");
    }

    /* ---- ntn nxfp4 (FP4 + UE5M3), K=48, GROUP_SIZE_16 (nvfp4 pairing):
     * K is illegal under GROUP_SIZE_32 (see the KMOD negative below) yet
     * runs here — proof g_size reaches both the divisibility check and
     * the k/16 scale indexing; per-group scales differ, so the indexing
     * is observable ---- */
    {
        const uint32_t g = group_width(GROUP_SIZE_16);
        uint8_t AP[2 * 48 / 2], BP[2 * 48 / 2], LS[2 * 3], RS[2 * 3];
        float LSF[2 * 3], RSF[2 * 3];
        /* ls rows: {1, 2.25, 8} / {1.125, 2, 4};
         * rs rows: {2, 4, 1} / {2.25, 8, 1.125}
         * (UE5M3_TAB order: 1, 1.125, 2, 2.25, 4, 8) */
        static const int LSI[6] = {0, 3, 5, 1, 2, 4};
        static const int RSI[6] = {2, 4, 0, 3, 5, 1};
        float out[4], expct[4];
        for (m = 0; m < 6; m++) {
            LS[m] = UE5M3_TAB[LSI[m]].pat;
            LSF[m] = UE5M3_TAB[LSI[m]].val;
            RS[m] = UE5M3_TAB[RSI[m]].pat;
            RSF[m] = UE5M3_TAB[RSI[m]].val;
        }
        for (m = 0; m < 2; m++)
            for (k = 0; k < 48; k++) {
                const pat_val_t *e = &FP4_TAB[(k * 5 + m * 3 + 2) % FP4_N];
                if ((m * 48 + k) % 2 == 0)
                    AP[(m * 48 + k) / 2] = e->pat; /* low nibble */
                else
                    AP[(m * 48 + k) / 2] |= (uint8_t)(e->pat << 4);
            }
        for (n = 0; n < 2; n++)
            for (k = 0; k < 48; k++) {
                const pat_val_t *e = &FP4_TAB[(k * 3 + n * 4 + 1) % FP4_N];
                if ((n * 48 + k) % 2 == 0)
                    BP[(n * 48 + k) / 2] = e->pat;
                else
                    BP[(n * 48 + k) / 2] |= (uint8_t)(e->pat << 4);
            }
        for (m = 0; m < 2; m++)
            for (n = 0; n < 2; n++) {
                /* block-scaling accounting (spec); scale product rounded
                 * in the UE5M3 domain (e5m3 multiply, RNE) */
                double e = 0.0;
                uint32_t b, kk;
                for (b = 0; b < 48 / g; b++) {
                    double blk = 0.0;
                    for (kk = b * g; kk < (b + 1) * g; kk++) {
                        const pat_val_t *ae = &FP4_TAB[(kk * 5 + m * 3 + 2) % FP4_N];
                        const pat_val_t *be = &FP4_TAB[(kk * 3 + n * 4 + 1) % FP4_N];
                        blk += (double)ae->val * (double)be->val;
                    }
                    const double sc = tnkr_decode_fp(
                        tnkr_encode_fp((double)LSF[m * 3 + b] * (double)RSF[n * 3 + b],
                                       TN_DTYPE_UE5M3),
                        TN_DTYPE_UE5M3);
                    e += sc * blk;
                }
                expct[m * 2 + n] = (float)e;
            }
        run_mm_bq(ctx, top_mm_matmul_bq_ntn, 1, 2, 48, 2, TN_DTYPE_FP4, TN_DTYPE_UE5M3,
                  TN_DTYPE_UE5M3, AP, sizeof(AP), BP, sizeof(BP), LS, RS, NULL, NULL, GROUP_SIZE_16,
                  ACC_MODE_OVERWRITE, NULL, TN_DTYPE_FP32, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "bq ntn nxfp4 GROUP_SIZE_16 (K=48)");
    }

    /* ---- ntn FP8 + UE8M0 -> BF16 output: the FP32 accumulation is cast
     * at write-out; data stays BF16-exact (products <= 6 in magnitude,
     * quarter quantum, K=32 sums < 32 -> 8 significand bits) so the
     * test-side bf16_of image of the float expectation is the engine's
     * RNE result ---- */
    {
        uint8_t A[2 * 32], B[3 * 32], LS[2], RS[3];
        float AV[2 * 32], BV[3 * 32], LSF[2], RSF[3];
        uint16_t out16[6], exp16[6];
        float e;
        for (m = 0; m < 2; m++)
            for (k = 0; k < 32; k++) {
                const pat_val_t *p = &FP8_TAB[(k * 3 + m * 5) % FP8_N];
                A[m * 32 + k] = p->pat;
                AV[m * 32 + k] = p->val;
            }
        for (n = 0; n < 3; n++)
            for (k = 0; k < 32; k++) {
                const pat_val_t *p = &FP8_TAB[(k * 4 + n * 3 + 1) % FP8_N];
                B[n * 32 + k] = p->pat;
                BV[n * 32 + k] = p->val;
            }
        for (m = 0; m < 2; m++) {
            const pat_val_t *p = &UE8M0_TAB[(m + 1) % 4];
            LS[m] = p->pat;
            LSF[m] = p->val;
        }
        for (n = 0; n < 3; n++) {
            const pat_val_t *p = &UE8M0_TAB[(n + 2) % 4];
            RS[n] = p->pat;
            RSF[n] = p->val;
        }
        for (m = 0; m < 2; m++)
            for (n = 0; n < 3; n++) {
                /* block-scaling accounting (spec); single group, UE8M0 exact */
                double blk = 0.0;
                uint32_t kk;
                for (kk = 0; kk < 32; kk++)
                    blk += (double)AV[m * 32 + kk] * (double)BV[n * 32 + kk];
                e = (float)((double)LSF[m] * (double)RSF[n] * blk);
                exp16[m * 3 + n] = bf16_of(e);
            }
        run_mm_bq(ctx, top_mm_matmul_bq_ntn, 1, 2, 32, 3, TN_DTYPE_FP8_E4M3, TN_DTYPE_UE8M0,
                  TN_DTYPE_UE8M0, A, sizeof(A), B, sizeof(B), LS, RS, NULL, NULL, GROUP_SIZE_32,
                  ACC_MODE_OVERWRITE, NULL, TN_DTYPE_BF16, out16);
        CHECK(memcmp(out16, exp16, sizeof(exp16)) == 0, "bq ntn FP8 -> BF16 out (cast)");
    }

    /* ---- negatives ---- */
    check_mm_bq_reject(ctx, "bq FP8 + UE5M3 scale -> violation", BJ_SCALEDT);
    check_mm_bq_reject(ctx, "bq K % group != 0 -> violation", BJ_KMOD);
    check_mm_bq_reject(ctx, "bq ACCUMULATE + PER_CHANNEL bias -> violation", BJ_ACCBIAS);
    check_mm_bq_reject(ctx, "bq out S8 -> violation", BJ_OUTDT);
    check_mm_bq_reject(ctx, "bq ACCUMULATE + BF16 out -> violation", BJ_ACCBF16);
    check_mm_bq_reject(ctx, "bq bias mode 2 (reserved) -> violation", BJ_BIASMODE);
    check_mm_bq_reject(ctx, "bq acc mode 2 (reserved) -> violation", BJ_ACCMODE);
}
