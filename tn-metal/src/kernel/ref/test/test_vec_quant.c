// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/test/test_vec_quant.c
 *
 * top/vec cast & quantization cases: fp8/mxfp4/nxfp4 quant (block scale
 * derivation: OCP MX floor exponent for UE8M0, RNE(amax/6) for UE5M3;
 * zero group -> scale 0; elements saturate + RNE), fp8 dequant and int8
 * dequant (W = scale*q - min). Multi-row (M=2) multi-group cases give
 * every group a distinct scale so a broken (mi, gi) scale index cannot
 * hide. Quant expectations are hand-computed from the format specs
 * (scales picked as exact powers of two / explicit patterns; FP4 packed
 * low-nibble-first), no codec reuse. Plus violation negatives for
 * every legality rule.
 * ========================================================================== */
#include "smoke.h"

typedef void (*quant_fn)(tdr_idx_t, tdr_idx_t, tdr_idx_t, group_size_t);

/* Standard quant case: the FP32 image at GA is quantized; the quantized
 * bytes land in OUT, the scale bytes in SOUT, both through the ldst loop */
static void run_quant(tnkr_ctx_t *ctx, quant_fn fn, uint16_t M, uint16_t N, group_size_t gs,
                      tn_dtype_t qdt, tn_dtype_t sdt, const float *IN, void *OUT, void *SOUT) {
    const uint32_t g = (gs == GROUP_SIZE_32) ? 32u : 16u;
    const size_t qsz = ((size_t)M * N * tn_dtype_bits(qdt) + 7u) / 8u;
    const size_t ssz = (size_t)M * (N / g);
    tdr_t d;
    uint8_t sga, sgq, sgs, sua, suq, sus;

    memcpy(ctx->global + GA, IN, (size_t)M * N * 4);
    memset(ctx->global + GO, 0xAA, qsz);
    memset(ctx->global + GO + 4096u, 0xAA, ssz);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GA, 1, M, 1, N);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, qdt, GO, 1, M, 1, N);
    sgq = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, sdt, GO + 4096u, 1, M, 1, (uint16_t)(N / g));
    sgs = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UA, 1, M, 1, N, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, qdt, UO, 1, M, 1, N, 0);
    suq = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, sdt, UB, 1, M, 1, (uint16_t)(N / g), 0);
    sus = mk_desc(ctx, &d);

    top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
    fn(suq, sus, sua, gs);
    top_ldst_store(sgq, 0, suq, LDST_MODE_NORMAL);
    top_ldst_store(sgs, 0, sus, LDST_MODE_NORMAL);
    memcpy(OUT, ctx->global + GO, qsz);
    memcpy(SOUT, ctx->global + GO + 4096u, ssz);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgq);
    tdr_free(ctx, sgs);
    tdr_free(ctx, sua);
    tdr_free(ctx, suq);
    tdr_free(ctx, sus);
}

/* Standard fp8 dequant case: Q + S enter, the float image lands in OUT */
static void run_dequant_fp8(tnkr_ctx_t *ctx, uint16_t M, uint16_t N, group_size_t gs,
                            const uint8_t *Q, const uint8_t *S, tn_dtype_t odt, void *OUT) {
    const uint32_t g = (gs == GROUP_SIZE_32) ? 32u : 16u;
    const size_t osz = (size_t)M * N * tn_dtype_size(odt);
    tdr_t d;
    uint8_t sga, sgb, sgo, sua, sub_, suo;

    memcpy(ctx->global + GA, Q, (size_t)M * N);
    memcpy(ctx->global + GB, S, (size_t)M * (N / g));
    memset(ctx->global + GO, 0xAA, osz);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP8_E4M3, GA, 1, M, 1, N);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_UE8M0, GB, 1, M, 1, (uint16_t)(N / g));
    sgb = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, odt, GO, 1, M, 1, N);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP8_E4M3, UA, 1, M, 1, N, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_UE8M0, UB, 1, M, 1, (uint16_t)(N / g), 0);
    sub_ = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, odt, UO, 1, M, 1, N, 0);
    suo = mk_desc(ctx, &d);

    top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
    top_ldst_load(sub_, sgb, 0, LDST_MODE_NORMAL);
    top_vec_dequant_fp8(suo, sua, sub_, gs);
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(OUT, ctx->global + GO, osz);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgb);
    tdr_free(ctx, sgo);
    tdr_free(ctx, sua);
    tdr_free(ctx, sub_);
    tdr_free(ctx, suo);
}

/* BF16 bit pattern of an exactly-representable float (the test values
 * are dyadic, so the truncation is the RNE result) */
static uint16_t bf16_bits(float v) {
    uint32_t b;
    memcpy(&b, &v, sizeof(b));
    return (uint16_t)(b >> 16);
}

/* Standard int8 dequant case: Q + scale/min pairs enter, W = scale*q-min
 * (scale_min and output are BF16 only) */
static void run_dequant_i8(tnkr_ctx_t *ctx, uint16_t M, uint16_t N, group_size_i8_t gs,
                           const int8_t *Q, const uint16_t *SM, uint16_t *OUT) {
    const uint32_t g = (gs == GROUP_SIZE_128) ? 128u : 64u;
    const size_t osz = (size_t)M * N * 2u;
    tdr_t d;
    uint8_t sga, sgb, sgo, sua, sub_, suo;

    memcpy(ctx->global + GA, Q, (size_t)M * N);
    memcpy(ctx->global + GB, SM, (size_t)M * (N / g) * 2 * 2u);
    memset(ctx->global + GO, 0xAA, osz);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, M, 1, N);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_BF16, GB, 1, M, (uint16_t)(N / g), 2);
    sgb = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_BF16, GO, 1, M, 1, N);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, M, 1, N, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_BF16, UB, 1, M, (uint16_t)(N / g), 2, 0);
    sub_ = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_BF16, UO, 1, M, 1, N, 0);
    suo = mk_desc(ctx, &d);

    top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
    top_ldst_load(sub_, sgb, 0, LDST_MODE_NORMAL);
    top_vec_dequant_int8(suo, sua, sub_, gs);
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(OUT, ctx->global + GO, osz);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgb);
    tdr_free(ctx, sgo);
    tdr_free(ctx, sua);
    tdr_free(ctx, sub_);
    tdr_free(ctx, suo);
}

/* Negative selector for check_quant_reject */
enum {
    QJ_KMOD,    /* N not divisible by g (N=48, g32) */
    QJ_SCALEDT, /* scale descriptor BF16 (fp8 quant needs UE8M0) */
    QJ_OUTDT,   /* quant output BF16 (needs FP8_E4M3) */
    QJ_INTIN,   /* quant input S16 (needs BF16/FP32) */
    QJ_SCALESH, /* scale dw = N//g + 1 */
    QJ_SMDW,    /* int8 dequant scale_min dw = 3 (needs 2) */
    QJ_SMDT,    /* int8 dequant scale_min FP32 (needs BF16) */
    QJ_SMODT    /* int8 dequant output FP32 (needs BF16) */
};

/* One-illegality pipeline (fp8 quant skeleton, M=1 N=32 g32, except the
 * selected flaw); the poisoned q AND scale planes must both survive */
static void check_quant_reject(tnkr_ctx_t *ctx, const char *name, int why) {
    const uint16_t N = (why == QJ_KMOD) ? 48 : 32;
    const tn_dtype_t qdt = (why == QJ_OUTDT) ? TN_DTYPE_BF16 : TN_DTYPE_FP8_E4M3;
    const tn_dtype_t sdt = (why == QJ_SCALEDT) ? TN_DTYPE_BF16 : TN_DTYPE_UE8M0;
    const tn_dtype_t idt = (why == QJ_INTIN) ? TN_DTYPE_S16 : TN_DTYPE_FP32;
    const uint16_t sdw = (why == QJ_SCALESH) ? 2 : 1;
    const size_t qsz = ((size_t)N * tn_dtype_bits(qdt) + 7u) / 8u;
    uint8_t qout[64], sout[64], poison[64];
    tdr_t d;
    uint8_t sga, sgq, sgs, sua, suq, sus;

    if (why == QJ_SMDW || why == QJ_SMDT || why == QJ_SMODT) {
        /* int8 dequant skeleton: in S8 (1,1,1,64), scale_min BF16
         * (1,1,1,2), out BF16 — except the selected flaw */
        const tn_dtype_t smdt = (why == QJ_SMDT) ? TN_DTYPE_FP32 : TN_DTYPE_BF16;
        const tn_dtype_t odt = (why == QJ_SMODT) ? TN_DTYPE_FP32 : TN_DTYPE_BF16;
        const uint16_t smdw = (why == QJ_SMDW) ? 3 : 2;
        uint16_t o64[64];
        uint8_t poison256[256];
        tdr_t d2;
        uint8_t tga, tgb, tgo, tua, tub, tuo;

        memset(ctx->global + GA, 0x03, 64);
        memset(poison256, 0xAA, sizeof(poison256));
        d2 = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 1, 1, 64);
        tga = mk_desc(ctx, &d2);
        d2 = tdr_make_global(TDR_LAYOUT_CONTINUOUS, smdt, GB, 1, 1, 1, smdw);
        tgb = mk_desc(ctx, &d2);
        d2 = tdr_make_global(TDR_LAYOUT_CONTINUOUS, odt, GO, 1, 1, 1, 64);
        tgo = mk_desc(ctx, &d2);
        d2 = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, 1, 1, 64, 0);
        tua = mk_desc(ctx, &d2);
        d2 = tdr_make_urf(TDR_LAYOUT_HWALIGN, smdt, UB, 1, 1, 1, smdw, 0);
        tub = mk_desc(ctx, &d2);
        d2 = tdr_make_urf(TDR_LAYOUT_HWALIGN, odt, UO, 1, 1, 1, 64, 0);
        tuo = mk_desc(ctx, &d2);

        top_ldst_load(tua, tga, 0, LDST_MODE_NORMAL);
        memset(ctx->urf + UO, 0xAA, sizeof(o64)); /* poison the output plane */
        TNVI_BEGIN("vec_dequant.int8:") { top_vec_dequant_int8(tuo, tua, tub, GROUP_SIZE_64); }
        TNVI_END();
        top_ldst_store(tgo, 0, tuo, LDST_MODE_NORMAL);
        memcpy(o64, ctx->global + GO, sizeof(o64));
        CHECK(memcmp(o64, poison256, sizeof(o64)) == 0, name);

        tdr_free(ctx, tga);
        tdr_free(ctx, tgb);
        tdr_free(ctx, tgo);
        tdr_free(ctx, tua);
        tdr_free(ctx, tub);
        tdr_free(ctx, tuo);
        return;
    }

    memset(ctx->global + GA, 0x11, (size_t)N * tn_dtype_size(idt));
    memset(ctx->global + GO, 0xAA, qsz);       /* stale bytes must not leak in */
    memset(ctx->global + GO + 4096u, 0xAA, 4); /* the scale store is narrower than 4 */
    memset(poison, 0xAA, sizeof(poison));

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, idt, GA, 1, 1, 1, N);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, qdt, GO, 1, 1, 1, N);
    sgq = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, sdt, GO + 4096u, 1, 1, 1, sdw);
    sgs = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, idt, UA, 1, 1, 1, N, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, qdt, UO, 1, 1, 1, N, 0);
    suq = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, sdt, UB, 1, 1, 1, sdw, 0);
    sus = mk_desc(ctx, &d);

    top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
    memset(ctx->urf + UO, 0xAA, qsz); /* poison the q plane */
    memset(ctx->urf + UB, 0xAA, 4);   /* poison the scale plane */
    TNVI_BEGIN("vec_quant") { top_vec_quant_fp8(suq, sus, sua, GROUP_SIZE_32); }
    TNVI_END();
    top_ldst_store(sgq, 0, suq, LDST_MODE_NORMAL);
    top_ldst_store(sgs, 0, sus, LDST_MODE_NORMAL);
    memcpy(qout, ctx->global + GO, qsz);
    memcpy(sout, ctx->global + GO + 4096u, 4);
    CHECK(memcmp(qout, poison, qsz) == 0 && memcmp(sout, poison, 4) == 0, name);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgq);
    tdr_free(ctx, sgs);
    tdr_free(ctx, sua);
    tdr_free(ctx, suq);
    tdr_free(ctx, sus);
}

void suite_vec_quant(tnkr_ctx_t *ctx) {
    uint32_t i;

    printf("== vec_quant ==\n");

    /* ---- quant_fp8 g32, N=64: group 0 has amax=4 (scale 2^-6 = 0x79),
     * group 1 is all zero (scale 0x00, q 0) ---- */
    {
        float in[64];
        uint8_t q[64], s[2], qexp[64];
        static const uint8_t sexp[2] = {0x79, 0x00};

        memset(in, 0, sizeof(in));
        memset(qexp, 0, sizeof(qexp));
        in[0] = 1.0f;  /* / 2^-6 = 64  -> 0x68 */
        in[1] = 2.0f;  /* -> 128 -> 0x70 */
        in[2] = 0.5f;  /* -> 32  -> 0x60 */
        in[3] = -4.0f; /* -> -256 -> 0xF8 */
        qexp[0] = 0x68;
        qexp[1] = 0x70;
        qexp[2] = 0x60;
        qexp[3] = 0xF8;
        run_quant(ctx, top_vec_quant_fp8, 1, 64, GROUP_SIZE_32, TN_DTYPE_FP8_E4M3, TN_DTYPE_UE8M0,
                  in, q, s);
        CHECK(memcmp(q, qexp, sizeof(qexp)) == 0 && memcmp(s, sexp, sizeof(sexp)) == 0,
              "quant_fp8 g32 2-group + zero group");
    }

    /* ---- quant_fp8: the MX floor scale saturates the amax element
     * (amax=500 -> scale 2^0, 500 clamps to 448) ---- */
    {
        float in[32];
        uint8_t q[32], s[1], qexp[32];
        static const uint8_t sexp[1] = {0x7F};

        memset(in, 0, sizeof(in));
        memset(qexp, 0, sizeof(qexp));
        in[0] = 500.0f;
        in[1] = 1.5f;
        in[2] = -500.0f;
        qexp[0] = 0x7E; /* saturate 448 */
        qexp[1] = 0x3C; /* 1.5 */
        qexp[2] = 0xFE; /* -448 */
        run_quant(ctx, top_vec_quant_fp8, 1, 32, GROUP_SIZE_32, TN_DTYPE_FP8_E4M3, TN_DTYPE_UE8M0,
                  in, q, s);
        CHECK(memcmp(q, qexp, sizeof(qexp)) == 0 && memcmp(s, sexp, sizeof(sexp)) == 0,
              "quant_fp8 amax saturation (MX floor scale)");
    }

    /* ---- quant_fp8 g16: two groups, negative exponents ---- */
    {
        float in[32];
        uint8_t q[32], s[2], qexp[32];
        static const uint8_t sexp[2] = {0x7F, 0x75};

        memset(in, 0, sizeof(in));
        memset(qexp, 0, sizeof(qexp));
        in[0] = 448.0f; /* group 0: scale 2^0; q = 448 -> 0x7E */
        in[16] = 0.25f; /* group 1: scale 2^-10 (0x75); q = 256 -> 0x78 */
        qexp[0] = 0x7E;
        qexp[16] = 0x78;
        run_quant(ctx, top_vec_quant_fp8, 1, 32, GROUP_SIZE_16, TN_DTYPE_FP8_E4M3, TN_DTYPE_UE8M0,
                  in, q, s);
        CHECK(memcmp(q, qexp, sizeof(qexp)) == 0 && memcmp(s, sexp, sizeof(sexp)) == 0,
              "quant_fp8 g16 2-group");
    }

    /* ---- quant_fp8 g16, M=2: six groups, six distinct scale exponents,
     * a zero group mid-row; exercises the (mi, gi) scale indexing ---- */
    {
        float in[96];
        uint8_t q[96], s[6], qexp[96];
        static const uint8_t sexp[6] = {0x79, 0x75, 0x00, 0x7F, 0x70, 0x77};

        memset(in, 0, sizeof(in));
        memset(qexp, 0, sizeof(qexp));
        /* row 0: group amaxes 4 / 0.25 / 0 */
        in[0] = 1.0f; /* /2^-6 -> 64 */
        qexp[0] = 0x68;
        in[1] = -4.0f; /* -> -256 */
        qexp[1] = 0xF8;
        in[16] = 0.25f; /* /2^-10 -> 256 */
        qexp[16] = 0x78;
        in[17] = -0.125f; /* -> -128 */
        qexp[17] = 0xF0;
        /* row 1: group amaxes 448 / 2^-7 / 1.5 */
        in[48] = 448.0f; /* /2^0 -> 448 */
        qexp[48] = 0x7E;
        in[49] = -224.0f; /* -> -224 */
        qexp[49] = 0xF6;
        in[64] = 0.0078125f; /* 2^-7 / 2^-15 -> 256 */
        qexp[64] = 0x78;
        in[65] = 0.001953125f; /* 2^-9 -> 64 */
        qexp[65] = 0x68;
        in[80] = 1.5f; /* /2^-8 -> 384 */
        qexp[80] = 0x7C;
        in[81] = -0.75f; /* -> -192 */
        qexp[81] = 0xF4;
        run_quant(ctx, top_vec_quant_fp8, 2, 48, GROUP_SIZE_16, TN_DTYPE_FP8_E4M3, TN_DTYPE_UE8M0,
                  in, q, s);
        CHECK(memcmp(q, qexp, sizeof(qexp)) == 0 && memcmp(s, sexp, sizeof(sexp)) == 0,
              "quant_fp8 g16 M=2 6-group");
    }

    /* ---- dequant_fp8: q * scale, exact dyadics ---- */
    {
        uint8_t q[32], s[1];
        float out[32], expct[32];

        memset(q, 0, sizeof(q));
        memset(expct, 0, sizeof(expct));
        q[0] = 0x68; /* 64 */
        q[1] = 0xC0; /* -2 */
        q[2] = 0x7E; /* 448 */
        s[0] = 0x7B; /* 2^-4 */
        expct[0] = 4.0f;
        expct[1] = -0.125f;
        expct[2] = 28.0f;
        run_dequant_fp8(ctx, 1, 32, GROUP_SIZE_32, q, s, TN_DTYPE_FP32, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "dequant_fp8 g32");
    }

    /* ---- dequant_fp8 g16, M=2: eight groups, eight distinct scales
     * including a zero scale that zeroes its whole group ---- */
    {
        uint8_t q[128], s[8];
        float out[128], expct[128];

        memset(q, 0, sizeof(q));
        memset(expct, 0, sizeof(expct));
        s[0] = 0x7B; /* 2^-4 */
        s[1] = 0x7F; /* 2^0  */
        s[2] = 0x82; /* 2^3  */
        s[3] = 0x75; /* 2^-10 */
        s[4] = 0x80; /* 2^1  */
        s[5] = 0x00; /* zero scale (ef = 0 decodes to 0) */
        s[6] = 0x7E; /* 2^-1 */
        s[7] = 0x85; /* 2^6  */
        q[0] = 0x68; /* 64  * 2^-4 */
        expct[0] = 4.0f;
        q[1] = 0xC0; /* -2  * 2^-4 */
        expct[1] = -0.125f;
        q[16] = 0x7E; /* 448 * 1 */
        expct[16] = 448.0f;
        q[17] = 0x30; /* 0.5 * 1 */
        expct[17] = 0.5f;
        q[32] = 0x38; /* 1 * 2^3 */
        expct[32] = 8.0f;
        q[48] = 0x40; /* 2 * 2^-10 */
        expct[48] = 0.001953125f;
        q[64] = 0xB0; /* -0.5 * 2 */
        expct[64] = -1.0f;
        q[80] = 0x68; /* zero scale -> +0 */
        expct[80] = 0.0f;
        q[81] = 0xC0; /* -2 * 0 -> -0 (sign bit survives) */
        expct[81] = -0.0f;
        q[96] = 0x68; /* 64 * 0.5 */
        expct[96] = 32.0f;
        q[112] = 0x38; /* 1 * 64 */
        expct[112] = 64.0f;
        q[113] = 0xC0; /* -2 * 64 */
        expct[113] = -128.0f;
        run_dequant_fp8(ctx, 2, 64, GROUP_SIZE_16, q, s, TN_DTYPE_FP32, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "dequant_fp8 g16 M=2 8-group");
    }

    /* ---- quant_mxfp4 g32: amax=6 -> scale 2^0, identity codes
     * (OCP E2M1 incl. +-6 = 0x7/0xF), packed low-nibble-first ---- */
    {
        float in[32];
        uint8_t q[16], s[1], qexp[16];
        static const float V[14] = {0.5f,  1.0f,  1.5f,  2.0f,  3.0f,  4.0f,  6.0f,
                                    -0.5f, -1.0f, -1.5f, -2.0f, -3.0f, -4.0f, -6.0f};
        static const uint8_t sexp[1] = {0x7F};

        memset(in, 0, sizeof(in));
        memset(qexp, 0, sizeof(qexp));
        for (i = 0; i < 14; i++)
            in[i] = V[i];
        qexp[0] = 0x21; /* 0.5, 1 */
        qexp[1] = 0x43; /* 1.5, 2 */
        qexp[2] = 0x65; /* 3, 4 */
        qexp[3] = 0x97; /* 6, -0.5 */
        qexp[4] = 0xBA; /* -1, -1.5 */
        qexp[5] = 0xDC; /* -2, -3 */
        qexp[6] = 0xFE; /* -4, -6 */
        run_quant(ctx, top_vec_quant_mxfp4, 1, 32, GROUP_SIZE_32, TN_DTYPE_FP4, TN_DTYPE_UE8M0, in,
                  q, s);
        CHECK(memcmp(q, qexp, sizeof(qexp)) == 0 && memcmp(s, sexp, sizeof(sexp)) == 0,
              "quant_mxfp4 g32 identity codes");
    }

    /* ---- quant_mxfp4: amax=7 -> ceil(log2(7/6))=1 -> scale 2^1; the
     * scaled amax 3.5 ties to the even-mantissa 4 — the round-up scale
     * convention never saturates the amax element ---- */
    {
        float in[32];
        uint8_t q[16], s[1], qexp[16];
        static const uint8_t sexp[1] = {0x80}; /* 2^1 */

        memset(in, 0, sizeof(in));
        memset(qexp, 0, sizeof(qexp));
        in[0] = 7.0f;   /* /2 = 3.5 -> tie -> 4 */
        in[1] = -7.0f;  /* -> -4 */
        in[2] = 6.5f;   /* /2 = 3.25 -> 3 */
        qexp[0] = 0xE6; /* 4, -4 */
        qexp[1] = 0x05; /* 3, 0 */
        run_quant(ctx, top_vec_quant_mxfp4, 1, 32, GROUP_SIZE_32, TN_DTYPE_FP4, TN_DTYPE_UE8M0, in,
                  q, s);
        CHECK(memcmp(q, qexp, sizeof(qexp)) == 0 && memcmp(s, sexp, sizeof(sexp)) == 0,
              "quant_mxfp4 ceil scale (no saturation)");
    }

    /* ---- quant_mxfp4 g16: two groups, the second with a fractional
     * scale; the packing crosses the group boundary ---- */
    {
        float in[32];
        uint8_t q[16], s[2], qexp[16];
        static const uint8_t sexp[2] = {0x7F, 0x7D}; /* 2^0, 2^-2 */

        memset(in, 0, sizeof(in));
        memset(qexp, 0, sizeof(qexp));
        in[0] = 6.0f; /* scale 2^0: 6, -6 */
        in[1] = -6.0f;
        qexp[0] = 0xF7;
        in[2] = 3.0f; /* 3, 0 */
        qexp[1] = 0x05;
        in[16] = 1.5f; /* scale 2^-2: 1.5/0.25=6, 0.75/0.25=3 */
        in[17] = 0.75f;
        qexp[8] = 0x57;
        in[18] = -1.5f; /* -6, 0.375/0.25=1.5 */
        in[19] = 0.375f;
        qexp[9] = 0x3F;
        in[20] = 0.25f; /* 1, 0 */
        qexp[10] = 0x02;
        run_quant(ctx, top_vec_quant_mxfp4, 1, 32, GROUP_SIZE_16, TN_DTYPE_FP4, TN_DTYPE_UE8M0, in,
                  q, s);
        CHECK(memcmp(q, qexp, sizeof(qexp)) == 0 && memcmp(s, sexp, sizeof(sexp)) == 0,
              "quant_mxfp4 g16 2-group fractional scale");
    }

    /* ---- quant_nxfp4 g16: amax=6 -> round_up(1.0) UE5M3 = 0x78 ---- */
    {
        float in[16];
        uint8_t q[8], s[1], qexp[8];
        static const float V[6] = {6.0f, 3.0f, 1.5f, -6.0f, 2.0f, -0.5f};
        static const uint8_t sexp[1] = {0x78};

        memset(in, 0, sizeof(in));
        memset(qexp, 0, sizeof(qexp));
        for (i = 0; i < 6; i++)
            in[i] = V[i];
        qexp[0] = 0x57; /* 6, 3 */
        qexp[1] = 0xF3; /* 1.5, -6 */
        qexp[2] = 0x94; /* 2, -0.5 */
        run_quant(ctx, top_vec_quant_nxfp4, 1, 16, GROUP_SIZE_16, TN_DTYPE_FP4, TN_DTYPE_UE5M3, in,
                  q, s);
        CHECK(memcmp(q, qexp, sizeof(qexp)) == 0 && memcmp(s, sexp, sizeof(sexp)) == 0,
              "quant_nxfp4 g16 exact-mark scale");
    }

    /* ---- quant_nxfp4: round_up never rounds the scale down (6.1/6 ->
     * 1.125), the amax element stays in range (6.1/1.125 = 5.42 -> RNE 6) ---- */
    {
        float in[16];
        uint8_t q[8], s[1], qexp[8];
        static const uint8_t sexp[1] = {0x79}; /* UE5M3 1.125 */

        memset(in, 0, sizeof(in));
        memset(qexp, 0, sizeof(qexp));
        in[0] = 6.1f;   /* /1.125 = 5.422 -> RNE 6 */
        in[1] = 3.05f;  /* /1.125 = 2.711 -> RNE 3 */
        qexp[0] = 0x57; /* 6, 3 */
        run_quant(ctx, top_vec_quant_nxfp4, 1, 16, GROUP_SIZE_16, TN_DTYPE_FP4, TN_DTYPE_UE5M3, in,
                  q, s);
        CHECK(memcmp(q, qexp, sizeof(qexp)) == 0 && memcmp(s, sexp, sizeof(sexp)) == 0,
              "quant_nxfp4 round-up scale (no saturation)");
    }

    /* ---- quant_nxfp4: round_up past the midpoint (6.6/6 = 1.1 -> 1.125) ---- */
    {
        float in[16];
        uint8_t q[8], s[1], qexp[8];
        static const uint8_t sexp[1] = {0x79}; /* UE5M3 1.125 */

        memset(in, 0, sizeof(in));
        memset(qexp, 0, sizeof(qexp));
        in[0] = 6.6f;   /* /1.125 = 5.867 -> RNE 6 */
        in[1] = 3.3f;   /* /1.125 = 2.933 -> RNE 3 */
        in[2] = -6.6f;  /* -> -6 */
        qexp[0] = 0x57; /* 6, 3 */
        qexp[1] = 0x0F; /* -6, 0 */
        run_quant(ctx, top_vec_quant_nxfp4, 1, 16, GROUP_SIZE_16, TN_DTYPE_FP4, TN_DTYPE_UE5M3, in,
                  q, s);
        CHECK(memcmp(q, qexp, sizeof(qexp)) == 0 && memcmp(s, sexp, sizeof(sexp)) == 0,
              "quant_nxfp4 round-up past midpoint");
    }

    /* ---- quant_nxfp4: the element path multiplies by the scale's LUT
     * reciprocal — under exact division 5.625/1.125 = 5.0 would tie to
     * the even-mantissa 4, but 5.625 * LUT(1.125) lands above the tie ---- */
    {
        float in[16];
        uint8_t q[8], s[1], qexp[8];
        static const uint8_t sexp[1] = {0x79}; /* UE5M3 1.125 */

        memset(in, 0, sizeof(in));
        memset(qexp, 0, sizeof(qexp));
        in[0] = 6.5f;   /* amax: raw 1.0833 -> round_up 1.125; *LUT -> 6 */
        in[1] = 5.625f; /* * 0.888889 = 5.0000005 -> 6 (exact / ties -> 4) */
        qexp[0] = 0x77; /* 6, 6 */
        run_quant(ctx, top_vec_quant_nxfp4, 1, 16, GROUP_SIZE_16, TN_DTYPE_FP4, TN_DTYPE_UE5M3, in,
                  q, s);
        CHECK(memcmp(q, qexp, sizeof(qexp)) == 0 && memcmp(s, sexp, sizeof(sexp)) == 0,
              "quant_nxfp4 LUT reciprocal crosses a division tie");
    }

    /* ---- quant_nxfp4: a sub-normal-range raw scale flushes to the
     * minimum normal pattern (the chip has no subnormals) ---- */
    {
        float in[16];
        uint8_t q[8], s[1], qexp[8];
        static const uint8_t sexp[1] = {0x08}; /* UE5M3 min normal 2^-14 */

        memset(in, 0, sizeof(in));
        memset(qexp, 0, sizeof(qexp));
        in[0] = 1e-6f; /* raw 1.67e-7 -> flush; * 2^14 = 0.016 -> 0 */
        run_quant(ctx, top_vec_quant_nxfp4, 1, 16, GROUP_SIZE_16, TN_DTYPE_FP4, TN_DTYPE_UE5M3, in,
                  q, s);
        CHECK(memcmp(q, qexp, sizeof(qexp)) == 0 && memcmp(s, sexp, sizeof(sexp)) == 0,
              "quant_nxfp4 tiny group flushes scale to min normal");
    }

    /* ---- dequant_int8 g64: W = 2q - 1 ---- */
    {
        int8_t q[64];
        uint16_t sm[2], out[64], expct[64];

        sm[0] = bf16_bits(2.0f);
        sm[1] = bf16_bits(1.0f);
        memset(q, 0, sizeof(q));
        for (i = 0; i < 64; i++)
            expct[i] = bf16_bits(-1.0f); /* unpicked q=0 elements decode to -min */
        q[0] = 1;
        q[1] = 2;
        q[2] = -1;
        q[3] = 64;
        expct[0] = bf16_bits(1.0f);
        expct[1] = bf16_bits(3.0f);
        expct[2] = bf16_bits(-3.0f);
        expct[3] = bf16_bits(127.0f);
        run_dequant_i8(ctx, 1, 64, GROUP_SIZE_64, q, sm, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "dequant_int8 g64 (W = 2q-1)");
    }

    /* ---- dequant_int8 g128: W = 0.5q + 1 ---- */
    {
        int8_t q[128];
        uint16_t sm[2] = {0x3F00, 0xBF80}; /* 0.5, -1.0 */
        uint16_t out[128], expct[128];

        memset(q, 0, sizeof(q));
        for (i = 0; i < 128; i++)
            expct[i] = bf16_bits(1.0f); /* unpicked q=0 elements decode to -min */
        q[0] = 2;
        q[1] = -2;
        q[2] = 100;
        expct[0] = bf16_bits(2.0f);
        expct[1] = bf16_bits(0.0f);
        expct[2] = bf16_bits(51.0f);
        run_dequant_i8(ctx, 1, 128, GROUP_SIZE_128, q, sm, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "dequant_int8 g128 (W = 0.5q+1)");
    }

    /* ---- dequant_int8 g64, M=2: four distinct (scale, min) pairs;
     * unpicked elements decode to -min per group ---- */
    {
        static const float SMV[8] = {2.0f, 1.0f, 0.5f, -2.0f, 4.0f, -8.0f, 0.25f, 3.0f};
        int8_t q[256];
        uint16_t sm[8], out[256], expct[256];

        for (i = 0; i < 8; i++)
            sm[i] = bf16_bits(SMV[i]);
        memset(q, 0, sizeof(q));
        for (i = 0; i < 64; i++)
            expct[i] = bf16_bits(-1.0f); /* row 0 g0: -min */
        for (i = 64; i < 128; i++)
            expct[i] = bf16_bits(2.0f); /* row 0 g1 */
        for (i = 128; i < 192; i++)
            expct[i] = bf16_bits(8.0f); /* row 1 g0 */
        for (i = 192; i < 256; i++)
            expct[i] = bf16_bits(-3.0f); /* row 1 g1 */
        q[0] = 1;                        /* 2*1 - 1 */
        expct[0] = bf16_bits(1.0f);
        q[1] = -64; /* 2*(-64) - 1 */
        expct[1] = bf16_bits(-129.0f);
        q[64] = 100; /* 0.5*100 + 2 */
        expct[64] = bf16_bits(52.0f);
        q[65] = -128; /* 0.5*(-128) + 2 */
        expct[65] = bf16_bits(-62.0f);
        q[128] = -1; /* 4*(-1) + 8 */
        expct[128] = bf16_bits(4.0f);
        q[129] = 127; /* 4*127 + 8 */
        expct[129] = bf16_bits(516.0f);
        q[192] = 8; /* 0.25*8 - 3 */
        expct[192] = bf16_bits(-1.0f);
        q[193] = -4; /* 0.25*(-4) - 3 */
        expct[193] = bf16_bits(-4.0f);
        run_dequant_i8(ctx, 2, 128, GROUP_SIZE_64, q, sm, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "dequant_int8 g64 M=2 4-pair");
    }

    /* ---- dequant_int8: the MAC rounds to BF16 per step — q=-126 with
     * scale = min = BF16(0.1): double-precision math would give
     * 0xC14B, the BF16-step chain gives 0xC14C ---- */
    {
        int8_t q[64];
        uint16_t sm[2] = {0x3DCD, 0x3DCD}; /* BF16(0.1), BF16(0.1) */
        uint16_t out[64], expct[64];

        memset(q, 0, sizeof(q));
        for (i = 0; i < 64; i++)
            expct[i] = 0xBDCD; /* q=0 -> -min */
        q[0] = -126;           /* bf16(-126*0.1) - 0.1 -> 0xC14C (exact math: 0xC14B) */
        expct[0] = 0xC14C;
        run_dequant_i8(ctx, 1, 64, GROUP_SIZE_64, q, sm, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "dequant_int8 BF16-step MAC rounding");
    }

    /* ---- negatives: q and scale planes keep their poison ---- */
    check_quant_reject(ctx, "quant_fp8 N%%g!=0 -> violation", QJ_KMOD);
    check_quant_reject(ctx, "quant_fp8 scale BF16 -> violation", QJ_SCALEDT);
    check_quant_reject(ctx, "quant_fp8 output BF16 -> violation", QJ_OUTDT);
    check_quant_reject(ctx, "quant_fp8 input S16 -> violation", QJ_INTIN);
    check_quant_reject(ctx, "quant_fp8 scale dw!=N//g -> violation", QJ_SCALESH);
    check_quant_reject(ctx, "dequant_int8 scale_min dw!=2 -> violation", QJ_SMDW);
    check_quant_reject(ctx, "dequant_int8 scale_min FP32 -> violation", QJ_SMDT);
    check_quant_reject(ctx, "dequant_int8 output FP32 -> violation", QJ_SMODT);
}
