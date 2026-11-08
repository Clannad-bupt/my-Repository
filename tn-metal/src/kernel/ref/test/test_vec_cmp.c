// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/test/test_vec_cmp.c
 *
 * top/vec compare + compare-select cases: cmp gt/lt/le/eq/ge (integer 0/1,
 * FP 0.0/1.0, CONSTANT operand) and csel (pick left/right result per the
 * comparison), plus violation negatives for dtype mismatch,
 * left-Constant and Constant result operands.
 * Expectations are computed independently on the test side.
 * ========================================================================== */
#include "smoke.h"

static void op_cmp_gt(tdr_idx_t o, tdr_idx_t l, tdr_idx_t r, ovf_mode_t m) {
    (void)m;
    top_vec_cmp_gt(o, l, r);
}
static void op_cmp_lt(tdr_idx_t o, tdr_idx_t l, tdr_idx_t r, ovf_mode_t m) {
    (void)m;
    top_vec_cmp_lt(o, l, r);
}
static void op_cmp_le(tdr_idx_t o, tdr_idx_t l, tdr_idx_t r, ovf_mode_t m) {
    (void)m;
    top_vec_cmp_le(o, l, r);
}
static void op_cmp_eq(tdr_idx_t o, tdr_idx_t l, tdr_idx_t r, ovf_mode_t m) {
    (void)m;
    top_vec_cmp_eq(o, l, r);
}
static void op_cmp_ge(tdr_idx_t o, tdr_idx_t l, tdr_idx_t r, ovf_mode_t m) {
    (void)m;
    top_vec_cmp_ge(o, l, r);
}

typedef void (*csel_fn)(tdr_idx_t, tdr_idx_t, tdr_idx_t, tdr_idx_t, tdr_idx_t);

/* Integer cmp case: width-masked patterns in, 0/1 expectations compared
 * byte by byte */
static void check_cmp_int(tnkr_ctx_t *ctx, const char *name, binop_fn fn, char op, tn_dtype_t dt,
                          const int64_t *A, const int64_t *B) {
    uint8_t in_a[NELEM * 4], in_b[NELEM * 4], out[NELEM * 4], expct[NELEM * 4];
    unsigned sz = tn_dtype_size(dt);
    int64_t imask = (1LL << tn_dtype_bits(dt)) - 1;
    int isgn = (dt == TN_DTYPE_S8 || dt == TN_DTYPE_S16 || dt == TN_DTYPE_S32);
    uint32_t i, e;
    int64_t am, bm;

    memset(in_a, 0, sizeof(in_a));
    memset(in_b, 0, sizeof(in_b));
    memset(expct, 0, sizeof(expct));
    for (i = 0; i < NELEM; i++) {
        am = A[i] & imask; /* wrap to width: matches the in-memory pattern */
        bm = B[i] & imask;
        if (isgn) { /* decode sign */
            int64_t sbit = 1LL << (tn_dtype_bits(dt) - 1);
            if (am & sbit)
                am |= ~imask;
            if (bm & sbit)
                bm |= ~imask;
        }
        switch (op) {
        case '>':
            e = am > bm;
            break;
        case 't':
            e = am < bm;
            break;
        case 'l':
            e = am <= bm;
            break;
        case 'e':
            e = am == bm;
            break;
        default:
            e = am >= bm;
            break;
        }
        memcpy(in_a + i * sz, &am, sz);
        memcpy(in_b + i * sz, &bm, sz);
        memcpy(expct + i * sz, &e, sz);
    }
    run_binop(ctx, fn, dt, dt, OVF_MODE_SATURATE, in_a, in_b, out);
    CHECK(memcmp(out, expct, NELEM * sz) == 0, name);
}

/* Standard csel case: A/B (compare) and P/Q (results) all loop through
 * global->URF->op->global */
static void run_csel(tnkr_ctx_t *ctx, csel_fn fn, tn_dtype_t dt, const void *A, const void *B,
                     const void *P, const void *Q, void *OUT) {
    size_t sz = NELEM * tn_dtype_size(dt);
    tdr_t d;
    uint8_t sga, sgb, sgp, sgq, sgo, sua, sub_, sup, suq, suo;

    memcpy(ctx->global + GA, A, sz);
    memcpy(ctx->global + GB, B, sz);
    memcpy(ctx->global + GO, Q, sz); /* Q rides the output region's tail */
    memcpy(ctx->global + GB + 4096, P, sz);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, dt, GA, 1, 1, 4, 8);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, dt, GB, 1, 1, 4, 8);
    sgb = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, dt, GB + 4096, 1, 1, 4, 8);
    sgp = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, dt, GO, 1, 1, 4, 8);
    sgq = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, dt, GO + 4096, 1, 1, 4, 8);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, dt, UA, 1, 1, 4, 8, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, dt, UB, 1, 1, 4, 8, 0);
    sub_ = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, dt, UA + 1024, 1, 1, 4, 8, 0);
    sup = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, dt, UB + 1024, 1, 1, 4, 8, 0);
    suq = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, dt, UO, 1, 1, 4, 8, 0);
    suo = mk_desc(ctx, &d);

    top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
    top_ldst_load(sub_, sgb, 0, LDST_MODE_NORMAL);
    top_ldst_load(sup, sgp, 0, LDST_MODE_NORMAL);
    top_ldst_load(suq, sgq, 0, LDST_MODE_NORMAL);
    fn(suo, sua, sub_, sup, suq);
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(OUT, ctx->global + GO + 4096, sz);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgb);
    tdr_free(ctx, sgp);
    tdr_free(ctx, sgq);
    tdr_free(ctx, sgo);
    tdr_free(ctx, sua);
    tdr_free(ctx, sub_);
    tdr_free(ctx, sup);
    tdr_free(ctx, suq);
    tdr_free(ctx, suo);
}

void suite_vec_cmp(tnkr_ctx_t *ctx) {
    int64_t A[NELEM], B[NELEM];
    uint32_t i;

    printf("== vec_cmp ==\n");

    /* ---- integer compare: 0/1 output ---- */
    for (i = 0; i < NELEM; i++) {
        A[i] = (int64_t)i - 16;
        B[i] = (int64_t)(i % 7) - 3;
    }
    B[0] = A[0]; /* an equal pair for eq/ge/le */
    check_cmp_int(ctx, "cmp_gt S8", op_cmp_gt, '>', TN_DTYPE_S8, A, B);
    check_cmp_int(ctx, "cmp_lt S16", op_cmp_lt, 't', TN_DTYPE_S16, A, B);
    check_cmp_int(ctx, "cmp_le S16", op_cmp_le, 'l', TN_DTYPE_S16, A, B);
    check_cmp_int(ctx, "cmp_eq U8", op_cmp_eq, 'e', TN_DTYPE_U8, A, B);
    check_cmp_int(ctx, "cmp_ge S8", op_cmp_ge, 'g', TN_DTYPE_S8, A, B);

    /* ---- FP32 compare: 0.0/1.0 output ---- */
    {
        float fa[NELEM], fb[NELEM], fout[NELEM], fexp[NELEM];
        for (i = 0; i < NELEM; i++) {
            fa[i] = (float)((int)i - 16) * 0.5f;
            fb[i] = (float)(i % 5) - 1.0f;
            fexp[i] = fa[i] > fb[i] ? 1.0f : 0.0f;
        }
        fb[0] = fa[0]; /* equal pair */
        fexp[0] = 0.0f;
        run_binop(ctx, op_cmp_gt, TN_DTYPE_FP32, TN_DTYPE_FP32, OVF_MODE_SATURATE, fa, fb, fout);
        CHECK(memcmp(fout, fexp, sizeof(fout)) == 0, "cmp_gt FP32 (0.0/1.0)");
    }

    /* ---- CONSTANT right operand: cmp_eq against 5 (S8) ---- */
    {
        uint8_t in_a[NELEM], out[NELEM], expct[NELEM];
        int64_t a64;
        tdr_t d;
        uint8_t sga, sua, suo, sc, sgo;

        for (i = 0; i < NELEM; i++) {
            a64 = (int64_t)i - 16;
            in_a[i] = (uint8_t)(int8_t)a64;
            expct[i] = (a64 == 5) ? 1 : 0; /* i=21 hits the equal case */
        }
        memcpy(ctx->global + GA, in_a, sizeof(in_a));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 1, 4, 8);
        sga = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, 1, 4, 8, 0);
        sua = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UO, 1, 1, 4, 8, 0);
        suo = mk_desc(ctx, &d);
        d = tdr_make_urf_constant(TN_DTYPE_S8, 5);
        sc = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, 1, 4, 8);
        sgo = mk_desc(ctx, &d);

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
        top_vec_cmp_eq(suo, sua, sc);
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));
        CHECK(memcmp(out, expct, sizeof(out)) == 0, "cmp_eq S8 + CONSTANT(5)");

        tdr_free(ctx, sga);
        tdr_free(ctx, sua);
        tdr_free(ctx, suo);
        tdr_free(ctx, sc);
        tdr_free(ctx, sgo);
    }

    /* ==================================================================
     * csel: out = (A OP B) ? P : Q
     * ================================================================== */
    printf("== vec_csel ==\n");

    /* S8: gt select */
    {
        int8_t pa[NELEM], qa[NELEM];
        uint8_t in_a[NELEM], in_b[NELEM], in_p[NELEM], in_q[NELEM], out[NELEM], expct[NELEM];
        for (i = 0; i < NELEM; i++) {
            pa[i] = (int8_t)(100 - (int)i);
            qa[i] = (int8_t)(int)(i * 3 - 60);
            in_a[i] = (uint8_t)(int8_t)A[i];
            in_b[i] = (uint8_t)(int8_t)B[i];
            in_p[i] = (uint8_t)pa[i];
            in_q[i] = (uint8_t)qa[i];
            expct[i] = (A[i] > B[i]) ? in_p[i] : in_q[i];
        }
        run_csel(ctx, top_vec_cmp_select_gt, TN_DTYPE_S8, in_a, in_b, in_p, in_q, out);
        CHECK(memcmp(out, expct, sizeof(out)) == 0, "csel_gt S8");
    }

    /* FP32: le select */
    {
        float fa[NELEM], fb[NELEM], fp[NELEM], fq[NELEM], fout[NELEM], fexp[NELEM];
        for (i = 0; i < NELEM; i++) {
            fa[i] = (float)((int)i - 16) * 0.5f;
            fb[i] = (float)(i % 5) - 1.0f;
            fp[i] = (float)i + 0.25f;
            fq[i] = -(float)i - 0.5f;
            fexp[i] = (fa[i] <= fb[i]) ? fp[i] : fq[i];
        }
        run_csel(ctx, top_vec_cmp_select_le, TN_DTYPE_FP32, fa, fb, fp, fq, fout);
        CHECK(memcmp(fout, fexp, sizeof(fout)) == 0, "csel_le FP32");
    }

    /* S16: lt select */
    {
        int16_t pa[NELEM], qa[NELEM];
        uint16_t in_a[NELEM], in_b[NELEM], in_p[NELEM], in_q[NELEM], out[NELEM], expct[NELEM];
        for (i = 0; i < NELEM; i++) {
            pa[i] = (int16_t)(100 - (int)i);
            qa[i] = (int16_t)(int)(i * 3 - 60);
            in_a[i] = (uint16_t)(int16_t)A[i];
            in_b[i] = (uint16_t)(int16_t)B[i];
            in_p[i] = (uint16_t)pa[i];
            in_q[i] = (uint16_t)qa[i];
            expct[i] = (A[i] < B[i]) ? in_p[i] : in_q[i];
        }
        run_csel(ctx, top_vec_cmp_select_lt, TN_DTYPE_S16, in_a, in_b, in_p, in_q, out);
        CHECK(memcmp(out, expct, sizeof(out)) == 0, "csel_lt S16");
    }

    /* ---- negatives: violation raised, output keeps its poison pattern ---- */

    /* cmp with mismatched output dtype */
    {
        uint8_t in_a[NELEM], in_b[NELEM], out[NELEM * 2], poison[NELEM * 2];
        tdr_t d;
        uint8_t sga, sgb, sgo, sua, sub_, suo;

        for (i = 0; i < NELEM; i++) {
            in_a[i] = (uint8_t)i;
            in_b[i] = (uint8_t)(i + 1u);
        }
        for (i = 0; i < NELEM * 2; i++)
            poison[i] = 0xAA;
        memcpy(ctx->global + GA, in_a, sizeof(in_a));
        memcpy(ctx->global + GB, in_b, sizeof(in_b));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 1, 4, 8);
        sga = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GB, 1, 1, 4, 8);
        sgb = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GO, 1, 1, 4, 8);
        sgo = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, 1, 4, 8, 0);
        sua = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UB, 1, 1, 4, 8, 0);
        sub_ = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S16, UO, 1, 1, 4, 8, 0);
        suo = mk_desc(ctx, &d);

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
        top_ldst_load(sub_, sgb, 0, LDST_MODE_NORMAL);
        memset(ctx->urf + UO, 0xAA, NELEM * 2); /* poison the output plane */
        TNVI_BEGIN("vec_cmp") { top_vec_cmp_gt(suo, sua, sub_); }
        TNVI_END();
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));
        CHECK(memcmp(out, poison, sizeof(out)) == 0, "cmp out dtype != in -> violation");

        tdr_free(ctx, sga);
        tdr_free(ctx, sgb);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sua);
        tdr_free(ctx, sub_);
        tdr_free(ctx, suo);
    }

    /* csel with a Constant result operand */
    {
        uint8_t in_a[NELEM], in_b[NELEM], out[NELEM], poison[NELEM];
        tdr_t d;
        uint8_t sga, sgb, sgo, sua, sub_, sc, suq, suo;

        for (i = 0; i < NELEM; i++) {
            in_a[i] = (uint8_t)i;
            in_b[i] = (uint8_t)(i + 1u);
            poison[i] = 0xAA;
        }
        memcpy(ctx->global + GA, in_a, sizeof(in_a));
        memcpy(ctx->global + GB, in_b, sizeof(in_b));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 1, 4, 8);
        sga = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GB, 1, 1, 4, 8);
        sgb = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, 1, 4, 8);
        sgo = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, 1, 4, 8, 0);
        sua = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UB, 1, 1, 4, 8, 0);
        sub_ = mk_desc(ctx, &d);
        d = tdr_make_urf_constant(TN_DTYPE_S8, 0x55);
        sc = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UB + 1024, 1, 1, 4, 8, 0);
        suq = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UO, 1, 1, 4, 8, 0);
        suo = mk_desc(ctx, &d);

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
        top_ldst_load(sub_, sgb, 0, LDST_MODE_NORMAL);
        memset(ctx->urf + UO, 0xAA, NELEM); /* poison the output plane */
        TNVI_BEGIN("vec_csel") { top_vec_cmp_select_gt(suo, sua, sub_, sc, suq); }
        TNVI_END();
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));
        CHECK(memcmp(out, poison, sizeof(out)) == 0, "csel Constant result -> violation");

        tdr_free(ctx, sga);
        tdr_free(ctx, sgb);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sua);
        tdr_free(ctx, sub_);
        tdr_free(ctx, sc);
        tdr_free(ctx, suq);
        tdr_free(ctx, suo);
    }

    /* cmp with a Constant left operand: Constant goes on right (opd1)
     * only (kernel.h) -> violation before any write, poison survives */
    {
        uint8_t in_a[NELEM], out[NELEM], poison[NELEM];
        tdr_t d;
        uint8_t sga, sua, sc, sgo, suo;

        for (i = 0; i < NELEM; i++) {
            in_a[i] = (uint8_t)i;
            poison[i] = 0xAA;
        }
        memcpy(ctx->global + GA, in_a, sizeof(in_a));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 1, 4, 8);
        sga = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, 1, 4, 8, 0);
        sua = mk_desc(ctx, &d);
        d = tdr_make_urf_constant(TN_DTYPE_S8, 5);
        sc = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UO, 1, 1, 4, 8, 0);
        suo = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, 1, 4, 8);
        sgo = mk_desc(ctx, &d);

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
        memset(ctx->urf + UO, 0xAA, NELEM); /* poison the output plane */
        TNVI_BEGIN("vec_cmp") { top_vec_cmp_eq(suo, sc, sua); }
        TNVI_END();
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));
        CHECK(memcmp(out, poison, sizeof(out)) == 0, "cmp left Constant -> violation");

        tdr_free(ctx, sga);
        tdr_free(ctx, sua);
        tdr_free(ctx, sc);
        tdr_free(ctx, suo);
        tdr_free(ctx, sgo);
    }

    /* csel with a Constant left compare operand: the same placement
     * rule (kernel.h) -> violation before any write, poison survives */
    {
        uint8_t in_b[NELEM], out[NELEM], poison[NELEM];
        tdr_t d;
        uint8_t sgb, sgo, sub_, sc, sua, suq, suo;

        for (i = 0; i < NELEM; i++) {
            in_b[i] = (uint8_t)(i + 1u);
            poison[i] = 0xAA;
        }
        memcpy(ctx->global + GB, in_b, sizeof(in_b));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GB, 1, 1, 4, 8);
        sgb = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, 1, 4, 8);
        sgo = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UB, 1, 1, 4, 8, 0);
        sub_ = mk_desc(ctx, &d);
        d = tdr_make_urf_constant(TN_DTYPE_S8, 0x55);
        sc = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, 1, 4, 8, 0);
        sua = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UB + 1024, 1, 1, 4, 8, 0);
        suq = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UO, 1, 1, 4, 8, 0);
        suo = mk_desc(ctx, &d);

        top_ldst_load(sub_, sgb, 0, LDST_MODE_NORMAL);
        memset(ctx->urf + UO, 0xAA, NELEM); /* poison the output plane */
        TNVI_BEGIN("vec_csel") { top_vec_cmp_select_gt(suo, sc, sub_, sua, suq); }
        TNVI_END();
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));
        CHECK(memcmp(out, poison, sizeof(out)) == 0, "csel left Constant -> violation");

        tdr_free(ctx, sgb);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sub_);
        tdr_free(ctx, sc);
        tdr_free(ctx, sua);
        tdr_free(ctx, suq);
        tdr_free(ctx, suo);
    }
}
