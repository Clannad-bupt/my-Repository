// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/test/test_vec_conv.c
 *
 * top/vec conv cases: FP32 only, unreorganized HWAlign kernel (IC,OC,KH,KW)
 * (lane oc owns its whole (IC,KH,KW) slice; kernel dh==kernel_h,
 * dw==kernel_w). Covers the basic
 * reduction, pad / stride 2 / insert 1 / dilation 2, per-channel bias and
 * N=2 stacking, plus violation negatives for every legality rule.
 * Expectations are computed independently on the test side with the same
 * strict FP32-step chain (each tap rounds once to float, tap order
 * kh -> kw -> ic); the "step semantics" case (1e8, 1, -1e8) separates the
 * FP32 chain (0) from the mm-style double-tail golden (1), pinning the
 * kernel.h numeric rule.
 * ========================================================================== */
#include "smoke.h"

#include <stdlib.h>

/* URF bias base inside a lane window (input UA, kernel UB, output UO;
 * bias at the next slot) */
#define UBIAS 768u

/* poison every lane window at addr: a rejected call (violation raised
 * before any write) provably leaves the output plane untouched */
static void poison_urf(tnkr_ctx_t *ctx, uint32_t addr, uint32_t bytes, uint8_t pat) {
    uint32_t l, lane = ctx->info.urf_bytes / ctx->info.smc_num;
    for (l = 0; l < ctx->info.smc_num; l++)
        memset(ctx->urf + (uint64_t)l * lane + addr, pat, bytes);
}

/* derived output extents (own floor derivation, insert-expanded space) */
static void vec_conv_ohw(uint32_t H, uint32_t W, const conv_attr_t *attr, uint32_t *OH,
                         uint32_t *OW) {
    const uint32_t h_ins = (H - 1) * (attr->insert + 1) + 1;
    const uint32_t w_ins = (W - 1) * (attr->insert + 1) + 1;
    const uint32_t kheff = (attr->kernel_h - 1) * attr->dilation + 1;
    const uint32_t kweff = (attr->kernel_w - 1) * attr->dilation + 1;
    *OH = (h_ins + attr->pad_t + attr->pad_b - kheff) / attr->stride_h + 1;
    *OW = (w_ins + attr->pad_l + attr->pad_r - kweff) / attr->stride_w + 1;
}

/* Standard vec conv case: everything FP32 through the
 * global->URF->op->global loop; the kernel buffer is contiguous
 * (IC,OC,KH,KW). With BIAS == NULL the bias argument is an allocated but
 * never-configured slot — BIAS_MODE_NONE must not consult it. */
static void run_vec_conv(tnkr_ctx_t *ctx, uint32_t N, uint32_t IC, uint32_t H, uint32_t W,
                         uint32_t OC, const conv_attr_t *attr, const float *A, const float *KER,
                         const float *BIAS, float *OUT) {
    uint32_t OH, OW;
    size_t out_bytes;
    tdr_t d;
    uint8_t sga, sgk, sgb, sgo, sua, suk, subias, suo;

    vec_conv_ohw(H, W, attr, &OH, &OW);
    out_bytes = (size_t)N * OC * OH * OW * 4u;

    memcpy(ctx->global + GA, A, (size_t)N * IC * H * W * 4u);
    memcpy(ctx->global + GB, KER, (size_t)IC * OC * attr->kernel_h * attr->kernel_w * 4u);
    if (BIAS)
        memcpy(ctx->global + GB + 4096u, BIAS, (size_t)OC * 4u);
    memset(ctx->global + GO, 0xAA, out_bytes);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GA, (uint16_t)N, (uint16_t)IC,
                        (uint16_t)H, (uint16_t)W);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GB, (uint16_t)IC, (uint16_t)OC,
                        attr->kernel_h, attr->kernel_w);
    sgk = mk_desc(ctx, &d);
    if (BIAS) {
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GB + 4096u, 1, (uint16_t)OC, 1,
                            1);
        sgb = mk_desc(ctx, &d);
    } else {
        sgb = 0; /* unused: no bias load below */
    }
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GO, (uint16_t)N, (uint16_t)OC,
                        (uint16_t)OH, (uint16_t)OW);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UA, (uint16_t)N, (uint16_t)IC, (uint16_t)H,
                     (uint16_t)W, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UB, (uint16_t)IC, (uint16_t)OC,
                     attr->kernel_h, attr->kernel_w, 0);
    suk = mk_desc(ctx, &d);
    if (BIAS) {
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UBIAS, 1, (uint16_t)OC, 1, 1, 0);
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
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UO, (uint16_t)N, (uint16_t)OC, (uint16_t)OH,
                     (uint16_t)OW, 0);
    suo = mk_desc(ctx, &d);

    top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
    top_ldst_load(suk, sgk, 0, LDST_MODE_NORMAL);
    if (BIAS)
        top_ldst_load(subias, sgb, 0, LDST_MODE_NORMAL);
    top_vec_conv(suo, sua, suk, subias, attr, BIAS ? BIAS_MODE_PER_CHANNEL : BIAS_MODE_NONE);
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(OUT, ctx->global + GO, out_bytes);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgk);
    if (BIAS)
        tdr_free(ctx, sgb);
    tdr_free(ctx, sgo);
    tdr_free(ctx, sua);
    tdr_free(ctx, suk);
    tdr_free(ctx, subias);
    tdr_free(ctx, suo);
}

/* Test-side independent expectation: own loops, strict FP32-step chain
 * (each tap rounds the exact product sum once to float, tap order
 * kh -> kw -> ic), bias appended as one further FP32 add step */
static void expect_vec_conv(const float *AV, const float *KV, const float *BV, uint32_t N,
                            uint32_t IC, uint32_t H, uint32_t W, uint32_t OC,
                            const conv_attr_t *attr, float *EXP) {
    const uint32_t KH = attr->kernel_h, KW = attr->kernel_w;
    uint32_t OH, OW, n, oc, oh, ow, kh, kw, ic;

    vec_conv_ohw(H, W, attr, &OH, &OW);
    for (n = 0; n < N; n++)
        for (oc = 0; oc < OC; oc++)
            for (oh = 0; oh < OH; oh++)
                for (ow = 0; ow < OW; ow++) {
                    float acc = 0.0f;
                    for (kh = 0; kh < KH; kh++)
                        for (kw = 0; kw < KW; kw++) {
                            const int64_t ih_ins = (int64_t)oh * attr->stride_h - attr->pad_t +
                                                   (int64_t)kh * attr->dilation;
                            const int64_t iw_ins = (int64_t)ow * attr->stride_w - attr->pad_l +
                                                   (int64_t)kw * attr->dilation;
                            int64_t ih, iw;
                            if (ih_ins % (attr->insert + 1) != 0 ||
                                iw_ins % (attr->insert + 1) != 0)
                                continue; /* insert hole contributes zero */
                            ih = ih_ins / (attr->insert + 1);
                            iw = iw_ins / (attr->insert + 1);
                            if (ih < 0 || ih >= (int64_t)H || iw < 0 || iw >= (int64_t)W)
                                continue; /* padding contributes zero */
                            for (ic = 0; ic < IC; ic++)
                                acc = (float)((double)acc +
                                              (double)AV[((n * IC + ic) * H + (uint32_t)ih) * W +
                                                         (uint32_t)iw] *
                                                  (double)KV[((ic * OC + oc) * KH + kh) * KW + kw]);
                        }
                    if (BV)
                        acc = (float)((double)acc + (double)BV[oc]);
                    EXP[((n * OC + oc) * OH + oh) * OW + ow] = acc;
                }
}

/* Generic positive: dyadic-half patterns, expectation vs loopback */
static void check_vec_conv(tnkr_ctx_t *ctx, const char *name, uint32_t N, uint32_t IC, uint32_t H,
                           uint32_t W, uint32_t OC, const conv_attr_t *attr, int has_bias) {
    const size_t in_n = (size_t)N * IC * H * W;
    const size_t k_n = (size_t)IC * OC * attr->kernel_h * attr->kernel_w;
    uint32_t OH, OW;
    float *A, *K, *B, *out, *expct;
    size_t i;

    vec_conv_ohw(H, W, attr, &OH, &OW);
    A = malloc(in_n * 4u);
    K = malloc(k_n * 4u);
    B = malloc((size_t)OC * 4u);
    out = malloc((size_t)N * OC * OH * OW * 4u);
    expct = malloc((size_t)N * OC * OH * OW * 4u);
    if (!A || !K || !B || !out || !expct) {
        CHECK(0, name);
    } else {
        for (i = 0; i < in_n; i++)
            A[i] = (float)((int)(i * 7) % 9 - 4) * 0.5f;
        for (i = 0; i < k_n; i++)
            K[i] = (float)((int)(i * 5) % 7 - 3) * 0.5f;
        for (i = 0; i < (size_t)OC; i++)
            B[i] = (float)((int)i - 1) * 0.75f;
        expect_vec_conv(A, K, has_bias ? B : NULL, N, IC, H, W, OC, attr, expct);
        run_vec_conv(ctx, N, IC, H, W, OC, attr, A, K, has_bias ? B : NULL, out);
        CHECK(memcmp(out, expct, (size_t)N * OC * OH * OW * 4u) == 0, name);
    }
    free(A);
    free(K);
    free(B);
    free(out);
    free(expct);
}

/* Negative selector for check_vec_conv_reject */
enum {
    RJ_INDT,     /* input BF16 (FP32 only) */
    RJ_KLYS,     /* kernel WAlign (vec conv kernels are HWAlign) */
    RJ_KDN,      /* kernel dn != input dc (kernel is (IC,OC,KH,KW)) */
    RJ_KDH,      /* kernel dh != kernel_h */
    RJ_OUTN,     /* output dn != input dn */
    RJ_OUTC,     /* output dc != kernel dc */
    RJ_OHW,      /* output dw != derived OW */
    RJ_BIASDT,   /* bias BF16 (FP32 only) */
    RJ_BIASSH,   /* bias dc != OC (bias is (1,OC,1,1)) */
    RJ_BIASMODE, /* bias_mode outside {NONE, PER_CHANNEL} (reserved) */
    RJ_STRIDE0,  /* attr.stride_w == 0 */
    RJ_PAD4,     /* attr.pad_t == 4 */
    RJ_KSZH,     /* attr.kernel_h == 16 */
    RJ_DIL0,     /* attr.dilation == 0 */
    RJ_INSERT4,  /* attr.insert == 4 */
    RJ_NOWIN     /* no complete window fits the input (H < KH) */
};

/* One-illegality pipeline (N=1 IC=2 H=4 W=4, OC=2, KH=KW=2, FP32; no data
 * loads needed: the op must reject before reading): valid descriptors
 * except the selected flaw, poisoned output, run, store and require the
 * poison pattern to survive */
static void check_vec_conv_reject(tnkr_ctx_t *ctx, const char *name, int why) {
    const uint32_t IC = 2, H = 4, W = 4, OC = 2, KH = 2, KW = 2, OH = 3, OW = 3;
    const tn_dtype_t adt = (why == RJ_INDT) ? TN_DTYPE_BF16 : TN_DTYPE_FP32;
    const tdr_layout_t klys = (why == RJ_KLYS) ? TDR_LAYOUT_WALIGN : TDR_LAYOUT_HWALIGN;
    const uint16_t kdn = (uint16_t)(why == RJ_KDN ? IC + 1 : IC);
    const uint16_t kdh = (uint16_t)(why == RJ_KDH ? KH + 1 : (why == RJ_KSZH ? 16 : KH));
    const uint16_t inH = (uint16_t)(why == RJ_NOWIN ? 1 : H);
    const uint16_t outN = (uint16_t)(why == RJ_OUTN ? 2 : 1);
    const uint16_t outC = (uint16_t)(why == RJ_OUTC ? OC + 1 : OC);
    const uint16_t outW = (uint16_t)(why == RJ_OHW ? OW + 1 : OW);
    const tn_dtype_t bdt = (why == RJ_BIASDT) ? TN_DTYPE_BF16 : TN_DTYPE_FP32;
    const uint16_t biasC = (uint16_t)(why == RJ_BIASSH ? OC + 1 : OC);
    const int use_bias = (why == RJ_BIASDT || why == RJ_BIASSH);
    const bias_mode_t bm = (why == RJ_BIASMODE) ? (bias_mode_t)2
                           : use_bias           ? BIAS_MODE_PER_CHANNEL
                                                : BIAS_MODE_NONE;
    const size_t obytes = (size_t)outN * outC * OH * outW * 4u;
    const char *vexp;
    conv_attr_t attr = {1, 1, 0, 0, 0, 0, 0, 1, KW, KH};
    uint8_t out[2 * 3 * 3 * 3 * 4], poison[sizeof(out)];
    tdr_t d;
    uint8_t sga, sgk, sgb, sgo, sua, suk, subias, suo;

    switch (why) {
    case RJ_INDT:
        vexp = "dtypes must be FP32";
        break;
    case RJ_KLYS:
        vexp = "operands must be HWAlign";
        break;
    case RJ_KDH:
        vexp = "kernel dh/dw must equal kernel_h/kernel_w";
        break;
    case RJ_KDN:
    case RJ_OUTN:
    case RJ_OUTC:
        vexp = "operand shapes";
        break;
    case RJ_OHW:
        vexp = "output shape must match";
        break;
    case RJ_BIASDT:
        vexp = "bias dtype must be FP32";
        break;
    case RJ_BIASSH:
        vexp = "bias shape must be (1,OC,1,1)";
        break;
    case RJ_BIASMODE:
        vexp = "bias mode must be";
        break;
    case RJ_STRIDE0:
        vexp = "stride must be 1..15";
        break;
    case RJ_PAD4:
        vexp = "pad must be 0..3";
        break;
    case RJ_KSZH:
        vexp = "kernel_h/kernel_w must be 1..15";
        break;
    case RJ_DIL0:
        vexp = "dilation must be 1..3";
        break;
    case RJ_INSERT4:
        vexp = "insert must be 0..3";
        break;
    default: /* RJ_NOWIN */
        vexp = "no complete window fits";
        break;
    }
    if (why == RJ_STRIDE0)
        attr.stride_w = 0;
    if (why == RJ_PAD4)
        attr.pad_t = 4;
    if (why == RJ_KSZH)
        attr.kernel_h = 16;
    if (why == RJ_DIL0)
        attr.dilation = 0;
    if (why == RJ_INSERT4)
        attr.insert = 4;

    memset(poison, 0xAA, sizeof(poison));
    poison_urf(ctx, UO, 512, 0xAA);
    memset(ctx->global + GO, 0x55, obytes);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, adt, GA, 1, (uint16_t)IC, inH, (uint16_t)W);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GB, kdn, (uint16_t)OC, kdh,
                        (uint16_t)KW);
    sgk = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, bdt, GB + 4096u, 1, biasC, 1, 1);
    sgb = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GO, outN, outC, (uint16_t)OH, outW);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, adt, UA, 1, (uint16_t)IC, inH, (uint16_t)W, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(klys, TN_DTYPE_FP32, UB, kdn, (uint16_t)OC, kdh, (uint16_t)KW, 0);
    suk = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, bdt, UBIAS, 1, biasC, 1, 1, 0);
    subias = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UO, outN, outC, (uint16_t)OH, outW, 0);
    suo = mk_desc(ctx, &d);

    TNVI_BEGIN(vexp) { top_vec_conv(suo, sua, suk, subias, &attr, bm); }
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
    tdr_free(ctx, subias);
    tdr_free(ctx, suo);
}

void suite_vec_conv(tnkr_ctx_t *ctx) {
    printf("== vec_conv ==\n");

    /* ---- basic: N=1 IC=2 H=4 W=4, OC=2, 2x2 stride 1 ---- */
    {
        const conv_attr_t attr = {1, 1, 0, 0, 0,
                                  0, 0, 1, 2, 2}; /* sw sh pt pb pl pr ins dil kw kh */
        check_vec_conv(ctx, "conv FP32 basic (2x2)", 1, 2, 4, 4, 2, &attr, 0);
    }

    /* ---- pad 1: padding cells contribute zero ---- */
    {
        const conv_attr_t attr = {1, 1, 1, 1, 1, 1, 0, 1, 2, 2};
        check_vec_conv(ctx, "conv FP32 pad=1", 1, 2, 4, 4, 2, &attr, 0);
    }

    /* ---- stride 2 ---- */
    {
        const conv_attr_t attr = {2, 2, 0, 0, 0, 0, 0, 1, 2, 2};
        check_vec_conv(ctx, "conv FP32 stride=2", 1, 2, 5, 5, 2, &attr, 0);
    }

    /* ---- insert 1: expanded-space coordinates, holes contribute zero ---- */
    {
        const conv_attr_t attr = {1, 1, 0, 0, 0, 0, 1, 1, 2, 2};
        check_vec_conv(ctx, "conv FP32 insert=1", 1, 2, 3, 3, 2, &attr, 0);
    }

    /* ---- dilation 2 ---- */
    {
        const conv_attr_t attr = {1, 1, 0, 0, 0, 0, 0, 2, 2, 2};
        check_vec_conv(ctx, "conv FP32 dilation=2", 1, 2, 5, 5, 2, &attr, 0);
    }

    /* ---- per-channel bias (1,OC,1,1) appended as one FP32 add step ---- */
    {
        const conv_attr_t attr = {1, 1, 0, 0, 0, 0, 0, 1, 2, 2};
        check_vec_conv(ctx, "conv FP32 per-channel bias", 1, 2, 4, 4, 2, &attr, 1);
    }

    /* ---- N=2 stacking ---- */
    {
        const conv_attr_t attr = {1, 1, 0, 0, 0, 0, 0, 1, 2, 2};
        check_vec_conv(ctx, "conv FP32 N=2 + bias", 2, 2, 4, 4, 2, &attr, 1);
    }

    /* ---- step semantics: the FP32 chain rounds every tap, so
     * ((1e8 + 1) - 1e8) collapses to 0; the mm-style double-tail golden
     * would produce 1 ---- */
    {
        static const float A3[3] = {1e8f, 1.0f, -1e8f};
        static const float K3[3] = {1.0f, 1.0f, 1.0f};
        const conv_attr_t attr = {1, 1, 0, 0, 0, 0, 0, 1, 1, 1};
        float out[1], expct[1] = {0.0f};
        run_vec_conv(ctx, 1, 3, 1, 1, 1, &attr, A3, K3, NULL, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "conv strict FP32-step chain (1e8,1,-1e8)");
    }

    /* ---- negatives: violation raised, output keeps its poison pattern ---- */
    check_vec_conv_reject(ctx, "input BF16 -> violation", RJ_INDT);
    check_vec_conv_reject(ctx, "kernel WAlign (not HWAlign) -> violation", RJ_KLYS);
    check_vec_conv_reject(ctx, "kernel dn != IC -> violation", RJ_KDN);
    check_vec_conv_reject(ctx, "kernel dh != kernel_h -> violation", RJ_KDH);
    check_vec_conv_reject(ctx, "output N != input N -> violation", RJ_OUTN);
    check_vec_conv_reject(ctx, "output C != kernel OC -> violation", RJ_OUTC);
    check_vec_conv_reject(ctx, "output W != derived OW -> violation", RJ_OHW);
    check_vec_conv_reject(ctx, "bias BF16 -> violation", RJ_BIASDT);
    check_vec_conv_reject(ctx, "bias dc != OC -> violation", RJ_BIASSH);
    check_vec_conv_reject(ctx, "bias mode 2 (reserved) -> violation", RJ_BIASMODE);
    check_vec_conv_reject(ctx, "stride_w=0 -> violation", RJ_STRIDE0);
    check_vec_conv_reject(ctx, "pad_t=4 -> violation", RJ_PAD4);
    check_vec_conv_reject(ctx, "kernel_h=16 -> violation", RJ_KSZH);
    check_vec_conv_reject(ctx, "dilation=0 -> violation", RJ_DIL0);
    check_vec_conv_reject(ctx, "insert=4 -> violation", RJ_INSERT4);
    check_vec_conv_reject(ctx, "no complete window fits -> violation", RJ_NOWIN);

    /* attr NULL: violation raised before any resolve (slot indices are
     * unconfigured; the op must reject on attr first) */
    {
        uint8_t poison[16], out[16];
        memset(poison, 0xAA, sizeof(poison));
        poison_urf(ctx, UO, sizeof(poison), 0xAA);
        TNVI_BEGIN("attr pointer is NULL") { top_vec_conv(0, 1, 2, 3, NULL, BIAS_MODE_NONE); }
        TNVI_END();
        memcpy(out, ctx->urf + UO, sizeof(out));
        CHECK(memcmp(out, poison, sizeof(poison)) == 0, "attr=NULL -> violation");
    }
}
