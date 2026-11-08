// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/test/test_vec_depthwise.c
 *
 * top/vec depthwise cases: input (N,IC,H,W) FP8/BF16, kernel (1,IC,KH,KW)
 * same dtype, output (N,IC,OH,OW) BF16/FP32, all HWAlign; per-channel
 * bias (1,IC,1,1) BF16/FP32. Covers FP8 -> BF16 and BF16 -> FP32, pad /
 * stride 2 / insert 1 / dilation 2, bias and N=2 stacking, plus violation
 * negatives for every legality rule. Expectations are computed
 * independently on the test side (decode the bit patterns, strict
 * FP32-step chain over the KH*KW taps in kh -> kw order, bias as one
 * further FP32 add step, encode to the output dtype); the
 * "step semantics" case (1e8, 1, -1e8) separates the FP32 chain (0) from
 * the mm-style double-tail golden (1).
 * ========================================================================== */
#include "smoke.h"

#include <stdlib.h>

/* URF bias base inside a lane window (input UA, kernel UB, output UO;
 * bias at the next slot) */
#define UBIAS 768u

/* mechanical BF16 pattern of a float (top 16 bits of the FP32 image;
 * the case values are dyadic, so truncation == RNE) */
static uint16_t bf16_of(float f) {
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return (uint16_t)(u >> 16);
}

/* FP8 E4M3 integer-valued pins (sums stay integer -> BF16-exact) */
static const float FP8I_VALS[6] = {1.0f, 2.0f, 3.0f, 4.0f, -1.0f, -2.0f};
static const uint8_t FP8I_PATS[6] = {0x38, 0x40, 0x44, 0x48, 0xB8, 0xC0};

/* poison every lane window at addr: a rejected call (violation raised
 * before any write) provably leaves the output plane untouched */
static void poison_urf(tnkr_ctx_t *ctx, uint32_t addr, uint32_t bytes, uint8_t pat) {
    uint32_t l, lane = ctx->info.urf_bytes / ctx->info.smc_num;
    for (l = 0; l < ctx->info.smc_num; l++)
        memset(ctx->urf + (uint64_t)l * lane + addr, pat, bytes);
}

/* derived output extents (own floor derivation, insert-expanded space) */
static void dw_ohw(uint32_t H, uint32_t W, const conv_attr_t *attr, uint32_t *OH, uint32_t *OW) {
    const uint32_t h_ins = (H - 1) * (attr->insert + 1) + 1;
    const uint32_t w_ins = (W - 1) * (attr->insert + 1) + 1;
    const uint32_t kheff = (attr->kernel_h - 1) * attr->dilation + 1;
    const uint32_t kweff = (attr->kernel_w - 1) * attr->dilation + 1;
    *OH = (h_ins + attr->pad_t + attr->pad_b - kheff) / attr->stride_h + 1;
    *OW = (w_ins + attr->pad_l + attr->pad_r - kweff) / attr->stride_w + 1;
}

/* Standard depthwise case: everything through the
 * global->URF->op->global loop; the kernel buffer is contiguous
 * (1,IC,KH,KW). With BIAS == NULL the bias argument is an allocated but
 * never-configured slot — BIAS_MODE_NONE must not consult it. */
static void run_vec_dw(tnkr_ctx_t *ctx, uint32_t N, uint32_t IC, uint32_t H, uint32_t W,
                       const conv_attr_t *attr, tn_dtype_t idt, tn_dtype_t odt, tn_dtype_t bdt,
                       const void *A, const void *KER, const void *BIAS, void *OUT) {
    const size_t esz = tn_dtype_size(idt), oesz = tn_dtype_size(odt);
    uint32_t OH, OW;
    size_t out_bytes;
    tdr_t d;
    uint8_t sga, sgk, sgb, sgo, sua, suk, subias, suo;

    dw_ohw(H, W, attr, &OH, &OW);
    out_bytes = (size_t)N * IC * OH * OW * oesz;

    memcpy(ctx->global + GA, A, (size_t)N * IC * H * W * esz);
    memcpy(ctx->global + GB, KER, (size_t)IC * attr->kernel_h * attr->kernel_w * esz);
    if (BIAS)
        memcpy(ctx->global + GB + 4096u, BIAS, (size_t)IC * tn_dtype_size(bdt));
    memset(ctx->global + GO, 0xAA, out_bytes);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, idt, GA, (uint16_t)N, (uint16_t)IC, (uint16_t)H,
                        (uint16_t)W);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, idt, GB, 1, (uint16_t)IC, attr->kernel_h,
                        attr->kernel_w);
    sgk = mk_desc(ctx, &d);
    if (BIAS) {
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, bdt, GB + 4096u, 1, (uint16_t)IC, 1, 1);
        sgb = mk_desc(ctx, &d);
    } else {
        sgb = 0; /* unused: no bias load below */
    }
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, odt, GO, (uint16_t)N, (uint16_t)IC, (uint16_t)OH,
                        (uint16_t)OW);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, idt, UA, (uint16_t)N, (uint16_t)IC, (uint16_t)H,
                     (uint16_t)W, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, idt, UB, 1, (uint16_t)IC, attr->kernel_h, attr->kernel_w,
                     0);
    suk = mk_desc(ctx, &d);
    if (BIAS) {
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, bdt, UBIAS, 1, (uint16_t)IC, 1, 1, 0);
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
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, odt, UO, (uint16_t)N, (uint16_t)IC, (uint16_t)OH,
                     (uint16_t)OW, 0);
    suo = mk_desc(ctx, &d);

    top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
    top_ldst_load(suk, sgk, 0, LDST_MODE_NORMAL);
    if (BIAS)
        top_ldst_load(subias, sgb, 0, LDST_MODE_NORMAL);
    top_vec_depthwise(suo, sua, suk, subias, attr, BIAS ? BIAS_MODE_PER_CHANNEL : BIAS_MODE_NONE);
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

/* Test-side independent expectation over decoded values: strict
 * FP32-step chain (each tap rounds the exact product sum once to float,
 * tap order kh -> kw), bias appended as one further FP32 add step */
static void expect_vec_dw(const float *AV, const float *KV, const float *BV, uint32_t N,
                          uint32_t IC, uint32_t H, uint32_t W, const conv_attr_t *attr,
                          float *EXP) {
    const uint32_t KH = attr->kernel_h, KW = attr->kernel_w;
    uint32_t OH, OW, n, c, oh, ow, kh, kw;

    dw_ohw(H, W, attr, &OH, &OW);
    for (n = 0; n < N; n++)
        for (c = 0; c < IC; c++)
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
                            acc = (float)((double)acc +
                                          (double)AV[((n * IC + c) * H + (uint32_t)ih) * W +
                                                     (uint32_t)iw] *
                                              (double)KV[(c * KH + kh) * KW + kw]);
                        }
                    if (BV)
                        acc = (float)((double)acc + (double)BV[c]);
                    EXP[((n * IC + c) * OH + oh) * OW + ow] = acc;
                }
}

/* Generic positive: decoded-value patterns through the dtype loopback */
static void check_vec_dw(tnkr_ctx_t *ctx, const char *name, uint32_t N, uint32_t IC, uint32_t H,
                         uint32_t W, const conv_attr_t *attr, tn_dtype_t idt, tn_dtype_t odt,
                         int has_bias) {
    const size_t in_n = (size_t)N * IC * H * W;
    const size_t k_n = (size_t)IC * attr->kernel_h * attr->kernel_w;
    uint32_t OH, OW;
    float *AV, *KV, *BV, *expct;
    uint8_t *A, *K, *B, *out;
    size_t i, out_n;

    dw_ohw(H, W, attr, &OH, &OW);
    out_n = (size_t)N * IC * OH * OW;
    AV = malloc(in_n * 4u);
    KV = malloc(k_n * 4u);
    BV = malloc((size_t)IC * 4u);
    expct = malloc(out_n * 4u);
    A = malloc(in_n * tn_dtype_size(idt));
    K = malloc(k_n * tn_dtype_size(idt));
    B = malloc((size_t)IC * 4u);
    out = malloc(out_n * tn_dtype_size(odt));
    if (!AV || !KV || !BV || !expct || !A || !K || !B || !out) {
        CHECK(0, name);
    } else {
        for (i = 0; i < in_n; i++)
            AV[i] = (idt == TN_DTYPE_FP8_E4M3) ? FP8I_VALS[(i * 7) % 6]
                                               : (float)((int)(i * 7) % 9 - 4) * 0.5f;
        for (i = 0; i < k_n; i++)
            KV[i] = (idt == TN_DTYPE_FP8_E4M3) ? FP8I_VALS[(i * 5 + 1) % 6]
                                               : (float)((int)(i * 5) % 7 - 3) * 0.5f;
        for (i = 0; i < (size_t)IC; i++)
            BV[i] = (float)((int)i - 1); /* integer: exact in BF16/FP32 */
        if (idt == TN_DTYPE_FP8_E4M3) {
            for (i = 0; i < in_n; i++)
                A[i] = FP8I_PATS[(i * 7) % 6];
            for (i = 0; i < k_n; i++)
                K[i] = FP8I_PATS[(i * 5 + 1) % 6];
        } else {
            uint16_t *A16 = (uint16_t *)A, *K16 = (uint16_t *)K;
            for (i = 0; i < in_n; i++)
                A16[i] = bf16_of(AV[i]);
            for (i = 0; i < k_n; i++)
                K16[i] = bf16_of(KV[i]);
        }
        {
            uint16_t *B16 = (uint16_t *)B;
            for (i = 0; i < (size_t)IC; i++)
                B16[i] = bf16_of(BV[i]); /* bias BF16 */
        }
        expect_vec_dw(AV, KV, has_bias ? BV : NULL, N, IC, H, W, attr, expct);
        run_vec_dw(ctx, N, IC, H, W, attr, idt, odt, TN_DTYPE_BF16, A, K, has_bias ? B : NULL, out);
        if (odt == TN_DTYPE_FP32) {
            CHECK(memcmp(out, expct, out_n * 4u) == 0, name);
        } else {
            uint16_t *out16 = (uint16_t *)out;
            int ok = 1;
            for (i = 0; i < out_n; i++)
                if (out16[i] != bf16_of(expct[i]))
                    ok = 0;
            CHECK(ok, name);
        }
    }
    free(AV);
    free(KV);
    free(BV);
    free(expct);
    free(A);
    free(K);
    free(B);
    free(out);
}

/* Negative selector for check_vec_dw_reject */
enum {
    RJ_INDT32,   /* input FP32 (outside the FP8/BF16 set) */
    RJ_INDTS8,   /* input S8 (outside the FP8/BF16 set) */
    RJ_KDT,      /* kernel dtype != input dtype */
    RJ_OUTDT,    /* output S8 (BF16/FP32 only) */
    RJ_KDN,      /* kernel dn != 1 (kernel is (1,IC,KH,KW)) */
    RJ_KDC,      /* kernel dc != input dc */
    RJ_KDH,      /* kernel dh != kernel_h */
    RJ_KLYS,     /* kernel WAlign (operands are HWAlign) */
    RJ_BIASDT,   /* bias U8 (BF16/FP32 only) */
    RJ_BIASSH,   /* bias dc != IC (bias is (1,IC,1,1)) */
    RJ_BIASMODE, /* bias_mode outside {NONE, PER_CHANNEL} (reserved) */
    RJ_STRIDE0,  /* attr.stride_w == 0 */
    RJ_PAD4,     /* attr.pad_t == 4 */
    RJ_KSZH,     /* attr.kernel_h == 16 */
    RJ_DIL0,     /* attr.dilation == 0 */
    RJ_INSERT4,  /* attr.insert == 4 */
    RJ_NOWIN     /* no complete window fits the input (H < KH) */
};

/* One-illegality pipeline (N=1 IC=2 H=4 W=4, KH=KW=2, BF16 -> FP32; no
 * data loads needed: the op must reject before reading): valid
 * descriptors except the selected flaw, poisoned output, run, store and
 * require the poison pattern to survive */
static void check_vec_dw_reject(tnkr_ctx_t *ctx, const char *name, int why) {
    const uint32_t IC = 2, H = 4, W = 4, KH = 2, KW = 2, OH = 3, OW = 3;
    const tn_dtype_t adt = (why == RJ_INDT32)   ? TN_DTYPE_FP32
                           : (why == RJ_INDTS8) ? TN_DTYPE_S8
                                                : TN_DTYPE_BF16;
    const tn_dtype_t kdt = (why == RJ_KDT) ? TN_DTYPE_FP8_E4M3 : adt;
    const tn_dtype_t odt = (why == RJ_OUTDT) ? TN_DTYPE_S8 : TN_DTYPE_FP32;
    const tdr_layout_t klys = (why == RJ_KLYS) ? TDR_LAYOUT_WALIGN : TDR_LAYOUT_HWALIGN;
    const uint16_t kdn = (uint16_t)(why == RJ_KDN ? 2 : 1);
    const uint16_t kdc = (uint16_t)(why == RJ_KDC ? IC + 1 : IC);
    const uint16_t kdh = (uint16_t)(why == RJ_KDH ? KH + 1 : (why == RJ_KSZH ? 16 : KH));
    const uint16_t inH = (uint16_t)(why == RJ_NOWIN ? 1 : H);
    const tn_dtype_t bdt = (why == RJ_BIASDT) ? TN_DTYPE_U8 : TN_DTYPE_BF16;
    const uint16_t biasC = (uint16_t)(why == RJ_BIASSH ? IC + 1 : IC);
    const int use_bias = (why == RJ_BIASDT || why == RJ_BIASSH);
    const bias_mode_t bm = (why == RJ_BIASMODE) ? (bias_mode_t)2
                           : use_bias           ? BIAS_MODE_PER_CHANNEL
                                                : BIAS_MODE_NONE;
    const size_t obytes = (size_t)IC * OH * OW * tn_dtype_size(odt);
    const char *vexp;
    conv_attr_t attr = {1, 1, 0, 0, 0, 0, 0, 1, KW, KH};
    uint8_t out[2 * 3 * 3 * 4], poison[sizeof(out)];
    tdr_t d;
    uint8_t sga, sgk, sgb, sgo, sua, suk, subias, suo;

    switch (why) {
    case RJ_INDT32:
    case RJ_INDTS8:
        vexp = "input dtype must be FP8_E4M3 or BF16";
        break;
    case RJ_KDT:
        vexp = "kernel dtype must equal the input dtype";
        break;
    case RJ_OUTDT:
        vexp = "output dtype must be BF16/FP32";
        break;
    case RJ_KDH:
        vexp = "kernel dh/dw must equal kernel_h/kernel_w";
        break;
    case RJ_KDN:
    case RJ_KDC:
        vexp = "operand shapes";
        break;
    case RJ_KLYS:
        vexp = "operands must be HWAlign";
        break;
    case RJ_BIASDT:
        vexp = "bias dtype must be BF16/FP32";
        break;
    case RJ_BIASSH:
        vexp = "bias shape must be (1,IC,1,1)";
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
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, kdt, GB, kdn, kdc, kdh, (uint16_t)KW);
    sgk = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, bdt, GB + 4096u, 1, biasC, 1, 1);
    sgb = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, odt, GO, 1, (uint16_t)IC, (uint16_t)OH,
                        (uint16_t)OW);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, adt, UA, 1, (uint16_t)IC, inH, (uint16_t)W, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(klys, kdt, UB, kdn, kdc, kdh, (uint16_t)KW, 0);
    suk = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, bdt, UBIAS, 1, biasC, 1, 1, 0);
    subias = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, odt, UO, 1, (uint16_t)IC, (uint16_t)OH, (uint16_t)OW, 0);
    suo = mk_desc(ctx, &d);

    TNVI_BEGIN(vexp) { top_vec_depthwise(suo, sua, suk, subias, &attr, bm); }
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

void suite_vec_depthwise(tnkr_ctx_t *ctx) {
    printf("== vec_depthwise ==\n");

    /* ---- FP8 -> BF16, basic 2x2 + BF16 bias (integer sums are
     * BF16-exact, so the test-side bf16 pattern is the RNE result) ---- */
    {
        const conv_attr_t attr = {1, 1, 0, 0, 0,
                                  0, 0, 1, 2, 2}; /* sw sh pt pb pl pr ins dil kw kh */
        check_vec_dw(ctx, "depthwise FP8 -> BF16 basic (2x2) + bias", 1, 2, 4, 4, &attr,
                     TN_DTYPE_FP8_E4M3, TN_DTYPE_BF16, 1);
    }

    /* ---- BF16 -> FP32: basic, pad, stride 2, insert 1, dilation 2 ---- */
    {
        const conv_attr_t attr = {1, 1, 0, 0, 0, 0, 0, 1, 2, 2};
        check_vec_dw(ctx, "depthwise BF16 -> FP32 basic (2x2)", 1, 2, 4, 4, &attr, TN_DTYPE_BF16,
                     TN_DTYPE_FP32, 0);
    }
    {
        const conv_attr_t attr = {1, 1, 1, 1, 1, 1, 0, 1, 2, 2};
        check_vec_dw(ctx, "depthwise BF16 pad=1", 1, 2, 4, 4, &attr, TN_DTYPE_BF16, TN_DTYPE_FP32,
                     0);
    }
    {
        const conv_attr_t attr = {2, 2, 0, 0, 0, 0, 0, 1, 2, 2};
        check_vec_dw(ctx, "depthwise BF16 stride=2", 1, 2, 5, 5, &attr, TN_DTYPE_BF16,
                     TN_DTYPE_FP32, 0);
    }
    {
        const conv_attr_t attr = {1, 1, 0, 0, 0, 0, 1, 1, 2, 2};
        check_vec_dw(ctx, "depthwise BF16 insert=1", 1, 2, 3, 3, &attr, TN_DTYPE_BF16,
                     TN_DTYPE_FP32, 0);
    }
    {
        const conv_attr_t attr = {1, 1, 0, 0, 0, 0, 0, 2, 2, 2};
        check_vec_dw(ctx, "depthwise BF16 dilation=2", 1, 2, 5, 5, &attr, TN_DTYPE_BF16,
                     TN_DTYPE_FP32, 0);
    }

    /* ---- BF16 -> FP32 per-channel bias + N=2 stacking ---- */
    {
        const conv_attr_t attr = {1, 1, 0, 0, 0, 0, 0, 1, 2, 2};
        check_vec_dw(ctx, "depthwise BF16 N=2 + bias", 2, 2, 4, 4, &attr, TN_DTYPE_BF16,
                     TN_DTYPE_FP32, 1);
    }

    /* ---- step semantics: the FP32 chain rounds every tap, so
     * ((1e8 + 1) - 1e8) collapses to 0; the mm-style double-tail golden
     * would produce 1 (1x3 kernel: the chain runs over kw) ---- */
    {
        static const float AV[3] = {1e8f, 1.0f, -1e8f};
        static const float KV[3] = {1.0f, 1.0f, 1.0f};
        const conv_attr_t attr = {1, 1, 0, 0, 0, 0, 0, 1, 3, 1};
        uint16_t A[3], K[3];
        float out[1], expct[1] = {0.0f};
        uint32_t i;
        for (i = 0; i < 3; i++) {
            A[i] = bf16_of(AV[i]);
            K[i] = bf16_of(KV[i]);
        }
        run_vec_dw(ctx, 1, 1, 1, 3, &attr, TN_DTYPE_BF16, TN_DTYPE_FP32, TN_DTYPE_BF16, A, K, NULL,
                   out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0,
              "depthwise strict FP32-step chain (1e8,1,-1e8)");
    }

    /* ---- negatives: violation raised, output keeps its poison pattern ---- */
    check_vec_dw_reject(ctx, "input FP32 (out of set) -> violation", RJ_INDT32);
    check_vec_dw_reject(ctx, "input S8 (out of set) -> violation", RJ_INDTS8);
    check_vec_dw_reject(ctx, "kernel dtype != input dtype -> violation", RJ_KDT);
    check_vec_dw_reject(ctx, "output S8 -> violation", RJ_OUTDT);
    check_vec_dw_reject(ctx, "kernel dn != 1 -> violation", RJ_KDN);
    check_vec_dw_reject(ctx, "kernel dc != IC -> violation", RJ_KDC);
    check_vec_dw_reject(ctx, "kernel dh != kernel_h -> violation", RJ_KDH);
    check_vec_dw_reject(ctx, "kernel WAlign (not HWAlign) -> violation", RJ_KLYS);
    check_vec_dw_reject(ctx, "bias U8 -> violation", RJ_BIASDT);
    check_vec_dw_reject(ctx, "bias dc != IC -> violation", RJ_BIASSH);
    check_vec_dw_reject(ctx, "bias mode 2 (reserved) -> violation", RJ_BIASMODE);
    check_vec_dw_reject(ctx, "stride_w=0 -> violation", RJ_STRIDE0);
    check_vec_dw_reject(ctx, "pad_t=4 -> violation", RJ_PAD4);
    check_vec_dw_reject(ctx, "kernel_h=16 -> violation", RJ_KSZH);
    check_vec_dw_reject(ctx, "dilation=0 -> violation", RJ_DIL0);
    check_vec_dw_reject(ctx, "insert=4 -> violation", RJ_INSERT4);
    check_vec_dw_reject(ctx, "no complete window fits -> violation", RJ_NOWIN);

    /* attr NULL: violation raised before any resolve (slot indices are
     * unconfigured; the op must reject on attr first) */
    {
        uint8_t poison[16], out[16];
        memset(poison, 0xAA, sizeof(poison));
        poison_urf(ctx, UO, sizeof(poison), 0xAA);
        TNVI_BEGIN("attr pointer is NULL") { top_vec_depthwise(0, 1, 2, 3, NULL, BIAS_MODE_NONE); }
        TNVI_END();
        memcpy(out, ctx->urf + UO, sizeof(out));
        CHECK(memcmp(out, poison, sizeof(poison)) == 0, "attr=NULL -> violation");
    }
}
