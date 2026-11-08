// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/test/test_mm_conv.c
 *
 * top/mm conv fp family: FP8/BF16 input x WAlign kernel (1,OC,KH*KW,IC)
 * -> BF16/FP32. Covers the basic reduction, N stacking / pad / stride,
 * dilation + insert holes, FP8 operands, per-channel bias, BF16 output
 * cast, the WAlign per-row placement (address-formula direct
 * assertion), plus violation negatives. Expectations are computed
 * independently on the test side (own loops; values are engineered
 * dyadic halves/small integers so the double accumulator is exact and
 * float/double agree bit-for-bit).
 * ========================================================================== */
#include "smoke.h"

/* mechanical BF16 pattern of a float (top 16 bits of the FP32 image) */
static uint16_t bf16_of(float f) {
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return (uint16_t)(u >> 16);
}

/* URF bias base inside a lane window (input UA=0, kernel UB=256, output
 * UO=512; bias at the next slot) */
#define UBIAS 768u

/* FP8 E4M3 spec-table pins (0x38=1, 0x30=0.5, 0x3C=1.5, 0x40=2, 0x44=3,
 * 0x48=4, 0xB0=-0.5, 0xB8=-1, 0xC0=-2) */
static const float FP8_VALS[9] = {1.0f, 0.5f, 1.5f, 2.0f, 3.0f, 4.0f, -0.5f, -1.0f, -2.0f};
static const uint8_t FP8_PATS[9] = {0x38, 0x30, 0x3C, 0x40, 0x44, 0x48, 0xB0, 0xB8, 0xC0};

/* Standard conv case: everything goes through the global->URF->op->global
 * loop; OH/OW are derived on the test side (own floor derivation) */
static void run_mm_conv(tnkr_ctx_t *ctx, uint32_t N, uint32_t IC, uint32_t H, uint32_t W,
                        uint32_t OC, const conv_attr_t *attr, tn_dtype_t adt, tn_dtype_t odt,
                        int has_bias, tn_dtype_t bdt, const void *A, const void *KER,
                        const void *BIAS, void *OUT) {
    const uint32_t KH = attr->kernel_h, KW = attr->kernel_w;
    const uint32_t h_ins = (H - 1) * (attr->insert + 1) + 1;
    const uint32_t w_ins = (W - 1) * (attr->insert + 1) + 1;
    const uint32_t kheff = (KH - 1) * attr->dilation + 1;
    const uint32_t kweff = (KW - 1) * attr->dilation + 1;
    const uint32_t OH = (h_ins + attr->pad_t + attr->pad_b - kheff) / attr->stride_h + 1;
    const uint32_t OW = (w_ins + attr->pad_l + attr->pad_r - kweff) / attr->stride_w + 1;
    const size_t esz = tn_dtype_size(adt), oesz = tn_dtype_size(odt);
    const size_t out_bytes = (size_t)N * OC * OH * OW * oesz;
    tdr_t d;
    uint8_t sga, sgk, sgb, sgo, sua, suk, suo, subias;

    memcpy(ctx->global + GA, A, (size_t)N * IC * H * W * esz);
    memcpy(ctx->global + GB, KER, (size_t)OC * KH * KW * IC * esz);
    if (has_bias)
        memcpy(ctx->global + GB + 4096u, BIAS, (size_t)OC * tn_dtype_size(bdt));
    memset(ctx->global + GO, 0xAA, out_bytes);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, adt, GA, (uint16_t)N, (uint16_t)IC, (uint16_t)H,
                        (uint16_t)W);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, adt, GB, 1, (uint16_t)OC, (uint16_t)(KH * KW),
                        (uint16_t)IC);
    sgk = mk_desc(ctx, &d);
    if (has_bias) {
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, bdt, GB + 4096u, 1, (uint16_t)OC, 1, 1);
        sgb = mk_desc(ctx, &d);
    } else {
        sgb = 0; /* unused: no bias load below */
    }
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, odt, GO, (uint16_t)N, (uint16_t)OC, (uint16_t)OH,
                        (uint16_t)OW);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, adt, UA, (uint16_t)N, (uint16_t)IC, (uint16_t)H,
                     (uint16_t)W, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_WALIGN, adt, UB, 1, (uint16_t)OC, (uint16_t)(KH * KW), (uint16_t)IC,
                     0);
    suk = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, odt, UO, (uint16_t)N, (uint16_t)OC, (uint16_t)OH,
                     (uint16_t)OW, 0);
    suo = mk_desc(ctx, &d);
    if (has_bias) {
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, bdt, UBIAS, 1, (uint16_t)OC, 1, 1, 0);
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
    top_ldst_load(suk, sgk, 0, LDST_MODE_NORMAL);
    if (has_bias)
        top_ldst_load(subias, sgb, 0, LDST_MODE_NORMAL);
    top_mm_conv_fp(suo, sua, suk, subias, attr, has_bias ? BIAS_MODE_PER_CHANNEL : BIAS_MODE_NONE);
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(OUT, ctx->global + GO, out_bytes);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgk);
    if (has_bias)
        tdr_free(ctx, sgb);
    tdr_free(ctx, sgo);
    tdr_free(ctx, sua);
    tdr_free(ctx, suk);
    tdr_free(ctx, suo);
    tdr_free(ctx, subias);
}

/* Test-side independent golden (own loops, double accumulator): values
 * are dyadic halves / small integers, so every partial sum is exact and
 * the encode on the engine side must reproduce it bit-for-bit */
static void expect_conv(const float *AV, const float *KV, const float *BV, uint32_t N, uint32_t IC,
                        uint32_t H, uint32_t W, uint32_t OC, const conv_attr_t *attr, int has_bias,
                        float *EXP) {
    const uint32_t KH = attr->kernel_h, KW = attr->kernel_w;
    const uint32_t h_ins = (H - 1) * (attr->insert + 1) + 1;
    const uint32_t w_ins = (W - 1) * (attr->insert + 1) + 1;
    const uint32_t kheff = (KH - 1) * attr->dilation + 1;
    const uint32_t kweff = (KW - 1) * attr->dilation + 1;
    const uint32_t OH = (h_ins + attr->pad_t + attr->pad_b - kheff) / attr->stride_h + 1;
    const uint32_t OW = (w_ins + attr->pad_l + attr->pad_r - kweff) / attr->stride_w + 1;
    uint32_t n, oc, oh, ow, kh, kw, ic;

    for (n = 0; n < N; n++)
        for (oc = 0; oc < OC; oc++)
            for (oh = 0; oh < OH; oh++)
                for (ow = 0; ow < OW; ow++) {
                    double acc = 0.0;
                    for (kh = 0; kh < KH; kh++)
                        for (kw = 0; kw < KW; kw++) {
                            int64_t ih_ins = (int64_t)oh * attr->stride_h - attr->pad_t +
                                             (int64_t)kh * attr->dilation;
                            int64_t iw_ins = (int64_t)ow * attr->stride_w - attr->pad_l +
                                             (int64_t)kw * attr->dilation;
                            int64_t ih, iw;
                            if (ih_ins % (attr->insert + 1) != 0 ||
                                iw_ins % (attr->insert + 1) != 0)
                                continue; /* insert hole */
                            ih = ih_ins / (attr->insert + 1);
                            iw = iw_ins / (attr->insert + 1);
                            if (ih < 0 || ih >= (int64_t)H || iw < 0 || iw >= (int64_t)W)
                                continue; /* padding */
                            for (ic = 0; ic < IC; ic++)
                                acc += (double)AV[((n * IC + ic) * H + ih) * W + iw] *
                                       (double)KV[((oc * KH + kh) * KW + kw) * IC + ic];
                        }
                    if (has_bias)
                        acc += BV[oc];
                    EXP[((n * OC + oc) * OH + oh) * OW + ow] = (float)acc;
                }
}

/* Negative selector for check_conv_reject */
enum {
    CJ_KLAYOUT, /* kernel descriptor is HWALIGN, not WALIGN */
    CJ_DTYPE,   /* input BF16 with an FP8 kernel */
    CJ_OUTDT,   /* output S8 */
    CJ_KH,      /* kernel.dh != attr kernel_h*kernel_w */
    CJ_OSHAPE,  /* output W extent != derived OW */
    CJ_STRIDE0, /* attr.stride_w == 0 */
    CJ_INSERT,  /* attr.insert == 4 */
    CJ_DIL0,    /* attr.dilation == 0 */
    CJ_BIASMODE /* bias_mode outside {NONE, PER_CHANNEL} (reserved) */
};

/* One-illegality conv pipeline (case-1 skeleton, no data loads needed:
 * the op must reject before reading); the op must raise a violation,
 * poison intact. Poison is per-lane: the output planes live at
 * lane(c) windows, not contiguously behind UO */
static void check_conv_reject(tnkr_ctx_t *ctx, const char *name, int why) {
    const uint32_t IC = 10, H = 6, W = 6, OC = 4, KH = 2, KW = 2, OH = 5, OW = 5;
    const tn_dtype_t adt = TN_DTYPE_BF16;
    const tn_dtype_t kdt = (why == CJ_DTYPE) ? TN_DTYPE_FP8_E4M3 : adt;
    const tn_dtype_t odt = (why == CJ_OUTDT) ? TN_DTYPE_S8 : TN_DTYPE_FP32;
    const uint32_t kdh = (why == CJ_KH) ? 5u : KH * KW;
    const uint32_t ow_d = (why == CJ_OSHAPE) ? OW + 1u : OW;
    const char *vexp;
    switch (why) {
    case CJ_KLAYOUT:
        vexp = "kernel WAlign";
        break;
    case CJ_DTYPE:
        vexp = "input/kernel dtype mismatch";
        break;
    case CJ_OUTDT:
        vexp = "output dtype must be";
        break;
    case CJ_KH:
        vexp = "kernel.dh must equal";
        break;
    case CJ_OSHAPE:
        vexp = "output shape must match";
        break;
    case CJ_STRIDE0:
        vexp = "stride must be 1..15";
        break;
    case CJ_INSERT:
        vexp = "insert must be 0..3";
        break;
    case CJ_BIASMODE:
        vexp = "bias mode must be";
        break;
    default: /* CJ_DIL0 */
        vexp = "dilation must be 1..3";
        break;
    }
    conv_attr_t attr = {1, 1, 0, 0, 0, 0, 0, 1, KW, KH};
    const bias_mode_t bm = (why == CJ_BIASMODE) ? (bias_mode_t)2 : BIAS_MODE_NONE;
    const size_t obytes = (size_t)OC * OH * OW * tn_dtype_size(odt);
    uint8_t out[4 * 5 * 5 * 4], poison[4 * 5 * 5 * 4];
    tdr_t d;
    uint8_t sga, sgk, sgb, sgo, sua, suk, suo, subias;
    uint32_t l;
    const uint64_t lane_bytes = ctx->info.urf_bytes / ctx->info.smc_num;

    if (why == CJ_STRIDE0)
        attr.stride_w = 0;
    if (why == CJ_INSERT)
        attr.insert = 4;
    if (why == CJ_DIL0)
        attr.dilation = 0;

    memset(poison, 0xAA, sizeof(poison));
    for (l = 0; l < OC; l++)
        memset(ctx->urf + (size_t)l * lane_bytes + UO, 0xAA, obytes);
    memset(ctx->global + GO, 0x55, obytes);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, adt, GA, 1, IC, H, W);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, kdt, GB, 1, OC, (uint16_t)kdh, IC);
    sgk = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_BF16, GB + 4096u, 1, OC, 1, 1);
    sgb = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, odt, GO, 1, OC, OH, (uint16_t)ow_d);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, adt, UA, 1, IC, H, W, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(why == CJ_KLAYOUT ? TDR_LAYOUT_HWALIGN : TDR_LAYOUT_WALIGN, kdt, UB, 1, OC,
                     (uint16_t)kdh, IC, 0);
    suk = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, odt, UO, 1, OC, OH, (uint16_t)ow_d, 0);
    suo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_BF16, UBIAS, 1, OC, 1, 1, 0);
    subias = mk_desc(ctx, &d);

    TNVI_BEGIN(vexp) { top_mm_conv_fp(suo, sua, suk, subias, &attr, bm); }
    TNVI_END();
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(out, ctx->global + GO, obytes);
    CHECK(memcmp(out, poison, obytes) == 0, name);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgk);
    tdr_free(ctx, sgb);
    tdr_free(ctx, sgo);
    tdr_free(ctx, sua);
    tdr_free(ctx, suk);
    tdr_free(ctx, suo);
    tdr_free(ctx, subias);
}

/* kernel size > 15 negatives: the geometry is otherwise fully legal
 * (H=W=32 with a matching derived output shape), so the kernel-size
 * limit is the ONLY rejection point — dropping the check makes the op
 * run and break the poison */
static void check_conv_ksz(tnkr_ctx_t *ctx, const char *name, int is_h) {
    const uint32_t IC = 10, H = 32, W = 32, OC = 4;
    const uint32_t KH = is_h ? 16u : 2u, KW = is_h ? 2u : 16u;
    conv_attr_t attr = {1, 1, 0, 0, 0, 0, 0, 1, KW, KH};
    const uint32_t OH = H - KH + 1u, OW = W - KW + 1u;
    const size_t obytes = (size_t)OC * OH * OW * 4u;
    uint8_t out[4 * 17 * 31 * 4], poison[4 * 17 * 31 * 4];
    tdr_t d;
    uint8_t sga, sgk, sgb, sgo, sua, suk, suo, subias;
    uint32_t l;
    const uint64_t lane_bytes = ctx->info.urf_bytes / ctx->info.smc_num;

    memset(poison, 0xAA, sizeof(poison));
    for (l = 0; l < OC; l++)
        memset(ctx->urf + (size_t)l * lane_bytes + UO, 0xAA, obytes);
    memset(ctx->global + GO, 0x55, obytes);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_BF16, GA, 1, IC, H, W);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_BF16, GB, 1, OC, KH * KW, IC);
    sgk = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_BF16, GB + 4096u, 1, OC, 1, 1);
    sgb = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GO, 1, OC, OH, OW);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_BF16, UA, 1, IC, H, W, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_WALIGN, TN_DTYPE_BF16, UB, 1, OC, KH * KW, IC, 0);
    suk = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UO, 1, OC, OH, OW, 0);
    suo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_BF16, UBIAS, 1, OC, 1, 1, 0);
    subias = mk_desc(ctx, &d);

    TNVI_BEGIN("kernel_h/kernel_w must be 1..15") {
        top_mm_conv_fp(suo, sua, suk, subias, &attr, BIAS_MODE_NONE);
    }
    TNVI_END();
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(out, ctx->global + GO, obytes);
    CHECK(memcmp(out, poison, obytes) == 0, name);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgk);
    tdr_free(ctx, sgb);
    tdr_free(ctx, sgo);
    tdr_free(ctx, sua);
    tdr_free(ctx, suk);
    tdr_free(ctx, suo);
    tdr_free(ctx, subias);
}

void suite_mm_conv(tnkr_ctx_t *ctx) {
    printf("== mm_conv ==\n");

    /* ---- BF16 basic: N=1 IC=10 H=6 W=6, OC=4, 2x2, stride 1 ----
     * IC=10 keeps the kernel rows at 20B < urf_width: every numeric case
     * here pins the WAlign row-gap placement through the loopback */
    {
        const uint32_t N = 1, IC = 10, H = 6, W = 6, OC = 4;
        const conv_attr_t attr = {1, 1, 0, 0, 0,
                                  0, 0, 1, 2, 2}; /* sw sh pt pb pl pr ins dil kw kh */
        uint16_t A[360], K[160];
        float AV[360], KV[160], out[100], expct[100];
        uint32_t i;

        for (i = 0; i < 360; i++) {
            AV[i] = (float)((int)(i * 7) % 9 - 4) * 0.5f;
            A[i] = bf16_of(AV[i]);
        }
        for (i = 0; i < 160; i++) {
            KV[i] = (float)((int)(i * 5) % 7 - 3) * 0.5f;
            K[i] = bf16_of(KV[i]);
        }
        expect_conv(AV, KV, NULL, N, IC, H, W, OC, &attr, 0, expct);
        run_mm_conv(ctx, N, IC, H, W, OC, &attr, TN_DTYPE_BF16, TN_DTYPE_FP32, 0, TN_DTYPE_BF16, A,
                    K, NULL, out);
        CHECK(memcmp(out, expct, sizeof(out)) == 0, "conv BF16 -> FP32 basic (IC=10, 2x2)");
    }

    /* ---- N=2 stacking, stride 2, pad 1: pad cells contribute zero ---- */
    {
        const uint32_t N = 2, IC = 10, H = 7, W = 7, OC = 4;
        const conv_attr_t attr = {2, 2, 1, 1, 1, 1, 0, 1, 2, 2};
        uint16_t A[980], K[160];
        float AV[980], KV[160], out[128], expct[128];
        uint32_t i;

        for (i = 0; i < 980; i++) {
            AV[i] = (float)((int)(i * 3) % 11 - 5) * 0.5f;
            A[i] = bf16_of(AV[i]);
        }
        for (i = 0; i < 160; i++) {
            KV[i] = (float)((int)(i * 5) % 9 - 4) * 0.5f;
            K[i] = bf16_of(KV[i]);
        }
        expect_conv(AV, KV, NULL, N, IC, H, W, OC, &attr, 0, expct);
        run_mm_conv(ctx, N, IC, H, W, OC, &attr, TN_DTYPE_BF16, TN_DTYPE_FP32, 0, TN_DTYPE_BF16, A,
                    K, NULL, out);
        CHECK(memcmp(out, expct, sizeof(out)) == 0, "conv BF16 N=2 pad=1 stride=2");
    }

    /* ---- dilation 2 + insert 1: expanded-space coordinates; holes
     * contribute zero ---- */
    {
        const uint32_t N = 1, IC = 10, H = 5, W = 5, OC = 4;
        const conv_attr_t attr = {1, 1, 0, 0, 0, 0, 1, 2, 2, 2};
        uint16_t A[250], K[160];
        float AV[250], KV[160], out[196], expct[196];
        uint32_t i;

        for (i = 0; i < 250; i++) {
            AV[i] = (float)((int)(i * 7) % 9 - 4) * 0.5f;
            A[i] = bf16_of(AV[i]);
        }
        for (i = 0; i < 160; i++) {
            KV[i] = (float)((int)(i * 5) % 7 - 3) * 0.5f;
            K[i] = bf16_of(KV[i]);
        }
        expect_conv(AV, KV, NULL, N, IC, H, W, OC, &attr, 0, expct);
        run_mm_conv(ctx, N, IC, H, W, OC, &attr, TN_DTYPE_BF16, TN_DTYPE_FP32, 0, TN_DTYPE_BF16, A,
                    K, NULL, out);
        CHECK(memcmp(out, expct, sizeof(out)) == 0, "conv BF16 dilation=2 insert=1");
    }

    /* ---- FP8 operands -> FP32 out: spec-table values stay dyadic
     * exact through the reduction ---- */
    {
        const uint32_t N = 1, IC = 10, H = 4, W = 4, OC = 4;
        const conv_attr_t attr = {1, 1, 0, 0, 0, 0, 0, 1, 2, 2};
        uint8_t A[160], K[160];
        float AV[160], KV[160], out[36], expct[36];
        uint32_t i;

        for (i = 0; i < 160; i++) {
            AV[i] = FP8_VALS[(i * 7) % 9];
            A[i] = FP8_PATS[(i * 7) % 9];
        }
        for (i = 0; i < 160; i++) {
            KV[i] = FP8_VALS[(i * 5 + 2) % 9];
            K[i] = FP8_PATS[(i * 5 + 2) % 9];
        }
        expect_conv(AV, KV, NULL, N, IC, H, W, OC, &attr, 0, expct);
        run_mm_conv(ctx, N, IC, H, W, OC, &attr, TN_DTYPE_FP8_E4M3, TN_DTYPE_FP32, 0, TN_DTYPE_BF16,
                    A, K, NULL, out);
        CHECK(memcmp(out, expct, sizeof(out)) == 0, "conv FP8 -> FP32");
    }

    /* ---- per-channel BF16 bias (1,OC,1,1) added after the reduction ---- */
    {
        const uint32_t N = 1, IC = 10, H = 6, W = 6, OC = 4;
        const conv_attr_t attr = {1, 1, 0, 0, 0, 0, 0, 1, 2, 2};
        static const float BV[4] = {0.5f, 1.5f, -1.0f, 2.0f};
        uint16_t A[360], K[160], B16[4];
        float AV[360], KV[160], out[100], expct[100];
        uint32_t i;

        for (i = 0; i < 360; i++) {
            AV[i] = (float)((int)(i * 7) % 9 - 4) * 0.5f;
            A[i] = bf16_of(AV[i]);
        }
        for (i = 0; i < 160; i++) {
            KV[i] = (float)((int)(i * 5) % 7 - 3) * 0.5f;
            K[i] = bf16_of(KV[i]);
        }
        for (i = 0; i < 4; i++)
            B16[i] = bf16_of(BV[i]);
        expect_conv(AV, KV, BV, N, IC, H, W, OC, &attr, 1, expct);
        run_mm_conv(ctx, N, IC, H, W, OC, &attr, TN_DTYPE_BF16, TN_DTYPE_FP32, 1, TN_DTYPE_BF16, A,
                    K, B16, out);
        CHECK(memcmp(out, expct, sizeof(out)) == 0, "conv BF16 -> FP32 per-channel bias");
    }

    /* ---- BF16 output: integer-valued sums (|sum| <= 255) are
     * BF16-exact, so the test-side bf16 pattern is the RNE result ---- */
    {
        const uint32_t N = 1, IC = 10, H = 4, W = 4, OC = 4;
        const conv_attr_t attr = {1, 1, 0, 0, 0, 0, 0, 1, 2, 2};
        uint16_t A[160], K[160], out16[36], exp16[36];
        float AV[160], KV[160], expct[36];
        uint32_t i;

        for (i = 0; i < 160; i++) {
            AV[i] = (float)((int)(i * 7) % 5 - 2); /* {-2..2} integers */
            A[i] = bf16_of(AV[i]);
        }
        for (i = 0; i < 160; i++) {
            KV[i] = (float)((int)(i * 5) % 3 - 1); /* {-1,0,1} */
            K[i] = bf16_of(KV[i]);
        }
        expect_conv(AV, KV, NULL, N, IC, H, W, OC, &attr, 0, expct);
        for (i = 0; i < 36; i++)
            exp16[i] = bf16_of(expct[i]);
        run_mm_conv(ctx, N, IC, H, W, OC, &attr, TN_DTYPE_BF16, TN_DTYPE_BF16, 0, TN_DTYPE_BF16, A,
                    K, NULL, out16);
        CHECK(memcmp(out16, exp16, sizeof(out16)) == 0, "conv BF16 -> BF16 out (cast)");
    }

    /* ---- WAlign placement (address-formula direct assertion): poison
     * the kernel windows, load, then check data bytes and row gaps
     * against the test-side row formula (BF16 IC=10: row 20B -> 32B) ---- */
    {
        uint32_t OC = 4, TAPS = 4, IC = 10;
        const uint32_t lane_bytes = ctx->info.urf_bytes / ctx->info.smc_num;
        uint16_t K[OC * TAPS * IC];
        uint32_t oc, r, e;
        int ok = 1;
        tdr_t d;
        uint8_t sgk, suk;

        for (oc = 0; oc < OC * TAPS * IC; oc++)
            K[oc] = bf16_of((float)((int)(oc * 7) % 9 - 4) * 0.5f);
        memcpy(ctx->global + GB, K, sizeof(K));

        for (oc = 0; oc < OC; oc++)
            memset(ctx->urf + (size_t)oc * lane_bytes + UB, 0xAA, 512);

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_BF16, GB, 1, OC, TAPS, IC);
        sgk = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_WALIGN, TN_DTYPE_BF16, UB, 1, OC, TAPS, IC, 0);
        suk = mk_desc(ctx, &d);
        top_ldst_load(suk, sgk, 0, LDST_MODE_NORMAL);

        for (oc = 0; oc < OC && ok; oc++)
            for (r = 0; r < TAPS && ok; r++) {
                const uint8_t *row = ctx->urf + (size_t)oc * lane_bytes + UB + r * 32u;
                if (memcmp(row, &K[(oc * TAPS + r) * IC], IC * 2u) != 0)
                    ok = 0;
                for (e = IC * 2u; e < 32u; e++)
                    if (row[e] != 0xAA)
                        ok = 0;
            }
        CHECK(ok, "WALIGN load: rows packed at urf_width, gaps keep poison");

        tdr_free(ctx, sgk);
        tdr_free(ctx, suk);
    }

    /* ---- negatives ---- */
    check_conv_reject(ctx, "conv kernel HWALIGN (not WALIGN) -> violation", CJ_KLAYOUT);
    check_conv_reject(ctx, "conv BF16 input + FP8 kernel -> violation", CJ_DTYPE);
    check_conv_reject(ctx, "conv out S8 -> violation", CJ_OUTDT);
    check_conv_reject(ctx, "conv kernel.dh != KH*KW -> violation", CJ_KH);
    check_conv_reject(ctx, "conv out W != derived OW -> violation", CJ_OSHAPE);
    check_conv_reject(ctx, "conv stride_w=0 -> violation", CJ_STRIDE0);
    check_conv_reject(ctx, "conv insert=4 -> violation", CJ_INSERT);
    check_conv_reject(ctx, "conv dilation=0 -> violation", CJ_DIL0);
    check_conv_reject(ctx, "conv bias mode 2 (reserved) -> violation", CJ_BIASMODE);
    check_conv_ksz(ctx, "conv kernel_w=16 -> violation", 0);
    check_conv_ksz(ctx, "conv kernel_h=16 -> violation", 1);

    /* attr NULL: violation raised before any resolve (slot indices are
     * unconfigured; the op must reject on attr first) */
    {
        uint8_t poison[16], out[16];
        memset(poison, 0xAA, sizeof(poison));
        memset(ctx->urf + UO, 0xAA, sizeof(poison));
        TNVI_BEGIN("attr pointer is NULL") { top_mm_conv_fp(0, 1, 2, 3, NULL, BIAS_MODE_NONE); }
        TNVI_END();
        memcpy(out, ctx->urf + UO, sizeof(poison));
        CHECK(memcmp(out, poison, sizeof(poison)) == 0, "conv attr=NULL -> violation");
    }
}
