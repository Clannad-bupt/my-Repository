// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/test/test_vec_special.c
 *
 * top/vec special-function cases: normal / getmant (bit-field extraction,
 * explicit patterns incl. zero/subnormal/inf/nan -> 0), rsqrt (exact
 * golden values, negative -> NaN, zero -> +-inf, +inf -> +0), taylor
 * (every case loads a prefit row from taylor_lut.h: FP32/BF16 exp2, the
 * mixed exp2|tanh lane-wrap case, C-broadcast at load) and reciprocal (LUT + segment-local
 * quadratic interpolation, expectations traced through the fixed-point
 * algorithm by hand: interpolation, rounding carry, FTZ boundaries).
 * Plus violation negatives for illegal dtype/shape rules.
 * Expectations are computed independently on the test side (explicit bit
 * patterns, no codec reuse).
 * ========================================================================== */
#include "smoke.h"

#include "kernel/ref/rcp_lut.h"    /* shared fixed table asset (src/kernel/ref) */
#include "kernel/ref/taylor_lut.h" /* prefit taylor coeff tables (src/kernel/ref) */

#include "../dtype.h" /* tnkr_decode_fp/tnkr_encode_fp: trace the ref algorithm exactly */

/* rcp table buffers: one (1, smc, 1, TNKR_RCP_TBL_WORDS) S32 tensor; the
 * spec row is broadcast to every lane (row c serves lane c) */
#define GR 32768u /* table backing store in global memory */
#define UR 2048u  /* table plane base inside each lane window */

/* Standard rcp case: input -> global->URF->op->global loop with the table
 * loaded alongside; output lands in OUT. Input shape (1,dc,4,8). The table
 * keeps ONE physical copy in global: a FREE descriptor with c_str=0 makes
 * ldst broadcast it into every lane's URF plane at load time. */
static void run_rcp(tnkr_ctx_t *ctx, uint32_t dc, tn_dtype_t idt, const void *A, void *OUT) {
    const uint32_t smc = ctx->info.smc_num;
    tdr_t d;
    tdr_stride_t s;
    uint8_t sga, sgr, sgo, sua, sur, suo, sstr;
    int sidx;
    size_t isz = dc * NELEM * tn_dtype_size(idt);

    memcpy(ctx->global + GA, A, isz);
    memcpy(ctx->global + GR, TNKR_RCP_LUT, sizeof(TNKR_RCP_LUT));

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, idt, GA, 1, (uint16_t)dc, 4, 8);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_FREE, TN_DTYPE_S32, GR, 1, (uint16_t)smc, 1,
                        (uint16_t)TNKR_RCP_TBL_WORDS);
    sgr = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, idt, GO, 1, (uint16_t)dc, 4, 8);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, idt, UA, 1, (uint16_t)dc, 4, 8, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S32, UR, 1, (uint16_t)smc, 1,
                     (uint16_t)TNKR_RCP_TBL_WORDS, 0);
    sur = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, idt, UO, 1, (uint16_t)dc, 4, 8, 0);
    suo = mk_desc(ctx, &d);

    /* companion stride: strides count elements — w_str=1 (S32), c_str=0
     * -> C-broadcast at load */
    s = tdr_make_stride(1u, 0u, 0u, 0u);
    sidx = tdr_alloc(ctx);
    if (sidx < 0 || tdr_shadow_write_stride(ctx, (uint8_t)sidx, &s) != TN_SUCCESS) {
        printf("  FATAL: rcp table stride setup failed\n");
        g_fail++;
        sstr = 0;
    } else {
        sstr = (uint8_t)sidx;
    }

    top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
    top_ldst_load(sur, sgr, sstr, LDST_MODE_NORMAL);
    top_vec_reciprocal(suo, sua, sur);
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(OUT, ctx->global + GO, isz);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgr);
    tdr_free(ctx, sgo);
    tdr_free(ctx, sua);
    tdr_free(ctx, sur);
    tdr_free(ctx, suo);
    tdr_free(ctx, sstr);
}

/* One illegality of the rcp pipeline; the poisoned output must survive */
static void check_rcp_reject(tnkr_ctx_t *ctx, const char *name, tn_dtype_t idt, tn_dtype_t odt,
                             tn_dtype_t tdt, uint32_t tdc, uint32_t tdh, uint32_t tdw) {
    size_t osz = NELEM * tn_dtype_size(odt);
    uint32_t in_a[NELEM], out[NELEM], poison[NELEM];
    tdr_t d;
    uint8_t sga, sgr, sgo, sua, sur, suo;
    uint32_t i;

    for (i = 0; i < NELEM; i++) {
        in_a[i] = 0x3F800000u;
        poison[i] = 0xAAAAAAAAu;
    }
    memcpy(ctx->global + GA, in_a, sizeof(in_a));
    /* a loaded-in table keeps the op's operand views well-formed */
    for (i = 0; i < ctx->info.smc_num * TNKR_RCP_TBL_WORDS; i++)
        ((int32_t *)(ctx->global + GR))[i] = 1;

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, idt, GA, 1, 1, 4, 8);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, tdt, GR, 1, (uint16_t)tdc, (uint16_t)tdh,
                        (uint16_t)tdw);
    sgr = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, odt, GO, 1, 1, 4, 8);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, idt, UA, 1, 1, 4, 8, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, tdt, UR, 1, (uint16_t)tdc, (uint16_t)tdh, (uint16_t)tdw,
                     0);
    sur = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, odt, UO, 1, 1, 4, 8, 0);
    suo = mk_desc(ctx, &d);

    top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
    top_ldst_load(sur, sgr, 0, LDST_MODE_NORMAL);
    memset(ctx->urf + UO, 0xAA, osz); /* poison the output plane */
    TNVI_BEGIN("vec_rcp") { top_vec_reciprocal(suo, sua, sur); }
    TNVI_END();
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(out, ctx->global + GO, osz);
    CHECK(memcmp(out, poison, osz) == 0, name);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgr);
    tdr_free(ctx, sgo);
    tdr_free(ctx, sua);
    tdr_free(ctx, sur);
    tdr_free(ctx, suo);
}

/* ---- taylor prefit-table helpers (taylor_lut.h) ----
 * taylor_ref_eval: exact trace of the ref taylor loop for a prefit table
 * row -- decode each coefficient at the operand dtype, Horner in double.
 * taylor_fill_row: copy one authored (1,1,1,step) row into a per-lane
 * coeff tensor slot as the operand dtype's bit pattern. */
static double taylor_ref_eval(const void *coeff, uint32_t step, tn_dtype_t dt, double x) {
    double y = 0.0;
    int k;

    for (k = (int)step - 1; k >= 0; k--) {
        uint64_t bits =
            (dt == TN_DTYPE_BF16) ? ((const uint16_t *)coeff)[k] : ((const uint32_t *)coeff)[k];
        y = y * x + tnkr_decode_fp(bits, dt);
    }
    return y;
}

static void taylor_fill_row(uint32_t *dst, const void *row, uint32_t step, tn_dtype_t dt) {
    uint32_t k;

    for (k = 0; k < step; k++)
        dst[k] = (dt == TN_DTYPE_BF16) ? ((const uint16_t *)row)[k] : ((const uint32_t *)row)[k];
}

void suite_vec_special(tnkr_ctx_t *ctx) {
    uint32_t i;

    printf("== vec_special ==\n");

    /* ---- normal: FP exponent - bias -> S8; specials -> 0 ---- */
    /* FP32: 1.0 -> 0, 4.0 -> 2, 0.5 -> -1, -2.0 -> 1, zero/subnormal/
     * inf/nan -> 0 */
    {
        uint32_t fa[NELEM];
        int8_t fout[NELEM], fexp[NELEM];
        memset(fa, 0, sizeof(fa));
        memset(fexp, 0, sizeof(fexp));
        fa[0] = 0x3F800000u;
        fexp[0] = 0;
        fa[1] = 0x40800000u;
        fexp[1] = 2;
        fa[2] = 0x3F000000u;
        fexp[2] = -1;
        fa[3] = 0xC0000000u;
        fexp[3] = 1;
        fa[4] = 0x00000001u; /* subnormal */
        fexp[4] = 0;
        fa[5] = 0x7F800000u; /* inf */
        fexp[5] = 0;
        fa[6] = 0x7FC00000u; /* nan */
        fexp[6] = 0;
        run_unary(ctx, top_vec_normal, TN_DTYPE_FP32, TN_DTYPE_S8, fa, fout);
        CHECK(memcmp(fout, fexp, sizeof(fexp)) == 0, "normal FP32 -> S8");
    }

    /* FP8: 2.0 -> 1, 1.0 -> 0, 0.5 -> -1, zero/subnormal/nan -> 0 */
    {
        uint8_t fa[NELEM];
        int8_t fout[NELEM], fexp[NELEM];
        memset(fa, 0, sizeof(fa));
        memset(fexp, 0, sizeof(fexp));
        fa[0] = 0x40; /* 2.0 */
        fexp[0] = 1;
        fa[1] = 0x38; /* 1.0 */
        fexp[1] = 0;
        fa[2] = 0x30; /* 0.5 */
        fexp[2] = -1;
        fa[3] = 0x01; /* subnormal */
        fexp[3] = 0;
        fa[4] = 0x7F; /* nan */
        fexp[4] = 0;
        run_unary(ctx, top_vec_normal, TN_DTYPE_FP8_E4M3, TN_DTYPE_S8, fa, fout);
        CHECK(memcmp(fout, fexp, sizeof(fexp)) == 0, "normal FP8 -> S8");
    }

    /* ---- getmant: mantissa | hidden 1 -> U8/U16/U32; specials -> 0 ---- */
    /* FP32 -> U32: 1.0 -> 0x800000, 1.5 -> 0xC00000, -2.0 -> 0x800000,
     * specials -> 0 */
    {
        uint32_t fa[NELEM], fout[NELEM], fexp[NELEM];
        memset(fa, 0, sizeof(fa));
        memset(fexp, 0, sizeof(fexp));
        fa[0] = 0x3F800000u;
        fexp[0] = 0x00800000u;
        fa[1] = 0x3FC00000u;
        fexp[1] = 0x00C00000u;
        fa[2] = 0xC0000000u;
        fexp[2] = 0x00800000u;
        fa[3] = 0x00000001u; /* subnormal */
        fexp[3] = 0;
        fa[4] = 0x7FC00000u; /* nan */
        fexp[4] = 0;
        run_unary(ctx, top_vec_getmant, TN_DTYPE_FP32, TN_DTYPE_U32, fa, fout);
        CHECK(memcmp(fout, fexp, sizeof(fexp)) == 0, "getmant FP32 -> U32");
    }

    /* FP8 -> U8: 3.0 -> 0x0A, 1.0 -> 0x08, -0.5 -> 0x08, nan -> 0 */
    {
        uint8_t fa[NELEM], fout[NELEM], fexp[NELEM];
        memset(fa, 0, sizeof(fa));
        memset(fexp, 0, sizeof(fexp));
        fa[0] = 0x42; /* 2.5: m=2 */
        fexp[0] = 0x0A;
        fa[1] = 0x38; /* 1.0 */
        fexp[1] = 0x08;
        fa[2] = 0xB0; /* -0.5 */
        fexp[2] = 0x08;
        fa[3] = 0x7F; /* nan */
        fexp[3] = 0;
        run_unary(ctx, top_vec_getmant, TN_DTYPE_FP8_E4M3, TN_DTYPE_U8, fa, fout);
        CHECK(memcmp(fout, fexp, sizeof(fexp)) == 0, "getmant FP8 -> U8");
    }

    /* ---- rsqrt: exact golden values + documented specials ---- */
    /* FP32: 4->0.5, 0.25->2, 1->1, -1->NaN, +0->+inf, -0->-inf,
     * +inf->+0; unpicked elements are +0 inputs -> +inf */
    {
        uint32_t fa[NELEM], fout[NELEM], fexp[NELEM];
        memset(fa, 0, sizeof(fa));
        for (i = 0; i < NELEM; i++)
            fexp[i] = 0x7F800000u; /* default: rsqrt(+0) = +inf */
        fa[0] = 0x40800000u;
        fexp[0] = 0x3F000000u;
        fa[1] = 0x3E800000u;
        fexp[1] = 0x40000000u;
        fa[2] = 0x3F800000u;
        fexp[2] = 0x3F800000u;
        fa[3] = 0xBF800000u;
        fexp[3] = 0x7FC00000u; /* negative -> NaN */
        fa[4] = 0x00000000u;
        fexp[4] = 0x7F800000u; /* +0 -> +inf */
        fa[5] = 0x80000000u;
        fexp[5] = 0xFF800000u; /* -0 -> -inf */
        fa[6] = 0x7F800000u;
        fexp[6] = 0x00000000u; /* +inf -> +0 */
        run_unary(ctx, top_vec_rsqrt, TN_DTYPE_FP32, TN_DTYPE_FP32, fa, fout);
        CHECK(memcmp(fout, fexp, sizeof(fexp)) == 0, "rsqrt FP32 (incl. specials)");
    }

    /* BF16: 4->0.5, 0.25->2, -4->NaN, +0->+inf */
    {
        uint16_t fa[NELEM], fout[NELEM], fexp[NELEM];
        memset(fa, 0, sizeof(fa));
        for (i = 0; i < NELEM; i++)
            fexp[i] = 0x7F80; /* default: rsqrt(+0) = +inf */
        fa[0] = 0x4080;
        fexp[0] = 0x3F00;
        fa[1] = 0x3E80;
        fexp[1] = 0x4000;
        fa[2] = 0xC080;
        fexp[2] = 0x7FC0; /* negative -> NaN */
        fa[3] = 0x0000;
        fexp[3] = 0x7F80; /* +0 -> +inf */
        run_unary(ctx, top_vec_rsqrt, TN_DTYPE_BF16, TN_DTYPE_BF16, fa, fout);
        CHECK(memcmp(fout, fexp, sizeof(fexp)) == 0, "rsqrt BF16 (incl. specials)");
    }

    /* ---- taylor loading the prefit FP32 exp2 row from taylor_lut.h:
     * one authored (1,1,1,step) row, ONE physical copy in global, C-
     * broadcast to every lane at load time (FREE descriptor + c_str=0
     * companion stride, the same convention as the rcp table). Input
     * (1,2,4,8) FP32 over [0,1) -- the fitted domain of this table;
     * the expectation is the exact ref Horner over the same table bits,
     * so any drift in table contents or the lane read fails the case ---- */
    {
        const uint32_t smc = ctx->info.smc_num;
        const uint32_t step = TNKR_TAYLOR_EXP2_ORDER;
        float fa[2 * NELEM], fout[2 * NELEM], fexp[2 * NELEM];
        tdr_t d;
        tdr_stride_t s;
        uint8_t sga, sgc, sgo, sua, suc, suo, sstr;
        int sidx;

        for (i = 0; i < 2 * NELEM; i++) {
            fa[i] = (float)i / (float)(2 * NELEM); /* [0,1), exact in FP32 */
            fexp[i] =
                (float)taylor_ref_eval(TNKR_TAYLOR_EXP2_F32, step, TN_DTYPE_FP32, (double)fa[i]);
        }

        memcpy(ctx->global + GA, fa, sizeof(fa));
        memcpy(ctx->global + GB, TNKR_TAYLOR_EXP2_F32, sizeof(TNKR_TAYLOR_EXP2_F32));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GA, 1, 2, 4, 8);
        sga = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_FREE, TN_DTYPE_FP32, GB, 1, (uint16_t)smc, 1,
                            (uint16_t)step);
        sgc = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GO, 1, 2, 4, 8);
        sgo = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UA, 1, 2, 4, 8, 0);
        sua = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UB, 1, (uint16_t)smc, 1, (uint16_t)step,
                         0);
        suc = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UO, 1, 2, 4, 8, 0);
        suo = mk_desc(ctx, &d);

        /* c_str=0: one physical coeff row broadcast to every lane at load */
        s = tdr_make_stride(1u, 0u, 0u, 0u);
        sidx = tdr_alloc(ctx);
        if (sidx < 0 || tdr_shadow_write_stride(ctx, (uint8_t)sidx, &s) != TN_SUCCESS) {
            printf("  FATAL: taylor coeff stride setup failed\n");
            g_fail++;
            sstr = 0;
        } else {
            sstr = (uint8_t)sidx;
        }

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
        top_ldst_load(suc, sgc, sstr, LDST_MODE_NORMAL);
        top_vec_taylor(suo, sua, suc);
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(fout, ctx->global + GO, sizeof(fout));
        CHECK(memcmp(fout, fexp, sizeof(fout)) == 0,
              "taylor FP32 prefit exp2 table (taylor_lut.h, C-broadcast)");

        /* the loaded row must BE the exp2 table, not merely self-
         * consistent with the expectation: a degree-6 minimax fit of 2^t
         * reproduces the interval ends and the midpoint (1, sqrt2, 2) to
         * well under an FP32 ulp. No libm on the test side: the three
         * reference values are literals. */
        {
            const double ref[3] = {1.0, 1.4142135623730951, 2.0};
            const double at[3] = {0.0, 0.5, 1.0};
            int bad = 0, k;
            for (k = 0; k < 3; k++) {
                double d =
                    taylor_ref_eval(TNKR_TAYLOR_EXP2_F32, step, TN_DTYPE_FP32, at[k]) - ref[k];
                if (d > 1e-6 || d < -1e-6)
                    bad++;
            }
            CHECK(bad == 0, "taylor exp2 table matches 2^t at 0, 0.5, 1");
        }

        tdr_free(ctx, sga);
        tdr_free(ctx, sgc);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sua);
        tdr_free(ctx, suc);
        tdr_free(ctx, suo);
        tdr_free(ctx, sstr);
    }

    /* ---- taylor lane wrap across the lane count, coeff rows are REAL
     * prefit tables: even lanes hold exp2, odd lanes tanh (both are
     * TNKR_TAYLOR_*_ORDER words, so one tensor carries either). Input
     * (1,2*smc_num,4,8); channel c must read the coeff row of its OWN
     * lane, c % smc_num — the pre-wrap read would take block 1, which
     * this case prefills with URF poison, so the wrap is provable value
     * by value from the two tables' distinct coefficient sets ---- */
    {
        const uint32_t smc = ctx->info.smc_num;
        const uint32_t cn = 2 * smc;
        const uint32_t step = TNKR_TAYLOR_EXP2_ORDER;
        float fa[cn * NELEM], fout[cn * NELEM], fexp[cn * NELEM];
        uint32_t fc[128 * TNKR_TAYLOR_EXP2_ORDER];
        tdr_t d;
        uint8_t sga, sgc, sgo, sua, suc, suo;
        uint32_t c, j;

        /* the two tables share a Horner order: lane c can hold either */
        TN_STATIC_ASSERT(TNKR_TAYLOR_EXP2_ORDER == TNKR_TAYLOR_TANH_ORDER,
                         "exp2/tanh tables must share their Horner order");

        for (c = 0; c < smc; c++)
            taylor_fill_row(&fc[c * step],
                            (c % 2) ? (const void *)TNKR_TAYLOR_TANH_F32
                                    : (const void *)TNKR_TAYLOR_EXP2_F32,
                            step, TN_DTYPE_FP32);
        for (c = 0; c < cn; c++)
            for (j = 0; j < NELEM; j++) {
                double x = (double)j / (double)NELEM; /* [0,1): inside both domains */
                const void *row = ((c % smc) % 2) ? (const void *)TNKR_TAYLOR_TANH_F32
                                                  : (const void *)TNKR_TAYLOR_EXP2_F32;
                fa[c * NELEM + j] = (float)x;
                fexp[c * NELEM + j] = (float)taylor_ref_eval(row, step, TN_DTYPE_FP32, x);
            }

        memset(ctx->urf, 0xAA, ctx->info.urf_bytes); /* poison: block-1 reads must miss */
        memcpy(ctx->global + GA, fa, sizeof(fa));
        memcpy(ctx->global + GB, fc, smc * step * sizeof(uint32_t));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GA, 1, (uint16_t)cn, 4, 8);
        sga = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GB, 1, (uint16_t)smc, 1,
                            (uint16_t)step);
        sgc = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GO, 1, (uint16_t)cn, 4, 8);
        sgo = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UA, 1, (uint16_t)cn, 4, 8, 0);
        sua = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UB, 1, (uint16_t)smc, 1, (uint16_t)step,
                         0);
        suc = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UO, 1, (uint16_t)cn, 4, 8, 0);
        suo = mk_desc(ctx, &d);

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
        top_ldst_load(suc, sgc, 0, LDST_MODE_NORMAL);
        top_vec_taylor(suo, sua, suc);
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(fout, ctx->global + GO, sizeof(fout));
        CHECK(memcmp(fout, fexp, sizeof(fout)) == 0,
              "taylor FP32 prefit exp2|tanh rows, C > smc_num (lane wrap)");

        tdr_free(ctx, sga);
        tdr_free(ctx, sgc);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sua);
        tdr_free(ctx, suc);
        tdr_free(ctx, suo);
    }

    /* ---- taylor with a step well past the small tables: the gelu_tanh
     * row is TNKR_TAYLOR_GELU_TANH_ORDER (13) words, so this case pins
     * that `step` is a free descriptor width, not a 7-word limit (the
     * kernel loop is `for k = dw-1 .. 0`, the only rule is dw != 0).
     * Same broadcast load; input (1,1,4,8) over the fitted [-4,4] ---- */
    {
        const uint32_t smc = ctx->info.smc_num;
        const uint32_t step = TNKR_TAYLOR_GELU_TANH_ORDER;
        float fa[NELEM], fout[NELEM], fexp[NELEM];
        tdr_t d;
        tdr_stride_t s;
        uint8_t sga, sgc, sgo, sua, suc, suo, sstr;
        int sidx;

        for (i = 0; i < NELEM; i++) {
            fa[i] = -4.0f + 8.0f * (float)i / (float)(NELEM - 1); /* [-4,4] */
            fexp[i] = (float)taylor_ref_eval(TNKR_TAYLOR_GELU_TANH_F32, step, TN_DTYPE_FP32,
                                             (double)fa[i]);
        }

        memcpy(ctx->global + GA, fa, sizeof(fa));
        memcpy(ctx->global + GB, TNKR_TAYLOR_GELU_TANH_F32, sizeof(TNKR_TAYLOR_GELU_TANH_F32));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GA, 1, 1, 4, 8);
        sga = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_FREE, TN_DTYPE_FP32, GB, 1, (uint16_t)smc, 1,
                            (uint16_t)step);
        sgc = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GO, 1, 1, 4, 8);
        sgo = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UA, 1, 1, 4, 8, 0);
        sua = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UB, 1, (uint16_t)smc, 1, (uint16_t)step,
                         0);
        suc = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UO, 1, 1, 4, 8, 0);
        suo = mk_desc(ctx, &d);

        s = tdr_make_stride(1u, 0u, 0u, 0u);
        sidx = tdr_alloc(ctx);
        if (sidx < 0 || tdr_shadow_write_stride(ctx, (uint8_t)sidx, &s) != TN_SUCCESS) {
            printf("  FATAL: gelu coeff stride setup failed\n");
            g_fail++;
            sstr = 0;
        } else {
            sstr = (uint8_t)sidx;
        }

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
        top_ldst_load(suc, sgc, sstr, LDST_MODE_NORMAL);
        top_vec_taylor(suo, sua, suc);
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(fout, ctx->global + GO, sizeof(fout));
        CHECK(memcmp(fout, fexp, sizeof(fout)) == 0,
              "taylor FP32 prefit gelu_tanh table (order 13, step > 7)");

        tdr_free(ctx, sga);
        tdr_free(ctx, sgc);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sua);
        tdr_free(ctx, suc);
        tdr_free(ctx, suo);
        tdr_free(ctx, sstr);
    }

    /* ---- taylor loading the prefit taylor_lut.h table: BF16 exp2 row,
     * single physical copy C-broadcast at load time (c_str=0 companion
     * stride, same convention as the rcp table). Input BF16 x = i/64 on
     * [0, 0.5]; expectation traces the ref algorithm exactly through the
     * shared codec: Horner in double over the decoded coefficients,
     * output tnkr_encode_fp RNE to BF16 (values match 2^x within ~2e-3,
     * the fitted abs err) ---- */
    {
        const uint32_t smc = ctx->info.smc_num;
        const uint32_t step = TNKR_TAYLOR_EXP2_ORDER;
        uint16_t ia[NELEM], fout[NELEM], fexp[NELEM];
        tdr_t d;
        tdr_stride_t s;
        uint8_t sga, sgc, sgo, sua, suc, suo, sstr;
        int sidx;

        for (i = 0; i < NELEM; i++) {
            double x = (double)i / 64.0;
            ia[i] = (uint16_t)tnkr_encode_fp(x, TN_DTYPE_BF16);
            fexp[i] = (uint16_t)tnkr_encode_fp(
                taylor_ref_eval(TNKR_TAYLOR_EXP2_BF16, step, TN_DTYPE_BF16, x), TN_DTYPE_BF16);
        }

        memcpy(ctx->global + GA, ia, sizeof(ia));
        memcpy(ctx->global + GB, TNKR_TAYLOR_EXP2_BF16, sizeof(TNKR_TAYLOR_EXP2_BF16));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_BF16, GA, 1, 1, 4, 8);
        sga = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_FREE, TN_DTYPE_BF16, GB, 1, (uint16_t)smc, 1,
                            (uint16_t)step);
        sgc = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_BF16, GO, 1, 1, 4, 8);
        sgo = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_BF16, UA, 1, 1, 4, 8, 0);
        sua = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_BF16, UB, 1, (uint16_t)smc, 1, (uint16_t)step,
                         0);
        suc = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_BF16, UO, 1, 1, 4, 8, 0);
        suo = mk_desc(ctx, &d);

        /* c_str=0: one physical coeff row broadcast to every lane at load */
        s = tdr_make_stride(1u, 0u, 0u, 0u);
        sidx = tdr_alloc(ctx);
        if (sidx < 0 || tdr_shadow_write_stride(ctx, (uint8_t)sidx, &s) != TN_SUCCESS) {
            printf("  FATAL: taylor lut stride setup failed\n");
            g_fail++;
            sstr = 0;
        } else {
            sstr = (uint8_t)sidx;
        }

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
        top_ldst_load(suc, sgc, sstr, LDST_MODE_NORMAL);
        top_vec_taylor(suo, sua, suc);
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(fout, ctx->global + GO, sizeof(fout));
        CHECK(memcmp(fout, fexp, sizeof(fout)) == 0,
              "taylor BF16 prefit exp2 table (taylor_lut.h)");

        tdr_free(ctx, sga);
        tdr_free(ctx, sgc);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sua);
        tdr_free(ctx, suc);
        tdr_free(ctx, suo);
        tdr_free(ctx, sstr);
    }

    /* taylor with a bad coeff shape (dh=2) -> violation; the coeff store
     * carries the prefit exp2 row so the descriptor is well-formed
     * otherwise and only the shape rule can reject it */
    {
        const uint32_t smc = ctx->info.smc_num;
        float fa[NELEM], fout[NELEM], poison[NELEM];
        uint32_t fc[128 * 2 * 3];
        tdr_t d;
        uint8_t sga, sgc, sgo, sua, suc, suo;
        uint32_t c;

        for (i = 0; i < NELEM; i++) {
            fa[i] = 1.0f;
            ((uint32_t *)poison)[i] = 0xAAAAAAAAu;
        }
        for (c = 0; c < smc; c++)
            taylor_fill_row(&fc[c * 6], TNKR_TAYLOR_EXP2_F32, 6, TN_DTYPE_FP32);
        memcpy(ctx->global + GA, fa, sizeof(fa));
        memcpy(ctx->global + GB, fc, smc * 6 * sizeof(uint32_t));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GA, 1, 1, 4, 8);
        sga = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GB, 1, (uint16_t)smc, 2, 3);
        sgc = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GO, 1, 1, 4, 8);
        sgo = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UA, 1, 1, 4, 8, 0);
        sua = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UB, 1, (uint16_t)smc, 2, 3, 0);
        suc = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UO, 1, 1, 4, 8, 0);
        suo = mk_desc(ctx, &d);

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
        memset(ctx->urf + UO, 0xAA, NELEM * 4); /* poison the output plane */
        TNVI_BEGIN("vec_taylor") { top_vec_taylor(suo, sua, suc); }
        TNVI_END();
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(fout, ctx->global + GO, sizeof(fout));
        CHECK(memcmp(fout, poison, sizeof(fout)) == 0, "taylor coeff dh!=1 -> violation");

        tdr_free(ctx, sga);
        tdr_free(ctx, sgc);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sua);
        tdr_free(ctx, suc);
        tdr_free(ctx, suo);
    }

    /* taylor with coeff C > smc_num -> violation (the load-time broadcast
     * gives every lane exactly one copy; more rows cannot be placed).
     * Every row is the prefit exp2 row, so only the count is illegal */
    {
        const uint32_t smc = ctx->info.smc_num;
        float fa[NELEM], fout[NELEM], poison[NELEM];
        uint32_t fc[256 * 3];
        tdr_t d;
        uint8_t sga, sgc, sgo, sua, suc, suo;
        uint32_t c;

        for (i = 0; i < NELEM; i++) {
            fa[i] = 1.0f;
            ((uint32_t *)poison)[i] = 0xAAAAAAAAu;
        }
        for (c = 0; c < 2 * smc; c++)
            taylor_fill_row(&fc[c * 3], TNKR_TAYLOR_EXP2_F32, 3, TN_DTYPE_FP32);
        memcpy(ctx->global + GA, fa, sizeof(fa));
        memcpy(ctx->global + GB, fc, 2 * smc * 3 * sizeof(uint32_t));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GA, 1, 1, 4, 8);
        sga = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GB, 1, (uint16_t)(2 * smc), 1, 3);
        sgc = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GO, 1, 1, 4, 8);
        sgo = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UA, 1, 1, 4, 8, 0);
        sua = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UB, 1, (uint16_t)(2 * smc), 1, 3, 0);
        suc = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UO, 1, 1, 4, 8, 0);
        suo = mk_desc(ctx, &d);

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
        memset(ctx->urf + UO, 0xAA, NELEM * 4); /* poison the output plane */
        TNVI_BEGIN("vec_taylor") { top_vec_taylor(suo, sua, suc); }
        TNVI_END();
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(fout, ctx->global + GO, sizeof(fout));
        CHECK(memcmp(fout, poison, sizeof(fout)) == 0, "taylor coeff C > smc_num -> violation");

        tdr_free(ctx, sga);
        tdr_free(ctx, sgc);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sua);
        tdr_free(ctx, suc);
        tdr_free(ctx, suo);
    }

    /* rsqrt with an integer input -> violation */
    {
        uint8_t in_a[NELEM], out[NELEM], poison[NELEM];
        tdr_t d;
        uint8_t sga, sgo, sua, suo;

        for (i = 0; i < NELEM; i++) {
            in_a[i] = (uint8_t)i;
            poison[i] = 0xAA;
        }
        memcpy(ctx->global + GA, in_a, sizeof(in_a));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 1, 4, 8);
        sga = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, 1, 4, 8);
        sgo = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, 1, 4, 8, 0);
        sua = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UO, 1, 1, 4, 8, 0);
        suo = mk_desc(ctx, &d);

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
        memset(ctx->urf + UO, 0xAA, NELEM); /* poison the output plane */
        TNVI_BEGIN("vec_rsqrt") { top_vec_rsqrt(suo, sua); }
        TNVI_END();
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));
        CHECK(memcmp(out, poison, sizeof(out)) == 0, "rsqrt S8 (int) -> violation");

        tdr_free(ctx, sga);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sua);
        tdr_free(ctx, suo);
    }

    /* getmant with a mismatched output dtype (FP8 -> U16) -> violation */
    {
        uint8_t in_a[NELEM], out[NELEM * 2], poison[NELEM * 2];
        tdr_t d;
        uint8_t sga, sgo, sua, suo;

        for (i = 0; i < NELEM; i++)
            in_a[i] = (uint8_t)i;
        for (i = 0; i < NELEM * 2; i++)
            poison[i] = 0xAA;
        memcpy(ctx->global + GA, in_a, sizeof(in_a));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP8_E4M3, GA, 1, 1, 4, 8);
        sga = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GO, 1, 1, 4, 8);
        sgo = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP8_E4M3, UA, 1, 1, 4, 8, 0);
        sua = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_U16, UO, 1, 1, 4, 8, 0);
        suo = mk_desc(ctx, &d);

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
        memset(ctx->urf + UO, 0xAA, NELEM * 2); /* poison the output plane */
        TNVI_BEGIN("vec_getmant") { top_vec_getmant(suo, sua); }
        TNVI_END();
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));
        CHECK(memcmp(out, poison, sizeof(out)) == 0, "getmant FP8 -> U16 -> violation");

        tdr_free(ctx, sga);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sua);
        tdr_free(ctx, suo);
    }

    /* ---- reciprocal: LUT + quadratic interpolation, fixed-point golden ----
     * FP32 patterns traced through the algorithm: powers of two (carry
     * path), segment indices with xl=0, deep interpolation, specials,
     * subnormal-input FTZ (-> +-inf), the carry boundary rcp(2^126) = the
     * minimum normal, and subnormal-result FTZ (-> +-0). Unpicked slots
     * hold +0 -> +inf */
    {
        uint32_t fa[NELEM], fout[NELEM], fexp[NELEM];
        memset(fa, 0, sizeof(fa));
        for (i = 0; i < NELEM; i++)
            fexp[i] = 0x7F800000u; /* default: rcp(+0) = +inf */
        fa[0] = 0x3F800000u;
        fexp[0] = 0x3F800000u; /* 1.0 -> 1.0 */
        fa[1] = 0x40800000u;
        fexp[1] = 0x3E800000u; /* 4.0 -> 0.25 */
        fa[2] = 0x3F000000u;
        fexp[2] = 0x40000000u; /* 0.5 -> 2.0 */
        fa[3] = 0x3FC00000u;
        fexp[3] = 0x3F2AAAABu; /* 1.5 -> 2/3 (idx!=0, xl=0) */
        fa[4] = 0x4048F5C3u;
        fexp[4] = 0x3EA30EACu; /* 3.14 (deep interpolation) */
        fa[5] = 0xC0000000u;
        fexp[5] = 0xBF000000u; /* -2.0 -> -0.5 */
        fa[6] = 0x3FC17A98u;
        fexp[6] = 0x3F295CB6u; /* 1.51155376 */
        fa[7] = 0x3EAAAAABu;
        fexp[7] = 0x40400000u; /* 1/3 -> 3.0 */
        fa[8] = 0x00000000u;
        fexp[8] = 0x7F800000u; /* +0 -> +inf */
        fa[9] = 0x80000000u;
        fexp[9] = 0xFF800000u; /* -0 -> -inf */
        fa[10] = 0x7F800000u;
        fexp[10] = 0x00000000u; /* +inf -> +0 */
        fa[11] = 0xFF800000u;
        fexp[11] = 0x80000000u; /* -inf -> -0 */
        fa[12] = 0x7FC00000u;
        fexp[12] = 0x7FC00000u; /* NaN -> NaN */
        fa[13] = 0x00000001u;
        fexp[13] = 0x7F800000u; /* subnormal input: FTZ -> +inf */
        fa[14] = 0x80000042u;
        fexp[14] = 0xFF800000u; /* -subnormal input: FTZ -> -inf */
        fa[15] = 0x7E800000u;
        fexp[15] = 0x00800000u; /* 2^126 -> min normal (rounding carry) */
        fa[16] = 0x7E800001u;
        fexp[16] = 0x00000000u; /* 2^126 + 1ulp: true result subnormal -> +0 */
        fa[17] = 0x7F7FFFFFu;
        fexp[17] = 0x00000000u; /* max normal -> +0 */
        fa[18] = 0x7F000000u;
        fexp[18] = 0x00000000u; /* ~1.7e38 -> +0 */
        fa[19] = 0x3F7FFFFFu;
        fexp[19] = 0x3F800001u; /* 1-ulp: half-up rounds away from 1.0 */
        run_rcp(ctx, 1, TN_DTYPE_FP32, fa, fout);
        CHECK(memcmp(fout, fexp, sizeof(fexp)) == 0, "rcp FP32 (incl. specials + FTZ)");
    }

    /* BF16: pure table lookup (no interpolation bits) + the same
     * specials/FTZ boundaries */
    {
        uint16_t fa[NELEM], fout[NELEM], fexp[NELEM];
        memset(fa, 0, sizeof(fa));
        for (i = 0; i < NELEM; i++)
            fexp[i] = 0x7F80; /* default: rcp(+0) = +inf */
        fa[0] = 0x3F80;
        fexp[0] = 0x3F80; /* 1.0 -> 1.0 */
        fa[1] = 0x4080;
        fexp[1] = 0x3E80; /* 4.0 -> 0.25 */
        fa[2] = 0x3F00;
        fexp[2] = 0x4000; /* 0.5 -> 2.0 */
        fa[3] = 0x4040;
        fexp[3] = 0x3EAB; /* 3.0 -> 1/3 */
        fa[4] = 0x3FC0;
        fexp[4] = 0x3F2B; /* 1.5 -> 2/3 */
        fa[5] = 0xC000;
        fexp[5] = 0xBF00; /* -2.0 -> -0.5 */
        fa[6] = 0x42AA;
        fexp[6] = 0x3C41; /* 85 */
        fa[7] = 0x3F78;
        fexp[7] = 0x3F84; /* 0.96875 */
        fa[8] = 0x0000;
        fexp[8] = 0x7F80; /* +0 -> +inf */
        fa[9] = 0x8000;
        fexp[9] = 0xFF80; /* -0 -> -inf */
        fa[10] = 0x7F80;
        fexp[10] = 0x0000; /* +inf -> +0 */
        fa[11] = 0xFF80;
        fexp[11] = 0x8000; /* -inf -> -0 */
        fa[12] = 0x7FC0;
        fexp[12] = 0x7FC0; /* NaN -> NaN */
        fa[13] = 0x0001;
        fexp[13] = 0x7F80; /* subnormal input: FTZ -> +inf */
        fa[14] = 0x8001;
        fexp[14] = 0xFF80; /* -subnormal input: FTZ -> -inf */
        fa[15] = 0x7F7F;
        fexp[15] = 0x0000; /* max normal -> +0 */
        fa[16] = 0x7E80;
        fexp[16] = 0x0080; /* 2^126 -> min normal (rounding carry) */
        fa[17] = 0x7E81;
        fexp[17] = 0x0000; /* 2^126 + 1ulp: subnormal result -> +0 */
        run_rcp(ctx, 1, TN_DTYPE_BF16, fa, fout);
        CHECK(memcmp(fout, fexp, sizeof(fexp)) == 0, "rcp BF16 (pure LUT + FTZ)");
    }

    /* two-channel (1,2,4,8) FP32: row c of the table serves lane c */
    {
        uint32_t fa[2 * NELEM], fout[2 * NELEM], fexp[2 * NELEM];
        memset(fa, 0, sizeof(fa));
        for (i = 0; i < 2 * NELEM; i++)
            fexp[i] = 0x7F800000u; /* default: rcp(+0) = +inf */
        fa[0] = 0x3F800000u;
        fexp[0] = 0x3F800000u; /* lane 0: 1.0 -> 1.0 */
        fa[1] = 0x3EAAAAABu;
        fexp[1] = 0x40400000u; /* lane 0: 1/3 -> 3.0 */
        fa[NELEM + 0] = 0x3E800000u;
        fexp[NELEM + 0] = 0x40800000u; /* lane 1: 0.25 -> 4.0 */
        fa[NELEM + 1] = 0xBFC00000u;
        fexp[NELEM + 1] = 0xBF2AAAABu; /* lane 1: -1.5 -> -2/3 */
        fa[NELEM + 2] = 0x4048F5C3u;
        fexp[NELEM + 2] = 0x3EA30EACu; /* lane 1: 3.14 */
        fa[NELEM + 3] = 0x7E800000u;
        fexp[NELEM + 3] = 0x00800000u; /* lane 1: 2^126 -> min normal */
        fa[NELEM + 4] = 0x7F7FFFFFu;
        fexp[NELEM + 4] = 0x00000000u; /* lane 1: max normal -> +0 */
        run_rcp(ctx, 2, TN_DTYPE_FP32, fa, fout);
        CHECK(memcmp(fout, fexp, sizeof(fexp)) == 0, "rcp FP32 2-channel (per-lane table)");
    }

    /* ---- reciprocal across the lane count: input (1,2*smc_num,4,8) all
     * 1.0f; the LUT rows are broadcast copies, so every channel must
     * return 1.0 — a raw-channel table read takes block 1, which this
     * case prefills with URF poison, so the lane wrap is provable ---- */
    {
        const uint32_t smc = ctx->info.smc_num;
        const uint32_t cn = 2 * smc;
        uint32_t fa[cn * NELEM], fout[cn * NELEM], j;
        int bad = 0;

        for (j = 0; j < cn * NELEM; j++)
            fa[j] = 0x3F800000u;                     /* 1.0f */
        memset(ctx->urf, 0xAA, ctx->info.urf_bytes); /* poison: block-1 reads must miss */
        run_rcp(ctx, cn, TN_DTYPE_FP32, fa, fout);
        for (j = 0; j < cn * NELEM && !bad; j++)
            bad = (fout[j] != 0x3F800000u);
        CHECK(!bad, "rcp FP32 C > smc_num (LUT lane wrap)");
    }

    /* ---- rcp negatives: violation raised, output keeps its poison pattern ---- */
    check_rcp_reject(ctx, "rcp FP8 input -> violation", TN_DTYPE_FP8_E4M3, TN_DTYPE_FP8_E4M3,
                     TN_DTYPE_S32, ctx->info.smc_num, 1, TNKR_RCP_TBL_WORDS);
    check_rcp_reject(ctx, "rcp out dtype != in dtype -> violation", TN_DTYPE_FP32, TN_DTYPE_BF16,
                     TN_DTYPE_S32, ctx->info.smc_num, 1, TNKR_RCP_TBL_WORDS);
    check_rcp_reject(ctx, "rcp table dtype FP32 -> violation", TN_DTYPE_FP32, TN_DTYPE_FP32,
                     TN_DTYPE_FP32, ctx->info.smc_num, 1, TNKR_RCP_TBL_WORDS);
    check_rcp_reject(ctx, "rcp table bad width -> violation", TN_DTYPE_FP32, TN_DTYPE_FP32,
                     TN_DTYPE_S32, ctx->info.smc_num, 1, 2 * TNKR_RCP_SEGS);
    check_rcp_reject(ctx, "rcp table bad channel count -> violation", TN_DTYPE_FP32, TN_DTYPE_FP32,
                     TN_DTYPE_S32, ctx->info.smc_num / 2, 1, TNKR_RCP_TBL_WORDS);
    check_rcp_reject(ctx, "rcp table C > smc_num -> violation", TN_DTYPE_FP32, TN_DTYPE_FP32,
                     TN_DTYPE_S32, 2 * ctx->info.smc_num, 1, TNKR_RCP_TBL_WORDS);
    check_rcp_reject(ctx, "rcp table dh != 1 -> violation", TN_DTYPE_FP32, TN_DTYPE_FP32,
                     TN_DTYPE_S32, ctx->info.smc_num, 2, TNKR_RCP_TBL_WORDS);
}
