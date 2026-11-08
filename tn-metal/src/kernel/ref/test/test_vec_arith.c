// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/test/test_vec_arith.c
 *
 * top/vec elementwise-arith family cases: add / sub / mul / div / max / min
 * (integer saturate/truncate, width promotion, FP32/FP8/BF16, CONSTANT,
 * ZEROSTRIDE broadcast); mac and single-operand arith (square/abs/floor)
 * land in this file later.
 * ========================================================================== */
#include "smoke.h"

void suite_vec_arith(tnkr_ctx_t *ctx) {
    int64_t A[NELEM], B[NELEM];
    uint32_t i;

    /* ==================================================================
     * vec_add: integer saturate/truncate, width promotion, FP32,
     *          broadcast, constant, FP8 saturation, BF16 RNE
     * ================================================================== */
    printf("== vec_add ==\n");

    for (i = 0; i < NELEM; i++) {
        A[i] = (int64_t)i - 16;
        B[i] = (int64_t)(i % 5) - 2;
    }
    A[0] = 100;
    B[0] = 100; /* saturation point */
    A[1] = -100;
    B[1] = -100;
    check_int_binop(ctx, "add S8 saturate", top_vec_add, '+', TN_DTYPE_S8, TN_DTYPE_S8,
                    OVF_MODE_SATURATE, A, B);
    check_int_binop(ctx, "add S8 truncate", top_vec_add, '+', TN_DTYPE_S8, TN_DTYPE_S8,
                    OVF_MODE_TRUNCATE, A, B);
    check_int_binop(ctx, "add S8 -> S16 (widen)", top_vec_add, '+', TN_DTYPE_S8, TN_DTYPE_S16,
                    OVF_MODE_SATURATE, A, B);

    /* FP32: exact values, no rounding ambiguity */
    {
        float fa[NELEM], fb[NELEM], fout[NELEM], fexp[NELEM];
        for (i = 0; i < NELEM; i++) {
            fa[i] = (float)((int)i - 16) * 0.5f;
            fb[i] = (float)(i % 4) + 0.25f;
            fexp[i] = fa[i] + fb[i];
        }
        run_binop(ctx, top_vec_add, TN_DTYPE_FP32, TN_DTYPE_FP32, OVF_MODE_SATURATE, fa, fb, fout);
        CHECK(memcmp(fout, fexp, sizeof(fout)) == 0, "add FP32");
    }

    /* CONSTANT operand: B = constant 5 (S8) */
    {
        int64_t a64[NELEM];
        uint8_t in_a[NELEM], out[NELEM], expct[NELEM];
        tdr_t d;
        uint8_t sga, sua, suo, sc, sgo;

        for (i = 0; i < NELEM; i++) {
            a64[i] = (int64_t)i - 16;
            in_a[i] = (uint8_t)(int8_t)a64[i];
        }
        a64[0] = 125;
        in_a[0] = 125; /* 125+5 saturation point */
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
        top_vec_add(suo, sua, sc, OVF_MODE_SATURATE);
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));

        for (i = 0; i < NELEM; i++)
            expct[i] = (uint8_t)t_expect_int(a64[i] + 5, 8, 1, OVF_MODE_SATURATE);
        CHECK(memcmp(out, expct, sizeof(out)) == 0, "add S8 + CONSTANT(5)");

        tdr_free(ctx, sga);
        tdr_free(ctx, sua);
        tdr_free(ctx, suo);
        tdr_free(ctx, sc);
        tdr_free(ctx, sgo);
    }

    /* ZEROSTRIDE broadcast (H/N dims): B is stored as a (1,1,1,8) plane —
     * the data is smaller than one alignment unit; plane starts align to
     * TDR_URF_WIDTH_BYTES and the shortfall is padding (h*w need not be a
     * multiple of the alignment granularity).
     * ldst's URF side accepts only HWALIGN, so staging loads use a
     * same-shape HWALIGN descriptor and the operation then reads the same
     * address through a ZEROSTRIDE view.
     * Note there is no C broadcast here: CB requires dc == smc_num (lane
     * locality, see tensor.cpp); the standalone C-broadcast case is below */
    {
        int64_t a64[NELEM];
        uint8_t in_a[NELEM], in_b[8], out[NELEM], expct[NELEM];
        tdr_t d;
        uint8_t sga, sgb, sgo, sua, sub_ld, sub_, suo;

        for (i = 0; i < NELEM; i++) {
            a64[i] = (int64_t)i - 16;
            in_a[i] = (uint8_t)(int8_t)a64[i];
        }
        for (i = 0; i < 8; i++)
            in_b[i] = (uint8_t)(i + 1); /* 8-element broadcast vector */
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
        /* HWALIGN staging: used only for the load (ldst rejects ZEROSTRIDE) */
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UB, 1, 1, 1, 8, 0);
        sub_ld = mk_desc(ctx, &d);
        /* same-address ZEROSTRIDE view: N/H broadcast (dc=1, no CB) */
        d = tdr_make_urf(TDR_LAYOUT_ZEROSTRIDE, TN_DTYPE_S8, UB, 1, 1, 1, 8,
                         TDR_BCAST_NB | TDR_BCAST_HB);
        sub_ = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UO, 1, 1, 4, 8, 0);
        suo = mk_desc(ctx, &d);

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
        top_ldst_load(sub_ld, sgb, 0, LDST_MODE_NORMAL);
        top_vec_add(suo, sua, sub_, OVF_MODE_SATURATE);
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));

        for (i = 0; i < NELEM; i++) /* out(h,w) = a(h,w) + bvec[w] */
            expct[i] =
                (uint8_t)t_expect_int(a64[i] + (int64_t)(i % 8) + 1, 8, 1, OVF_MODE_SATURATE);
        CHECK(memcmp(out, expct, sizeof(out)) == 0, "add S8 + ZEROSTRIDE bcast vector");

        tdr_free(ctx, sga);
        tdr_free(ctx, sgb);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sua);
        tdr_free(ctx, sub_ld);
        tdr_free(ctx, sub_);
        tdr_free(ctx, suo);
    }

    /* ZEROSTRIDE C broadcast (lane locality): B stored (1,smc_num,1,8) —
     * each lane holds the w-element vector of its own channel; A/output
     * (1,2*smc_num,4,8).
     * Expect out(c,h,w) = a(c,h,w) + b_lane(c%smc_num)[w]: channels c and
     * c+smc_num wrap around and reuse the same lane's local values, and
     * every lane's values differ — this catches both "broadcast lane0 to
     * all" and "tile sequentially without wraparound" buggy
     * implementations.
     * (Shapes and buffers derive from ctx->info.smc_num; no spec values
     * are hardcoded) */
    {
        const uint32_t smc = ctx->info.smc_num;
        const uint32_t plane = 4 * 8;        /* plane elements h*w */
        const uint32_t an = 2 * smc * plane; /* A/output element count */
        const uint32_t bn = smc * plane;     /* B staging element count */
        uint8_t in_a[an], in_b[bn], out[an], expct[an];
        tdr_t d;
        uint8_t sga, sgb, sgo, sua, sub_ld, sub_, suo;
        uint32_t c, w;

        for (i = 0; i < an; i++)
            in_a[i] = (uint8_t)(i * 5u + 2u);
        memset(in_b, 0, bn);
        for (c = 0; c < smc; c++) /* lane c's vector sits in row 0 */
            for (w = 0; w < 8; w++)
                in_b[c * plane + w] = (uint8_t)(c * 8 + w);
        memcpy(ctx->global + GA, in_a, an);
        memcpy(ctx->global + GB, in_b, bn);

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, (uint16_t)(2 * smc), 4, 8);
        sga = mk_desc(ctx, &d);
        /* B loads through a (1,smc,4,8) HWALIGN alias: one aligned plane
         * per lane */
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GB, 1, (uint16_t)smc, 4, 8);
        sgb = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, (uint16_t)(2 * smc), 4, 8);
        sgo = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, (uint16_t)(2 * smc), 4, 8, 0);
        sua = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UB, 1, (uint16_t)smc, 4, 8, 0);
        sub_ld = mk_desc(ctx, &d);
        /* ZEROSTRIDE storage view (1,smc,1,8): dc == smc_num, N/C/H broadcast */
        d = tdr_make_urf(TDR_LAYOUT_ZEROSTRIDE, TN_DTYPE_S8, UB, 1, (uint16_t)smc, 1, 8,
                         TDR_BCAST_NB | TDR_BCAST_CB | TDR_BCAST_HB);
        sub_ = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UO, 1, (uint16_t)(2 * smc), 4, 8, 0);
        suo = mk_desc(ctx, &d);

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
        top_ldst_load(sub_ld, sgb, 0, LDST_MODE_NORMAL);
        top_vec_add(suo, sua, sub_, OVF_MODE_SATURATE);
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, an);

        for (i = 0; i < an; i++) {
            int64_t a = (int8_t)in_a[i];
            int64_t b = (int8_t)in_b[((i / plane) % smc) * plane + (i % 8)];
            expct[i] = (uint8_t)t_expect_int(a + b, 8, 1, OVF_MODE_SATURATE);
        }
        CHECK(memcmp(out, expct, an) == 0, "add S8 + ZEROSTRIDE C bcast (dc=smc_num)");

        tdr_free(ctx, sga);
        tdr_free(ctx, sgb);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sua);
        tdr_free(ctx, sub_ld);
        tdr_free(ctx, sub_);
        tdr_free(ctx, suo);
    }

    /* Illegal ZEROSTRIDE C-broadcast shape (dc=1 != smc_num would need
     * cross-SMC transfer) -> violation raised, the output keeps its
     * poison pattern */
    {
        uint8_t in_a[NELEM], out[NELEM], poison[NELEM];
        tdr_t d;
        uint8_t sga, sgo, sua, sub_bad, suo;

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
        /* passes tdr_check (ZEROSTRIDE has no alignment constraint) but is
         * rejected at engine resolve time by the lane-locality rule */
        d = tdr_make_urf(TDR_LAYOUT_ZEROSTRIDE, TN_DTYPE_S8, UB, 1, 1, 1, 8,
                         TDR_BCAST_NB | TDR_BCAST_CB | TDR_BCAST_HB);
        sub_bad = mk_desc(ctx, &d);

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
        memset(ctx->urf + UO, 0xAA, NELEM); /* poison the output plane */
        TNVI_BEGIN("ZEROSTRIDE stored-extent violation") {
            top_vec_add(suo, sua, sub_bad, OVF_MODE_SATURATE);
        }
        TNVI_END();
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));
        CHECK(memcmp(out, poison, sizeof(out)) == 0, "ZEROSTRIDE C bcast dc=1 -> violation");

        tdr_free(ctx, sga);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sua);
        tdr_free(ctx, sub_bad);
        tdr_free(ctx, suo);
    }

    /* ZEROSTRIDE N/H/W broadcast dims must store extent 1 (kernel/common/tdr.h).
     * These descriptors fail the host-side tdr_check (verified first), so
     * the cases emulate a raw tdr_write into the slot and check that the
     * engine's resolve rejects them independently -> violation, poison
     * unchanged */
    {
        static const struct {
            uint16_t n, h, w;
            uint8_t bc;
            const char *what;
        } bad[3] = {
            {2, 1, 8, TDR_BCAST_NB, "NB stored dn=2"},
            {1, 2, 8, TDR_BCAST_HB, "HB stored dh=2"},
            {1, 1, 2, TDR_BCAST_WB, "WB stored dw=2"},
        };
        uint8_t in_a[NELEM], out[NELEM], poison[NELEM];
        char name[80];
        tdr_t d;
        uint8_t sga, sgo, sua, suo;
        int k, idx;

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

        for (k = 0; k < 3; k++) {
            d = tdr_make_urf(TDR_LAYOUT_ZEROSTRIDE, TN_DTYPE_S8, UB, bad[k].n, 1, bad[k].h,
                             bad[k].w, bad[k].bc);
            idx = tdr_alloc(ctx);
            if (idx < 0) {
                printf("  FATAL: descriptor alloc failed\n");
                g_fail++;
                break;
            }
            snprintf(name, sizeof(name), "ZEROSTRIDE %s: host rejects", bad[k].what);
            CHECK(tdr_shadow_write(ctx, (uint8_t)idx, &d) != TN_SUCCESS, name);

            /* tdr_shadow_write rejected it, so the slot is unconfigured;
             * raw-write it to emulate tdr_write */
            ctx->shadow[idx].desc = d;
            TNKR_BIT_SET(ctx->valid, idx);
            TNKR_BIT_CLR(ctx->is_stride, idx);

            memset(ctx->urf + UO, 0xAA, NELEM); /* poison the output plane */
            snprintf(name, sizeof(name), "ZEROSTRIDE %s: engine violation", bad[k].what);
            TNVI_BEGIN("ZEROSTRIDE stored-extent violation") {
                top_vec_add(suo, sua, (uint8_t)idx, OVF_MODE_SATURATE);
            }
            TNVI_END();
            top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
            memcpy(out, ctx->global + GO, sizeof(out));
            CHECK(memcmp(out, poison, sizeof(out)) == 0, name);
            tdr_free(ctx, (uint8_t)idx);
        }

        tdr_free(ctx, sga);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sua);
        tdr_free(ctx, suo);
    }

    /* FP8: 1.0+1.0=2.0; 448+1.0 saturates to 448; -1.0+-1.0=-2.0 */
    {
        uint8_t fa[NELEM], fb[NELEM], fout[NELEM], fexp[NELEM];
        memset(fa, 0, sizeof(fa));
        memset(fb, 0, sizeof(fb));
        memset(fexp, 0, sizeof(fexp));
        fa[0] = 0x38;
        fb[0] = 0x38;
        fexp[0] = 0x40; /* 1.0+1.0=2.0 */
        fa[1] = 0x7E;
        fb[1] = 0x38;
        fexp[1] = 0x7E; /* 448+1 saturates */
        fa[2] = 0xB8;
        fb[2] = 0xB8;
        fexp[2] = 0xC0; /* -1.0-1.0=-2.0 */
        run_binop(ctx, top_vec_add, TN_DTYPE_FP8_E4M3, TN_DTYPE_FP8_E4M3, OVF_MODE_SATURATE, fa, fb,
                  fout);
        CHECK(memcmp(fout, fexp, sizeof(fout)) == 0, "add FP8 (incl. saturation)");
    }

    /* BF16: 0x3F81(1.0078125) + 0x3B80(0.00390625) = 1.01171875,
     * exactly the midpoint of 0x3F81/0x3F82 -> RNE picks even -> 0x3F82;
     * 0x3FC0(1.5) + 0x4010(2.25) = 3.75 -> 0x4070 */
    {
        uint16_t fa[NELEM], fb[NELEM], fout[NELEM], fexp[NELEM];
        memset(fa, 0, sizeof(fa));
        memset(fb, 0, sizeof(fb));
        memset(fexp, 0, sizeof(fexp));
        fa[0] = 0x3F81;
        fb[0] = 0x3B80;
        fexp[0] = 0x3F82;
        fa[1] = 0x3FC0;
        fb[1] = 0x4010;
        fexp[1] = 0x4070;
        run_binop(ctx, top_vec_add, TN_DTYPE_BF16, TN_DTYPE_BF16, OVF_MODE_SATURATE, fa, fb, fout);
        CHECK(memcmp(fout, fexp, sizeof(fout)) == 0, "add BF16 (incl. RNE ties-to-even)");
    }

    /* ==================================================================
     * vec_sub / mul / div / max / min
     * ================================================================== */
    printf("== vec_sub/mul/div/max/min ==\n");

    for (i = 0; i < NELEM; i++) {
        A[i] = (int64_t)i - 16;
        B[i] = (int64_t)(i % 7) - 3;
    }
    A[0] = 10;
    B[0] = 200; /* U8 subtraction: underflow */
    check_int_binop(ctx, "sub U8 saturate (underflow)", top_vec_sub, '-', TN_DTYPE_U8, TN_DTYPE_U8,
                    OVF_MODE_SATURATE, A, B);
    check_int_binop(ctx, "sub U8 truncate", top_vec_sub, '-', TN_DTYPE_U8, TN_DTYPE_U8,
                    OVF_MODE_TRUNCATE, A, B);
    check_int_binop(ctx, "sub U8 -> S16", top_vec_sub, '-', TN_DTYPE_U8, TN_DTYPE_S16,
                    OVF_MODE_SATURATE, A, B);

    A[0] = 20;
    B[0] = 20; /* 400: U8 overflow */
    check_int_binop(ctx, "mul U8 saturate", top_vec_mul, '*', TN_DTYPE_U8, TN_DTYPE_U8,
                    OVF_MODE_SATURATE, A, B);
    check_int_binop(ctx, "mul U8 truncate", top_vec_mul, '*', TN_DTYPE_U8, TN_DTYPE_U8,
                    OVF_MODE_TRUNCATE, A, B);

    check_int_binop(ctx, "max S8", op_max, '>', TN_DTYPE_S8, TN_DTYPE_S8, OVF_MODE_SATURATE, A, B);
    check_int_binop(ctx, "min S8", op_min, '<', TN_DTYPE_S8, TN_DTYPE_S8, OVF_MODE_SATURATE, A, B);

    /* FP32 div: 3.5 exact; 1/0 and -1/0 produce ±inf -> saturate-clamped
     * to ±max; 0/0 -> NaN (canonical 0x7FC00000) */
    {
        float fa[NELEM], fb[NELEM], fout[NELEM];
        uint32_t exp32[NELEM], fmax = 0x7F7FFFFFu;
        memset(fa, 0, sizeof(fa));
        memset(exp32, 0, sizeof(exp32));
        for (i = 0; i < NELEM; i++)
            fb[i] = 1.0f;
        fa[0] = 7.0f;
        fb[0] = 2.0f;
        exp32[0] = 0x40600000u; /* 3.5 */
        fa[1] = 1.0f;
        fb[1] = 0.0f;
        exp32[1] = fmax; /* +inf clamp */
        fa[2] = -1.0f;
        fb[2] = 0.0f;
        exp32[2] = 0xFF7FFFFFu; /* -inf clamp */
        fa[3] = 0.0f;
        fb[3] = 0.0f;
        exp32[3] = 0x7FC00000u; /* NaN */
        run_binop(ctx, op_div, TN_DTYPE_FP32, TN_DTYPE_FP32, OVF_MODE_SATURATE, fa, fb, fout);
        CHECK(memcmp(fout, exp32, sizeof(fout)) == 0, "div FP32 (incl. div0 saturation)");
    }

    /* div with a Constant right operand: the contract reserves Constant for
     * the dividend side only (kernel.h) -> violation raised before any
     * write, the output keeps its poison pattern (FP32: div is FP-only,
     * so without this guard the op would run and break the poison) */
    {
        float in_a[NELEM], out[NELEM];
        uint8_t poison[NELEM * 4];
        tdr_t d;
        uint8_t sga, sua, sc, suo, sgo;

        for (i = 0; i < NELEM; i++)
            in_a[i] = (float)(i + 1);
        memset(poison, 0xAA, sizeof(poison));
        memcpy(ctx->global + GA, in_a, sizeof(in_a));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GA, 1, 1, 4, 8);
        sga = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UA, 1, 1, 4, 8, 0);
        sua = mk_desc(ctx, &d);
        d = tdr_make_urf_constant(TN_DTYPE_FP32, 0x40000000u); /* 2.0f */
        sc = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_FP32, UO, 1, 1, 4, 8, 0);
        suo = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP32, GO, 1, 1, 4, 8);
        sgo = mk_desc(ctx, &d);

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
        memset(ctx->urf + UO, 0xAA, NELEM * 4u); /* poison the output plane */
        TNVI_BEGIN("div right operand may not be Constant") { top_vec_div(suo, sua, sc); }
        TNVI_END();
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));
        CHECK(memcmp(out, poison, sizeof(out)) == 0, "div right Constant -> violation");

        tdr_free(ctx, sga);
        tdr_free(ctx, sua);
        tdr_free(ctx, sc);
        tdr_free(ctx, suo);
        tdr_free(ctx, sgo);
    }

    /* mul/max/min with a Constant left operand: Constant goes on right
     * (opd1) only for these (kernel.h) -> violation before any write,
     * the poisoned output survives each time */
    {
        uint8_t in_a[NELEM], out[NELEM], poison[NELEM];
        tdr_t d;
        uint8_t sga, sua, sc, suo, sgo;

        for (i = 0; i < NELEM; i++) {
            in_a[i] = (uint8_t)(i + 1u);
            poison[i] = 0xAA;
        }
        memcpy(ctx->global + GA, in_a, sizeof(in_a));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 1, 4, 8);
        sga = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, 1, 4, 8, 0);
        sua = mk_desc(ctx, &d);
        d = tdr_make_urf_constant(TN_DTYPE_S8, 0x03);
        sc = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UO, 1, 1, 4, 8, 0);
        suo = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, 1, 4, 8);
        sgo = mk_desc(ctx, &d);

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);

        memset(ctx->urf + UO, 0xAA, NELEM); /* poison the output plane */
        TNVI_BEGIN("vec_mul") { top_vec_mul(suo, sc, sua, OVF_MODE_SATURATE); }
        TNVI_END();
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));
        CHECK(memcmp(out, poison, sizeof(out)) == 0, "mul left Constant -> violation");

        memset(ctx->urf + UO, 0xAA, NELEM);
        TNVI_BEGIN("vec_max") { top_vec_max(suo, sc, sua); }
        TNVI_END();
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));
        CHECK(memcmp(out, poison, sizeof(out)) == 0, "max left Constant -> violation");

        memset(ctx->urf + UO, 0xAA, NELEM);
        TNVI_BEGIN("vec_min") { top_vec_min(suo, sc, sua); }
        TNVI_END();
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));
        CHECK(memcmp(out, poison, sizeof(out)) == 0, "min left Constant -> violation");

        tdr_free(ctx, sga);
        tdr_free(ctx, sua);
        tdr_free(ctx, sc);
        tdr_free(ctx, suo);
        tdr_free(ctx, sgo);
    }

    /* FP32 mul */
    {
        float fa[NELEM], fb[NELEM], fout[NELEM], fexp[NELEM];
        for (i = 0; i < NELEM; i++) {
            fa[i] = (float)((int)i - 16) * 0.25f;
            fb[i] = (float)(i % 3) - 1.0f;
            fexp[i] = fa[i] * fb[i];
        }
        run_binop(ctx, top_vec_mul, TN_DTYPE_FP32, TN_DTYPE_FP32, OVF_MODE_SATURATE, fa, fb, fout);
        CHECK(memcmp(fout, fexp, sizeof(fout)) == 0, "mul FP32");
    }
}
