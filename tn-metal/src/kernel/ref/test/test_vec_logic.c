// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/test/test_vec_logic.c
 *
 * top/vec logic family cases: and / or / xor / nxor / not
 * (multi-width integers, CONSTANT on right only, ZEROSTRIDE broadcast,
 * plus violation negatives for FP input, left-Constant and non-HWAlign
 * unary input). Expectations are bitwise, computed independently on the
 * test side.
 * ========================================================================== */
#include "smoke.h"

/* Test-side independent bitwise expectation, masked to the dtype width */
static uint32_t expect_bits(char op, int64_t a, int64_t b, unsigned bits) {
    uint32_t mask = (bits >= 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    uint32_t x = (uint32_t)a & mask;
    uint32_t y = (uint32_t)b & mask;
    uint32_t v;

    switch (op) {
    case '&':
        v = x & y;
        break;
    case '|':
        v = x | y;
        break;
    case '^':
        v = x ^ y;
        break;
    default:
        v = ~(x ^ y); /* nxor */
        break;
    }
    return v & mask;
}

/* Standard binary logic case: width-masked patterns in, per-element
 * independent expectations compared byte by byte */
static void check_logic(tnkr_ctx_t *ctx, const char *name, binop_fn fn, char op, tn_dtype_t dt,
                        const int64_t *A, const int64_t *B) {
    uint8_t in_a[NELEM * 4], in_b[NELEM * 4], out[NELEM * 4], expct[NELEM * 4];
    unsigned sz = tn_dtype_size(dt);
    unsigned bits = tn_dtype_bits(dt);
    int64_t imask = (1LL << bits) - 1;
    uint32_t i, e;
    int64_t am, bm;

    memset(in_a, 0, sizeof(in_a));
    memset(in_b, 0, sizeof(in_b));
    memset(expct, 0, sizeof(expct));
    for (i = 0; i < NELEM; i++) {
        am = A[i] & imask; /* wrap to width: matches the in-memory pattern */
        bm = B[i] & imask;
        e = expect_bits(op, am, bm, bits);
        memcpy(in_a + i * sz, &am, sz);
        memcpy(in_b + i * sz, &bm, sz);
        memcpy(expct + i * sz, &e, sz);
    }
    run_binop(ctx, fn, dt, dt, OVF_MODE_SATURATE, in_a, in_b, out);
    CHECK(memcmp(out, expct, NELEM * sz) == 0, name);
}

/* Unary not case via the shared run_unary loop */
static void check_not(tnkr_ctx_t *ctx, const char *name, tn_dtype_t dt, const int64_t *A) {
    uint8_t in_a[NELEM * 4], out[NELEM * 4], expct[NELEM * 4];
    unsigned sz = tn_dtype_size(dt);
    unsigned bits = tn_dtype_bits(dt);
    uint32_t mask = (bits >= 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    uint32_t i, e;
    int64_t am;

    memset(in_a, 0, sizeof(in_a));
    memset(expct, 0, sizeof(expct));
    for (i = 0; i < NELEM; i++) {
        am = A[i] & ((int64_t)mask);
        e = ~((uint32_t)am) & mask;
        memcpy(in_a + i * sz, &am, sz);
        memcpy(expct + i * sz, &e, sz);
    }
    run_unary(ctx, top_vec_not, dt, dt, in_a, out);
    CHECK(memcmp(out, expct, NELEM * sz) == 0, name);
}

void suite_vec_logic(tnkr_ctx_t *ctx) {
    int64_t A[NELEM], B[NELEM];
    uint32_t i;

    printf("== vec_logic ==\n");

    /* ---- binary: multi-width coverage ---- */
    for (i = 0; i < NELEM; i++) {
        A[i] = (int64_t)(i * 37u + 5u) - 600; /* signed spread */
        B[i] = (int64_t)(i * 11u + 3u) - 170;
    }
    check_logic(ctx, "and S8", op_and, '&', TN_DTYPE_S8, A, B);
    check_logic(ctx, "or U8", op_or, '|', TN_DTYPE_U8, A, B);
    check_logic(ctx, "xor S16", op_xor, '^', TN_DTYPE_S16, A, B);
    check_logic(ctx, "nxor S8", op_nxor, 'n', TN_DTYPE_S8, A, B);
    check_logic(ctx, "and S32", op_and, '&', TN_DTYPE_S32, A, B);

    /* ---- CONSTANT operand: or with constant 0x0F (S8) ---- */
    {
        uint8_t in_a[NELEM], out[NELEM], expct[NELEM];
        int64_t a64;
        tdr_t d;
        uint8_t sga, sua, suo, sc, sgo;

        for (i = 0; i < NELEM; i++) {
            a64 = A[i] & 0xFF;
            in_a[i] = (uint8_t)a64;
            expct[i] = (uint8_t)(a64 | 0x0F);
        }
        memcpy(ctx->global + GA, in_a, sizeof(in_a));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 1, 4, 8);
        sga = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, 1, 4, 8, 0);
        sua = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UO, 1, 1, 4, 8, 0);
        suo = mk_desc(ctx, &d);
        d = tdr_make_urf_constant(TN_DTYPE_S8, 0x0F);
        sc = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, 1, 4, 8);
        sgo = mk_desc(ctx, &d);

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
        top_vec_or(suo, sua, sc);
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));
        CHECK(memcmp(out, expct, sizeof(out)) == 0, "or S8 + CONSTANT(0x0F)");

        tdr_free(ctx, sga);
        tdr_free(ctx, sua);
        tdr_free(ctx, suo);
        tdr_free(ctx, sc);
        tdr_free(ctx, sgo);
    }

    /* ---- ZEROSTRIDE broadcast (N/H dims): B stored (1,1,1,8), staged via
     * a same-address HWALIGN alias (ldst rejects ZEROSTRIDE on URF);
     * expect out(h,w) = a(h,w) ^ bvec[w] ---- */
    {
        uint8_t in_a[NELEM], in_b[8], out[NELEM], expct[NELEM];
        int64_t a64;
        tdr_t d;
        uint8_t sga, sgb, sgo, sua, sub_ld, sub_, suo;

        for (i = 0; i < NELEM; i++)
            in_a[i] = (uint8_t)(A[i] & 0xFF);
        for (i = 0; i < 8; i++)
            in_b[i] = (uint8_t)(i * 29u + 7u); /* 8-element broadcast vector */
        memcpy(ctx->global + GA, in_a, sizeof(in_a));
        memcpy(ctx->global + GB, in_b, sizeof(in_b));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 1, 4, 8);
        sga = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GB, 1, 1, 1, 8);
        sgb = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, 1, 4, 8);
        sgo = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, 1, 4, 8, 0);
        sua = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UB, 1, 1, 1, 8, 0);
        sub_ld = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_ZEROSTRIDE, TN_DTYPE_S8, UB, 1, 1, 1, 8,
                         TDR_BCAST_NB | TDR_BCAST_HB);
        sub_ = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UO, 1, 1, 4, 8, 0);
        suo = mk_desc(ctx, &d);

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
        top_ldst_load(sub_ld, sgb, 0, LDST_MODE_NORMAL);
        top_vec_xor(suo, sua, sub_);
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));

        for (i = 0; i < NELEM; i++) {
            a64 = A[i] & 0xFF;
            expct[i] = (uint8_t)((uint32_t)a64 ^ (uint32_t)in_b[i % 8]);
        }
        CHECK(memcmp(out, expct, sizeof(out)) == 0, "xor S8 + ZEROSTRIDE bcast vector");

        tdr_free(ctx, sga);
        tdr_free(ctx, sgb);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sua);
        tdr_free(ctx, sub_ld);
        tdr_free(ctx, sub_);
        tdr_free(ctx, suo);
    }

    /* ---- unary not ---- */
    check_not(ctx, "not S8", TN_DTYPE_S8, A);
    check_not(ctx, "not U8", TN_DTYPE_U8, A);

    /* ---- negatives: violation raised, output keeps its poison pattern ---- */

    /* FP input: logic is integers only */
    {
        uint8_t in_a[NELEM], in_b[NELEM], out[NELEM], poison[NELEM];
        tdr_t d;
        uint8_t sga, sgb, sgo, sua, sub_, suo;

        for (i = 0; i < NELEM; i++) {
            in_a[i] = (uint8_t)i;
            in_b[i] = (uint8_t)(i + 1u);
            poison[i] = 0xAA;
        }
        memcpy(ctx->global + GA, in_a, sizeof(in_a));
        memcpy(ctx->global + GB, in_b, sizeof(in_b));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP8_E4M3, GA, 1, 1, 4, 8);
        sga = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP8_E4M3, GB, 1, 1, 4, 8);
        sgb = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP8_E4M3, GO, 1, 1, 4, 8);
        sgo = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP8_E4M3, UA, 1, 1, 4, 8, 0);
        sua = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP8_E4M3, UB, 1, 1, 4, 8, 0);
        sub_ = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP8_E4M3, UO, 1, 1, 4, 8, 0);
        suo = mk_desc(ctx, &d);

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
        top_ldst_load(sub_, sgb, 0, LDST_MODE_NORMAL);
        memset(ctx->urf + UO, 0xAA, NELEM); /* poison the output plane */
        TNVI_BEGIN("vec_and:") { top_vec_and(suo, sua, sub_); }
        TNVI_END();
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));
        CHECK(memcmp(out, poison, sizeof(out)) == 0, "and FP8 -> violation");

        tdr_free(ctx, sga);
        tdr_free(ctx, sgb);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sua);
        tdr_free(ctx, sub_);
        tdr_free(ctx, suo);
    }

    /* Constant on the left operand: Constant goes on right (opd1) only
     * (kernel.h) -> violation before any write, the poisoned output
     * survives */
    {
        uint8_t in_a[NELEM], out[NELEM], poison[NELEM];
        tdr_t d;
        uint8_t sga, sua, sc, sgo, suo;

        for (i = 0; i < NELEM; i++) {
            in_a[i] = (uint8_t)(A[i] & 0xFF);
            poison[i] = 0xAA;
        }
        memcpy(ctx->global + GA, in_a, sizeof(in_a));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 1, 4, 8);
        sga = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, 1, 4, 8, 0);
        sua = mk_desc(ctx, &d);
        d = tdr_make_urf_constant(TN_DTYPE_S8, 0x55);
        sc = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UO, 1, 1, 4, 8, 0);
        suo = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, 1, 4, 8);
        sgo = mk_desc(ctx, &d);

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
        memset(ctx->urf + UO, 0xAA, NELEM); /* poison the output plane */
        TNVI_BEGIN("vec_or:") { top_vec_or(suo, sc, sua); }
        TNVI_END();
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));
        CHECK(memcmp(out, poison, sizeof(out)) == 0, "or left Constant -> violation");

        tdr_free(ctx, sga);
        tdr_free(ctx, sua);
        tdr_free(ctx, sc);
        tdr_free(ctx, suo);
        tdr_free(ctx, sgo);
    }

    /* not with a ZEROSTRIDE input: unary requires HWAlign (kernel.h) */
    {
        uint8_t in_a[NELEM], out[NELEM], poison[NELEM];
        tdr_t d;
        uint8_t sga, sgo, sua_ld, sua_zs, suo;

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
        sua_ld = mk_desc(ctx, &d);
        /* same-address ZEROSTRIDE view: N/H broadcast, extent legal */
        d = tdr_make_urf(TDR_LAYOUT_ZEROSTRIDE, TN_DTYPE_S8, UA, 1, 1, 1, 8,
                         TDR_BCAST_NB | TDR_BCAST_HB);
        sua_zs = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UO, 1, 1, 4, 8, 0);
        suo = mk_desc(ctx, &d);

        top_ldst_load(sua_ld, sga, 0, LDST_MODE_NORMAL);
        memset(ctx->urf + UO, 0xAA, NELEM); /* poison the output plane */
        TNVI_BEGIN("vec_not:") { top_vec_not(suo, sua_zs); }
        TNVI_END();
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));
        CHECK(memcmp(out, poison, sizeof(out)) == 0, "not ZEROSTRIDE input -> violation");

        tdr_free(ctx, sga);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sua_ld);
        tdr_free(ctx, sua_zs);
        tdr_free(ctx, suo);
    }
}
