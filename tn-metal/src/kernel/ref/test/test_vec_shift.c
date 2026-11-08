// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/test/test_vec_shift.c
 *
 * top/vec shift family cases: logic/arith/rotate left/right shift
 * (tensor amounts, uint5 immediates, ZEROSTRIDE broadcast amounts, plus
 * violation negatives for out-of-range amounts, FP input, a
 * Constant-layout input and a Constant-layout shift operand).
 * Expectations are computed independently on the test side.
 * ========================================================================== */
#include "smoke.h"

typedef void (*shift_fn)(tdr_idx_t, tdr_idx_t, tdr_idx_t, uint8_t, shift_mode_t);

/* Test-side independent shift expectation on the width-masked pattern;
 * callers guarantee s < bits */
static uint32_t expect_shift(char op, int64_t a, uint32_t s, unsigned bits) {
    uint32_t mask = (bits >= 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    uint32_t x = (uint32_t)a & mask;

    if (s == 0)
        return x;
    switch (op) {
    case 'l':
        return (x << s) & mask;
    case 'r':
        return x >> s;
    case 'a': {
        uint32_t v = x >> s;
        if (x & (1u << (bits - 1)))
            v |= (mask << (bits - s)) & mask;
        return v;
    }
    case 'o':
        return ((x << s) | (x >> (bits - s))) & mask;
    default:
        return ((x >> s) | (x << (bits - s))) & mask;
    }
}

/* Standard tensor-amount case: A and the amounts both go through the
 * global->URF->op->global loop; the amount dtype may differ from the
 * input dtype */
static void check_shift_tensor(tnkr_ctx_t *ctx, const char *name, shift_fn fn, char op,
                               tn_dtype_t idt, tn_dtype_t sdt, const int64_t *A, const int64_t *S) {
    uint8_t in_a[NELEM * 4], in_s[NELEM * 4], out[NELEM * 4], expct[NELEM * 4];
    unsigned isz = tn_dtype_size(idt), ssz = tn_dtype_size(sdt);
    unsigned bits = tn_dtype_bits(idt);
    int64_t imask = (1LL << bits) - 1;
    int64_t smask = (1LL << tn_dtype_bits(sdt)) - 1;
    tdr_t d;
    uint8_t sga, sgs, sgo, sua, sus, suo;
    uint32_t i, e;
    int64_t am, sm;

    memset(in_a, 0, sizeof(in_a));
    memset(in_s, 0, sizeof(in_s));
    memset(expct, 0, sizeof(expct));
    for (i = 0; i < NELEM; i++) {
        am = A[i] & imask; /* wrap to width: matches the in-memory pattern */
        sm = S[i] & smask;
        e = expect_shift(op, am, (uint32_t)sm, bits);
        memcpy(in_a + i * isz, &am, isz);
        memcpy(in_s + i * ssz, &sm, ssz);
        memcpy(expct + i * isz, &e, isz);
    }
    memcpy(ctx->global + GA, in_a, NELEM * isz);
    memcpy(ctx->global + GB, in_s, NELEM * ssz);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, idt, GA, 1, 1, 4, 8);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, sdt, GB, 1, 1, 4, 8);
    sgs = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, idt, GO, 1, 1, 4, 8);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, idt, UA, 1, 1, 4, 8, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, sdt, UB, 1, 1, 4, 8, 0);
    sus = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, idt, UO, 1, 1, 4, 8, 0);
    suo = mk_desc(ctx, &d);

    top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
    top_ldst_load(sus, sgs, 0, LDST_MODE_NORMAL);
    fn(suo, sua, sus, 0, TENSOR);
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(out, ctx->global + GO, NELEM * isz);
    CHECK(memcmp(out, expct, NELEM * isz) == 0, name);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgs);
    tdr_free(ctx, sgo);
    tdr_free(ctx, sua);
    tdr_free(ctx, sus);
    tdr_free(ctx, suo);
}

/* Immediate-amount case (mode=CONSTANT): only the input goes through the
 * loop; the amount rides on the instruction */
static void check_shift_imm(tnkr_ctx_t *ctx, const char *name, shift_fn fn, char op, tn_dtype_t idt,
                            uint8_t imm, const int64_t *A) {
    uint8_t in_a[NELEM * 4], out[NELEM * 4], expct[NELEM * 4];
    unsigned isz = tn_dtype_size(idt);
    unsigned bits = tn_dtype_bits(idt);
    int64_t imask = (1LL << bits) - 1;
    tdr_t d;
    uint8_t sga, sgo, sua, suo;
    uint32_t i, e;
    int64_t am;

    memset(in_a, 0, sizeof(in_a));
    memset(expct, 0, sizeof(expct));
    for (i = 0; i < NELEM; i++) {
        am = A[i] & imask;
        e = expect_shift(op, am, imm, bits);
        memcpy(in_a + i * isz, &am, isz);
        memcpy(expct + i * isz, &e, isz);
    }
    memcpy(ctx->global + GA, in_a, NELEM * isz);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, idt, GA, 1, 1, 4, 8);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, idt, GO, 1, 1, 4, 8);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, idt, UA, 1, 1, 4, 8, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, idt, UO, 1, 1, 4, 8, 0);
    suo = mk_desc(ctx, &d);

    top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
    fn(suo, sua, 0, imm, CONSTANT);
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(out, ctx->global + GO, NELEM * isz);
    CHECK(memcmp(out, expct, NELEM * isz) == 0, name);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgo);
    tdr_free(ctx, sua);
    tdr_free(ctx, suo);
}

void suite_vec_shift(tnkr_ctx_t *ctx) {
    int64_t A[NELEM], S8[NELEM], S16[NELEM];
    uint32_t i;

    printf("== vec_shift ==\n");

    /* ---- tensor amounts: multi-width, multi-op coverage ---- */
    for (i = 0; i < NELEM; i++) {
        A[i] = (int64_t)(i * 37u + 5u) - 600; /* signed spread */
        S8[i] = (int64_t)((i * 5u) % 8u);     /* amounts stay < width */
        S16[i] = (int64_t)((i * 7u) % 16u);
    }
    check_shift_tensor(ctx, "lsl S16 tensor", top_vec_logic_left_shift, 'l', TN_DTYPE_S16,
                       TN_DTYPE_U8, A, S16);
    check_shift_tensor(ctx, "lsr U8 tensor", top_vec_logic_right_shift, 'r', TN_DTYPE_U8,
                       TN_DTYPE_S8, A, S8);
    check_shift_tensor(ctx, "asr S8 tensor (sign fill)", top_vec_arith_right_shift, 'a',
                       TN_DTYPE_S8, TN_DTYPE_U8, A, S8);
    check_shift_tensor(ctx, "rol U8 tensor", top_vec_rotate_left_shift, 'o', TN_DTYPE_U8,
                       TN_DTYPE_S16, A, S8);
    check_shift_tensor(ctx, "ror S16 tensor", top_vec_rotate_right_shift, 'e', TN_DTYPE_S16,
                       TN_DTYPE_U8, A, S16);

    /* ---- Constant-layout shift operand: unsupported on opd1 -> violation
     * (scalar amounts go through mode=CONSTANT + shift_imm instead) ---- */
    {
        uint8_t in_a[NELEM], out[NELEM], poison[NELEM];
        int64_t a64;
        tdr_t d;
        uint8_t sga, sua, suo, sc, sgo;

        for (i = 0; i < NELEM; i++) {
            a64 = A[i] & 0xFF;
            in_a[i] = (uint8_t)a64;
            poison[i] = 0xAA;
        }
        memcpy(ctx->global + GA, in_a, sizeof(in_a));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 1, 4, 8);
        sga = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, 1, 4, 8, 0);
        sua = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UO, 1, 1, 4, 8, 0);
        suo = mk_desc(ctx, &d);
        d = tdr_make_urf_constant(TN_DTYPE_S8, 3);
        sc = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, 1, 4, 8);
        sgo = mk_desc(ctx, &d);

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
        memset(ctx->urf + UO, 0xAA, NELEM); /* poison the output plane */
        TNVI_BEGIN("shift operand layout not allowed") {
            top_vec_logic_left_shift(suo, sua, sc, 0, TENSOR);
        }
        TNVI_END();
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));
        CHECK(memcmp(out, poison, sizeof(out)) == 0, "Constant shift operand -> violation");

        tdr_free(ctx, sga);
        tdr_free(ctx, sua);
        tdr_free(ctx, suo);
        tdr_free(ctx, sc);
        tdr_free(ctx, sgo);
    }

    /* ---- ZEROSTRIDE broadcast amounts (N/H dims): S stored (1,1,1,8),
     * staged via a same-address HWALIGN alias (ldst rejects ZEROSTRIDE on
     * URF); expect out(h,w) = a(h,w) << svec[w] ---- */
    {
        uint8_t in_a[NELEM], in_s[8], out[NELEM], expct[NELEM];
        int64_t a64;
        tdr_t d;
        uint8_t sga, sgs, sgo, sua, sus_ld, sus, suo;

        for (i = 0; i < NELEM; i++)
            in_a[i] = (uint8_t)(A[i] & 0xFF);
        for (i = 0; i < 8; i++)
            in_s[i] = (uint8_t)(i % 8u); /* broadcast amount vector */
        memcpy(ctx->global + GA, in_a, sizeof(in_a));
        memcpy(ctx->global + GB, in_s, sizeof(in_s));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 1, 4, 8);
        sga = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GB, 1, 1, 1, 8);
        sgs = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, 1, 4, 8);
        sgo = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, 1, 4, 8, 0);
        sua = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UB, 1, 1, 1, 8, 0);
        sus_ld = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_ZEROSTRIDE, TN_DTYPE_S8, UB, 1, 1, 1, 8,
                         TDR_BCAST_NB | TDR_BCAST_HB);
        sus = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UO, 1, 1, 4, 8, 0);
        suo = mk_desc(ctx, &d);

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
        top_ldst_load(sus_ld, sgs, 0, LDST_MODE_NORMAL);
        top_vec_logic_left_shift(suo, sua, sus, 0, TENSOR);
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));

        for (i = 0; i < NELEM; i++) {
            a64 = A[i] & 0xFF;
            expct[i] = (uint8_t)((uint32_t)a64 << in_s[i % 8]);
        }
        CHECK(memcmp(out, expct, sizeof(out)) == 0, "lsl S8 + ZEROSTRIDE bcast amounts");

        tdr_free(ctx, sga);
        tdr_free(ctx, sgs);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sua);
        tdr_free(ctx, sus_ld);
        tdr_free(ctx, sus);
        tdr_free(ctx, suo);
    }

    /* ---- immediates (mode=CONSTANT): top of the legal range and the
     * identity case ---- */
    check_shift_imm(ctx, "asr S32 imm 31", top_vec_arith_right_shift, 'a', TN_DTYPE_S32, 31, A);
    check_shift_imm(ctx, "ror S8 imm 0 (identity)", top_vec_rotate_right_shift, 'e', TN_DTYPE_S8, 0,
                    A);

    /* ---- negatives: violation raised, output keeps its poison pattern ---- */

    /* tensor amount >= input width (one bad element rejects the op) */
    {
        uint8_t in_a[NELEM], in_s[NELEM], out[NELEM], poison[NELEM];
        tdr_t d;
        uint8_t sga, sgs, sgo, sua, sus, suo;

        for (i = 0; i < NELEM; i++) {
            in_a[i] = (uint8_t)i;
            in_s[i] = 0;
            poison[i] = 0xAA;
        }
        in_s[0] = 8; /* S8 width is 8: out of range */
        memcpy(ctx->global + GA, in_a, sizeof(in_a));
        memcpy(ctx->global + GB, in_s, sizeof(in_s));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 1, 4, 8);
        sga = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GB, 1, 1, 4, 8);
        sgs = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, 1, 4, 8);
        sgo = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, 1, 4, 8, 0);
        sua = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UB, 1, 1, 4, 8, 0);
        sus = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UO, 1, 1, 4, 8, 0);
        suo = mk_desc(ctx, &d);

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
        top_ldst_load(sus, sgs, 0, LDST_MODE_NORMAL);
        memset(ctx->urf + UO, 0xAA, NELEM); /* poison the output plane */
        TNVI_BEGIN("vec_shift") { top_vec_logic_left_shift(suo, sua, sus, 0, TENSOR); }
        TNVI_END();
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));
        CHECK(memcmp(out, poison, sizeof(out)) == 0, "tensor amount >= width -> violation");

        tdr_free(ctx, sga);
        tdr_free(ctx, sgs);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sua);
        tdr_free(ctx, sus);
        tdr_free(ctx, suo);
    }

    /* immediate >= input width */
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
        TNVI_BEGIN("vec_shift") { top_vec_logic_left_shift(suo, sua, 0, 8, CONSTANT); }
        TNVI_END();
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));
        CHECK(memcmp(out, poison, sizeof(out)) == 0, "imm >= width -> violation");

        tdr_free(ctx, sga);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sua);
        tdr_free(ctx, suo);
    }

    /* FP input: shifts are integers only */
    {
        uint8_t in_a[NELEM], out[NELEM], poison[NELEM];
        tdr_t d;
        uint8_t sga, sgo, sua, suo;

        for (i = 0; i < NELEM; i++) {
            in_a[i] = (uint8_t)i;
            poison[i] = 0xAA;
        }
        memcpy(ctx->global + GA, in_a, sizeof(in_a));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP8_E4M3, GA, 1, 1, 4, 8);
        sga = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP8_E4M3, GO, 1, 1, 4, 8);
        sgo = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP8_E4M3, UA, 1, 1, 4, 8, 0);
        sua = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP8_E4M3, UO, 1, 1, 4, 8, 0);
        suo = mk_desc(ctx, &d);

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
        memset(ctx->urf + UO, 0xAA, NELEM); /* poison the output plane */
        TNVI_BEGIN("integers only") { top_vec_logic_left_shift(suo, sua, 0, 1, CONSTANT); }
        TNVI_END();
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));
        CHECK(memcmp(out, poison, sizeof(out)) == 0, "lsl FP8 input -> violation");

        tdr_free(ctx, sga);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sua);
        tdr_free(ctx, suo);
    }

    /* Constant-layout input: input may only be ZeroStride/HWAlign */
    {
        uint8_t out[NELEM], poison[NELEM];
        tdr_t d;
        uint8_t sc_in, sgo, suo;

        for (i = 0; i < NELEM; i++)
            poison[i] = 0xAA;

        d = tdr_make_urf_constant(TN_DTYPE_S8, 0x55);
        sc_in = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UO, 1, 1, 4, 8, 0);
        suo = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, 1, 4, 8);
        sgo = mk_desc(ctx, &d);

        memset(ctx->urf + UO, 0xAA, NELEM); /* poison the output plane */
        TNVI_BEGIN("input must be ZeroStride/HWAlign") {
            top_vec_logic_left_shift(suo, sc_in, 0, 1, CONSTANT);
        }
        TNVI_END();
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));
        CHECK(memcmp(out, poison, sizeof(out)) == 0, "Constant input -> violation");

        tdr_free(ctx, sc_in);
        tdr_free(ctx, suo);
        tdr_free(ctx, sgo);
    }
}
