// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/test/test_dmr_move.c
 *
 * dmr movement family cases: ld/st normal loopback; CW and NC transpose
 * verified independently on the ld side and the st side; mv URF<->URF
 * (cw/nc) and Global<->Global (Cont copy + FREE broadcast read); st
 * reduce_add (S8/U8 saturate, BF16 RNE + saturate, reduce x CW transpose
 * orthogonality); violation negatives (loc, layout, pairing) and
 * reserved-encoding silent returns (mode 3, reduce 2) with untouched
 * targets. Expectations are pure test-side index arithmetic on element
 * patterns (no URF-internal layout dependency, end-user perspective).
 * ========================================================================== */
#include "smoke.h"

/* CW expectation: got(n,w,h,c) == src(n,c,h,w) for in (2,3,4,8),
 * out (2,8,4,3) */
static uint32_t cw_fails(const uint8_t *got, const uint8_t *src) {
    uint32_t n, c, h, w, fails = 0;

    for (n = 0; n < 2; n++)
        for (w = 0; w < 8; w++)
            for (h = 0; h < 4; h++)
                for (c = 0; c < 3; c++)
                    if (got[((n * 8 + w) * 4 + h) * 3 + c] != src[((n * 3 + c) * 4 + h) * 8 + w])
                        fails++;
    return fails;
}

/* NC expectation: got(c,n,h,w) == src(n,c,h,w) for in (2,3,4,8),
 * out (3,2,4,8) */
static uint32_t nc_fails(const uint8_t *got, const uint8_t *src) {
    uint32_t n, c, h, w, fails = 0;

    for (n = 0; n < 2; n++)
        for (c = 0; c < 3; c++)
            for (h = 0; h < 4; h++)
                for (w = 0; w < 8; w++)
                    if (got[((c * 2 + n) * 4 + h) * 8 + w] != src[((n * 3 + c) * 4 + h) * 8 + w])
                        fails++;
    return fails;
}

void suite_dmr_move(tnkr_ctx_t *ctx) {
    tdr_t d;
    uint32_t i, n, c, h, w, fails;
    uint8_t sg_in, sg_out, sg_out2, sg_bad, su, su2, su3;

    /* ---- normal loopback: ld -> st round trip ---- */
    printf("== dmr ld/st normal loopback ==\n");
    {
        uint8_t src[128], dst[128]; /* (2,2,4,8) S8 */

        for (i = 0; i < 128; i++)
            src[i] = (uint8_t)(i * 7u + 3u);
        memcpy(ctx->global + GA, src, sizeof(src));
        memset(ctx->global + GO, 0, sizeof(dst));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 2, 2, 4, 8);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 2, 2, 4, 8, 0);
        su = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 2, 2, 4, 8);
        sg_out = mk_desc(ctx, &d);

        dmr_ld(su, sg_in, 0, DMR_MODE_NORMAL);
        dmr_st(sg_out, 0, su, REDUCE_MODE_NONE, DMR_MODE_NORMAL);
        memcpy(dst, ctx->global + GO, sizeof(dst));
        CHECK(memcmp(src, dst, sizeof(src)) == 0, "ld -> st loopback identical");

        /* reserved encodings: mode 3 and reduce 2 are silent; the targets
         * keep their poison (a stray violation would abort the run) */
        memset(ctx->global + GO, 0xAA, 128);
        memset(ctx->urf, 0x55, 128);
        dmr_ld(su, sg_in, 0, (dmr_mode_t)3);
        dmr_st(sg_out, 0, su, REDUCE_MODE_NONE, (dmr_mode_t)3);
        dmr_st(sg_out, 0, su, (reduce_mode_t)2, DMR_MODE_NORMAL);
        fails = 0;
        for (i = 0; i < 128; i++)
            if (ctx->urf[i] != 0x55 || ctx->global[GO + i] != 0xAA)
                fails++;
        CHECK(fails == 0, "reserved mode 3 / reduce 2: silent, targets untouched");

        /* negative: ld with a GLOBAL out -> violation */
        TNVI_BEGIN("load requires URF out") { dmr_ld(sg_out, sg_in, 0, DMR_MODE_NORMAL); }
        TNVI_END();

        tdr_free(ctx, sg_in);
        tdr_free(ctx, su);
        tdr_free(ctx, sg_out);
    }

    /* ---- CW transpose (N,W,H,C) out of (N,C,H,W), both sides ---- */
    printf("== dmr CW transpose ==\n");
    {
        uint8_t src[192], got[192]; /* in (2,3,4,8); cw out (2,8,4,3) */

        for (i = 0; i < 192; i++)
            src[i] = (uint8_t)(i * 5u + 1u);
        memcpy(ctx->global + GA, src, sizeof(src));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 2, 3, 4, 8);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 2, 8, 4, 3, 0);
        su = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 2, 8, 4, 3);
        sg_out = mk_desc(ctx, &d);

        dmr_ld(su, sg_in, 0, DMR_MODE_CW_TRANS);
        dmr_st(sg_out, 0, su, REDUCE_MODE_NONE, DMR_MODE_NORMAL);
        memcpy(got, ctx->global + GO, sizeof(got));
        CHECK(cw_fails(got, src) == 0, "ld CW: out(n,w,h,c) = in(n,c,h,w)");

        /* store side: normal load, CW store — same expectation */
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UB, 2, 3, 4, 8, 0);
        su2 = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GB, 2, 8, 4, 3);
        sg_out2 = mk_desc(ctx, &d);
        dmr_ld(su2, sg_in, 0, DMR_MODE_NORMAL);
        dmr_st(sg_out2, 0, su2, REDUCE_MODE_NONE, DMR_MODE_CW_TRANS);
        memcpy(got, ctx->global + GB, sizeof(got));
        CHECK(cw_fails(got, src) == 0, "st CW: out(n,w,h,c) = in(n,c,h,w)");

        /* negative: out not the CW permutation (same shape as in) */
        memset(ctx->global + GB, 0xAA, 192);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GB, 2, 3, 4, 8);
        sg_bad = mk_desc(ctx, &d);
        TNVI_BEGIN("do not pair") { dmr_st(sg_bad, 0, su2, REDUCE_MODE_NONE, DMR_MODE_CW_TRANS); }
        TNVI_END();
        fails = 0;
        for (i = 0; i < 192; i++)
            if (ctx->global[GB + i] != 0xAA)
                fails++;
        CHECK(fails == 0, "CW shape mismatch -> violation, target untouched");

        tdr_free(ctx, sg_in);
        tdr_free(ctx, su);
        tdr_free(ctx, sg_out);
        tdr_free(ctx, su2);
        tdr_free(ctx, sg_out2);
        tdr_free(ctx, sg_bad);
    }

    /* ---- NC transpose (C,N,H,W) out of (N,C,H,W), both sides ---- */
    printf("== dmr NC transpose ==\n");
    {
        uint8_t src[192], got[192]; /* in (2,3,4,8); nc out (3,2,4,8) */

        for (i = 0; i < 192; i++)
            src[i] = (uint8_t)(i * 9u + 7u);
        memcpy(ctx->global + GA, src, sizeof(src));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 2, 3, 4, 8);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 3, 2, 4, 8, 0);
        su = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 3, 2, 4, 8);
        sg_out = mk_desc(ctx, &d);

        dmr_ld(su, sg_in, 0, DMR_MODE_NC_TRANS);
        dmr_st(sg_out, 0, su, REDUCE_MODE_NONE, DMR_MODE_NORMAL);
        memcpy(got, ctx->global + GO, sizeof(got));
        CHECK(nc_fails(got, src) == 0, "ld NC: out(c,n,h,w) = in(n,c,h,w)");

        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UB, 2, 3, 4, 8, 0);
        su2 = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GB, 3, 2, 4, 8);
        sg_out2 = mk_desc(ctx, &d);
        dmr_ld(su2, sg_in, 0, DMR_MODE_NORMAL);
        dmr_st(sg_out2, 0, su2, REDUCE_MODE_NONE, DMR_MODE_NC_TRANS);
        memcpy(got, ctx->global + GB, sizeof(got));
        CHECK(nc_fails(got, src) == 0, "st NC: out(c,n,h,w) = in(n,c,h,w)");

        tdr_free(ctx, sg_in);
        tdr_free(ctx, su);
        tdr_free(ctx, sg_out);
        tdr_free(ctx, su2);
        tdr_free(ctx, sg_out2);
    }

    /* ---- mv inside URF: cw and nc transposes ---- */
    printf("== dmr mv URF transposes ==\n");
    {
        uint8_t src[192], got[192]; /* in (2,3,4,8) */

        for (i = 0; i < 192; i++)
            src[i] = (uint8_t)(i * 3u + 11u);
        memcpy(ctx->global + GA, src, sizeof(src));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 2, 3, 4, 8);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 2, 3, 4, 8, 0);
        su = mk_desc(ctx, &d);
        dmr_ld(su, sg_in, 0, DMR_MODE_NORMAL);

        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UB, 2, 8, 4, 3, 0);
        su2 = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 2, 8, 4, 3);
        sg_out = mk_desc(ctx, &d);
        dmr_mv(su2, 0, su, 0, DMR_MODE_CW_TRANS);
        dmr_st(sg_out, 0, su2, REDUCE_MODE_NONE, DMR_MODE_NORMAL);
        memcpy(got, ctx->global + GO, sizeof(got));
        CHECK(cw_fails(got, src) == 0, "mv CW (URF): out(n,w,h,c) = in(n,c,h,w)");

        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UO, 3, 2, 4, 8, 0);
        su3 = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GB, 3, 2, 4, 8);
        sg_out2 = mk_desc(ctx, &d);
        dmr_mv(su3, 0, su, 0, DMR_MODE_NC_TRANS);
        dmr_st(sg_out2, 0, su3, REDUCE_MODE_NONE, DMR_MODE_NORMAL);
        memcpy(got, ctx->global + GB, sizeof(got));
        CHECK(nc_fails(got, src) == 0, "mv NC (URF): out(c,n,h,w) = in(n,c,h,w)");

        /* negative: mv across locs (URF out, Global in) -> violation */
        TNVI_BEGIN("share the location") { dmr_mv(su2, 0, sg_in, 0, DMR_MODE_NORMAL); }
        TNVI_END();

        tdr_free(ctx, sg_in);
        tdr_free(ctx, su);
        tdr_free(ctx, su2);
        tdr_free(ctx, su3);
        tdr_free(ctx, sg_out);
        tdr_free(ctx, sg_out2);
    }

    /* ---- mv Global<->Global: Cont copy + FREE broadcast read ---- */
    printf("== dmr mv Global ==\n");
    {
        uint8_t src[192], got[192]; /* in (2,3,4,8) */
        uint8_t plane[32];          /* (1,2,4,8) S8, C broadcast source */
        tdr_stride_t s;
        int idx;

        for (i = 0; i < 192; i++)
            src[i] = (uint8_t)(i * 13u + 3u);
        memcpy(ctx->global + GA, src, sizeof(src));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 2, 3, 4, 8);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 2, 3, 4, 8);
        sg_out = mk_desc(ctx, &d);
        dmr_mv(sg_out, 0, sg_in, 0, DMR_MODE_NORMAL);
        memcpy(got, ctx->global + GO, sizeof(got));
        CHECK(memcmp(src, got, sizeof(src)) == 0, "mv Global copy identical");

        /* Global-side transposes: cw out (N,W,H,C), nc out (C,N,H,W) */
        tdr_free(ctx, sg_out);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 2, 8, 4, 3);
        sg_out = mk_desc(ctx, &d);
        dmr_mv(sg_out, 0, sg_in, 0, DMR_MODE_CW_TRANS);
        memcpy(got, ctx->global + GO, 192);
        CHECK(cw_fails(got, src) == 0, "mv CW (Global): out(n,w,h,c) = in(n,c,h,w)");

        tdr_free(ctx, sg_out);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GB, 3, 2, 4, 8);
        sg_out = mk_desc(ctx, &d);
        dmr_mv(sg_out, 0, sg_in, 0, DMR_MODE_NC_TRANS);
        memcpy(got, ctx->global + GB, 192);
        CHECK(nc_fails(got, src) == 0, "mv NC (Global): out(c,n,h,w) = in(n,c,h,w)");
        tdr_free(ctx, sg_in);
        tdr_free(ctx, sg_out);

        /* FREE read side with c_str=0: one source plane, every logical
         * channel reads it (broadcast read, FREE companion stride path) */
        for (i = 0; i < 32; i++)
            plane[i] = (uint8_t)(i * 17u + 9u);
        memcpy(ctx->global + GA, plane, 32);
        memset(ctx->global + GO, 0, 64);

        d = tdr_make_global(TDR_LAYOUT_FREE, TN_DTYPE_S8, GA, 1, 2, 4, 8);
        sg_in = mk_desc(ctx, &d);
        s = tdr_make_stride(1, 8, 0, 0); /* w=1, h=8, c=0 (broadcast), n=0 */
        idx = tdr_alloc(ctx);
        if (idx < 0 || tdr_shadow_write_stride(ctx, (uint8_t)idx, &s) != TN_SUCCESS) {
            printf("  FATAL: stride setup failed\n");
            g_fail++;
            su2 = 0;
        } else {
            su2 = (uint8_t)idx;
        }
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, 2, 4, 8);
        sg_out = mk_desc(ctx, &d);

        dmr_mv(sg_out, 0, sg_in, su2, DMR_MODE_NORMAL);
        memcpy(got, ctx->global + GO, 64);
        fails = 0;
        for (c = 0; c < 2; c++)
            if (memcmp(got + (size_t)c * 32u, plane, 32) != 0)
                fails++;
        CHECK(fails == 0, "mv Global FREE c_str=0: every channel reads the plane");

        tdr_free(ctx, sg_in);
        tdr_free(ctx, su2);
        tdr_free(ctx, sg_out);
    }

    /* ---- st reduce_add: dtypes + reduce x CW transpose ---- */
    printf("== dmr st reduce_add ==\n");
    {
        /* S8 saturate: 100+50 -> 127, -100-50 -> -128, 120+120 -> 127 */
        int8_t base8[8] = {100, -100, 120, 5, 0, 1, -1, 127};
        int8_t add8[8] = {50, -50, 120, 1, 7, 7, 7, 1};
        uint8_t exp8[8], got[8];

        memcpy(ctx->global + GO, base8, 8);
        memcpy(ctx->global + GB, add8, 8);
        for (i = 0; i < 8; i++)
            exp8[i] = (uint8_t)t_expect_int((int64_t)base8[i] + add8[i], 8, 1, OVF_MODE_SATURATE);

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GB, 1, 1, 1, 8);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, 1, 1, 8, 0);
        su = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, 1, 1, 8);
        sg_out = mk_desc(ctx, &d);

        dmr_ld(su, sg_in, 0, DMR_MODE_NORMAL);
        dmr_st(sg_out, 0, su, REDUCE_MODE_ADD, DMR_MODE_NORMAL);
        memcpy(got, ctx->global + GO, 8);
        CHECK(memcmp(got, exp8, 8) == 0, "st reduce_add S8: saturating accumulate");

        tdr_free(ctx, sg_in);
        tdr_free(ctx, su);
        tdr_free(ctx, sg_out);
    }
    {
        /* U8 saturate: 200+100 -> 255 */
        uint8_t base8[4] = {200, 100, 0, 255};
        uint8_t add8[4] = {100, 100, 200, 1};
        uint8_t exp8[4] = {255, 200, 200, 255};
        uint8_t got[4];

        memcpy(ctx->global + GO, base8, 4);
        memcpy(ctx->global + GB, add8, 4);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U8, GB, 1, 1, 1, 4);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_U8, UA, 1, 1, 1, 4, 0);
        su = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U8, GO, 1, 1, 1, 4);
        sg_out = mk_desc(ctx, &d);
        dmr_ld(su, sg_in, 0, DMR_MODE_NORMAL);
        dmr_st(sg_out, 0, su, REDUCE_MODE_ADD, DMR_MODE_NORMAL);
        memcpy(got, ctx->global + GO, 4);
        CHECK(memcmp(got, exp8, 4) == 0, "st reduce_add U8: saturating accumulate");

        tdr_free(ctx, sg_in);
        tdr_free(ctx, su);
        tdr_free(ctx, sg_out);
    }
    {
        /* BF16: 1.0+2.0=3.0 (0x4040); 0x3F81+0x3B80 RNE tie -> 0x3F82 (the
         * vec add pair); max+max saturates; -1.0-2.0=-3.0 (0xC040) */
        uint16_t base16[4] = {0x3F80, 0x3F81, 0x7F7F, 0xBF80};
        uint16_t add16[4] = {0x4000, 0x3B80, 0x7F7F, 0xC000};
        uint16_t exp16[4] = {0x4040, 0x3F82, 0x7F7F, 0xC040};
        uint16_t got[4];

        memcpy(ctx->global + GO, base16, 8);
        memcpy(ctx->global + GB, add16, 8);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_BF16, GB, 1, 1, 1, 4);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_BF16, UA, 1, 1, 1, 4, 0);
        su = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_BF16, GO, 1, 1, 1, 4);
        sg_out = mk_desc(ctx, &d);
        dmr_ld(su, sg_in, 0, DMR_MODE_NORMAL);
        dmr_st(sg_out, 0, su, REDUCE_MODE_ADD, DMR_MODE_NORMAL);
        memcpy(got, ctx->global + GO, 8);
        CHECK(memcmp(got, exp16, 8) == 0, "st reduce_add BF16: RNE + saturate");

        tdr_free(ctx, sg_in);
        tdr_free(ctx, su);
        tdr_free(ctx, sg_out);
    }
    {
        /* reduce x CW transpose: accumulator (2,8,4,3) at GO, add (2,3,4,8);
         * expected out(n,w,h,c) = base(n,w,h,c) + add(n,c,h,w) */
        uint8_t base[192], add[192], got[192];

        for (i = 0; i < 192; i++) {
            base[i] = (uint8_t)(i * 3u + 1u);
            add[i] = (uint8_t)(i * 5u + 2u);
        }
        memcpy(ctx->global + GO, base, 192);
        memcpy(ctx->global + GA, add, 192);

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 2, 3, 4, 8);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 2, 3, 4, 8, 0);
        su = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 2, 8, 4, 3);
        sg_out = mk_desc(ctx, &d);

        dmr_ld(su, sg_in, 0, DMR_MODE_NORMAL);
        dmr_st(sg_out, 0, su, REDUCE_MODE_ADD, DMR_MODE_CW_TRANS);
        memcpy(got, ctx->global + GO, 192);
        fails = 0;
        for (n = 0; n < 2; n++)
            for (w = 0; w < 8; w++)
                for (h = 0; h < 4; h++)
                    for (c = 0; c < 3; c++) {
                        uint32_t o = ((n * 8 + w) * 4 + h) * 3 + c;
                        int8_t b = (int8_t)base[o];
                        int8_t a = (int8_t)add[((n * 3 + c) * 4 + h) * 8 + w];
                        if (got[o] !=
                            (uint8_t)t_expect_int((int64_t)b + a, 8, 1, OVF_MODE_SATURATE))
                            fails++;
                    }
        CHECK(fails == 0, "st reduce_add x CW transpose: accumulate at the permuted coord");

        /* negative: dtype mismatch (S16 out, S8 in) -> violation */
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GB, 2, 8, 4, 3);
        sg_bad = mk_desc(ctx, &d);
        TNVI_BEGIN("do not pair") { dmr_st(sg_bad, 0, su, REDUCE_MODE_NONE, DMR_MODE_CW_TRANS); }
        TNVI_END();

        tdr_free(ctx, sg_in);
        tdr_free(ctx, su);
        tdr_free(ctx, sg_out);
        tdr_free(ctx, sg_bad);
    }

    /* ---- negative: st from a WAlign URF input (conv kernel layout) ---- */
    /* ---- ld/st with FREE layout (companion stride): padding skipped,
     * stride 0 broadcasts; the transposed FREE store pins the positional
     * stride interpretation (address map, tensor.cpp) ---- */
    printf("== dmr ld/st FREE (stride) ==\n");
    {
        uint8_t src[64], dst[24], exp[64];
        tdr_stride_t strd;
        int sstr, sstr2, sstr3;
        uint32_t q, r, s;

        /* A: strided load (1,2,3,4), w=1 h=8 c=32 — row/channel padding
         * sits between the data and must be skipped */
        memset(src, 0xEE, sizeof(src));
        for (c = 0; c < 2; c++)
            for (h = 0; h < 3; h++)
                for (w = 0; w < 4; w++)
                    src[(size_t)c * 32 + (size_t)h * 8 + w] = (uint8_t)(c * 100 + h * 10 + w);
        memcpy(ctx->global + GA, src, sizeof(src));
        memset(ctx->global + GO, 0, sizeof(dst));

        d = tdr_make_global(TDR_LAYOUT_FREE, TN_DTYPE_S8, GA, 1, 2, 3, 4);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, 2, 3, 4, 0);
        su = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, 2, 3, 4);
        sg_out = mk_desc(ctx, &d);
        strd = tdr_make_stride(1, 8, 32, 0); /* w=1, h=8, c=32, n=0 */
        sstr = tdr_alloc(ctx);
        if (sstr < 0 || tdr_shadow_write_stride(ctx, (uint8_t)sstr, &strd) != TN_SUCCESS) {
            printf("  FATAL: stride setup failed\n");
            g_fail++;
            sstr = 0;
        }

        dmr_ld(su, sg_in, (uint8_t)sstr, DMR_MODE_NORMAL);
        dmr_st(sg_out, 0, su, REDUCE_MODE_NONE, DMR_MODE_NORMAL);
        memcpy(dst, ctx->global + GO, sizeof(dst));
        fails = 0;
        for (c = 0; c < 2; c++)
            for (h = 0; h < 3; h++)
                for (w = 0; w < 4; w++)
                    if (dst[(c * 3 + h) * 4 + w] != src[(size_t)c * 32 + (size_t)h * 8 + w])
                        fails++;
        CHECK(fails == 0, "ld FREE: stride pairs honored, padding skipped");

        /* negative: the FREE input without a companion stride slot (the
         * descriptor slot itself is not a stride) -> violation */
        TNVI_BEGIN("FREE layout lacks") { dmr_ld(su, sg_in, sg_in, DMR_MODE_NORMAL); }
        TNVI_END();

        tdr_free(ctx, sg_in);
        tdr_free(ctx, su);
        tdr_free(ctx, sg_out);
        tdr_free(ctx, (uint8_t)sstr);

        /* B: strided store (1,2,2,4) into w=1 h=8 c=32 — the padding
         * keeps its poison */
        for (c = 0; c < 2; c++)
            for (h = 0; h < 2; h++)
                for (w = 0; w < 4; w++)
                    src[(c * 2 + h) * 4 + w] = (uint8_t)(c * 40 + h * 10 + w);
        memcpy(ctx->global + GA, src, 16);
        memset(ctx->global + GO, 0xAA, sizeof(exp));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 2, 2, 4);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UB, 1, 2, 2, 4, 0);
        su2 = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_FREE, TN_DTYPE_S8, GO, 1, 2, 2, 4);
        sg_out = mk_desc(ctx, &d);
        strd = tdr_make_stride(1, 8, 32, 0);
        sstr = tdr_alloc(ctx);
        if (sstr < 0 || tdr_shadow_write_stride(ctx, (uint8_t)sstr, &strd) != TN_SUCCESS) {
            printf("  FATAL: stride setup failed\n");
            g_fail++;
            sstr = 0;
        }

        top_ldst_load(su2, sg_in, 0, LDST_MODE_NORMAL);
        dmr_st(sg_out, (uint8_t)sstr, su2, REDUCE_MODE_NONE, DMR_MODE_NORMAL);

        memset(exp, 0xAA, sizeof(exp));
        for (c = 0; c < 2; c++)
            for (h = 0; h < 2; h++)
                for (w = 0; w < 4; w++)
                    exp[(size_t)c * 32 + (size_t)h * 8 + w] = src[(c * 2 + h) * 4 + w];
        fails = 0;
        for (i = 0; i < 64; i++)
            if (ctx->global[GO + i] != exp[i])
                fails++;
        CHECK(fails == 0, "st FREE: strided write, padding keeps poison");

        tdr_free(ctx, sg_out);
        tdr_free(ctx, (uint8_t)sstr);

        /* D: FREE x CW — the strides pair with the OUTPUT's positional
         * axes (out (1,W,H,C): axis1 pitch 8, axis2 4, axis3 1); pins the
         * ref interpretation of transpose x FREE (address map),
         * out(n,w,h,c) = in(n,c,h,w) */
        memset(ctx->global + GO, 0xAA, sizeof(exp));
        d = tdr_make_global(TDR_LAYOUT_FREE, TN_DTYPE_S8, GO, 1, 4, 2, 2);
        sg_out = mk_desc(ctx, &d);
        strd = tdr_make_stride(1, 4, 8, 0); /* axis3=1, axis2=4, axis1=8 */
        sstr3 = tdr_alloc(ctx);
        if (sstr3 < 0 || tdr_shadow_write_stride(ctx, (uint8_t)sstr3, &strd) != TN_SUCCESS) {
            printf("  FATAL: stride setup failed\n");
            g_fail++;
            sstr3 = 0;
        }

        dmr_st(sg_out, (uint8_t)sstr3, su2, REDUCE_MODE_NONE, DMR_MODE_CW_TRANS);
        memset(exp, 0xAA, sizeof(exp));
        for (q = 0; q < 4; q++)         /* out axis1 = the input's W value */
            for (r = 0; r < 2; r++)     /* h */
                for (s = 0; s < 2; s++) /* out axis3 = the input's C value */
                    exp[(size_t)q * 8 + (size_t)r * 4 + s] = src[(s * 2 + r) * 4 + q];
        fails = 0;
        for (i = 0; i < 64; i++)
            if (ctx->global[GO + i] != exp[i])
                fails++;
        CHECK(fails == 0, "st FREE x CW: positional strides on the permuted shape");

        tdr_free(ctx, sg_out);
        tdr_free(ctx, (uint8_t)sstr3);
        tdr_free(ctx, su2);
        tdr_free(ctx, sg_in);

        /* C: stride 0 broadcasts — c_str=0 makes every channel read the
         * same plane */
        for (i = 0; i < 8; i++)
            src[i] = (uint8_t)(i * 11u + 5u);
        memcpy(ctx->global + GA, src, 8);
        memset(ctx->global + GB, 0, 16);

        d = tdr_make_global(TDR_LAYOUT_FREE, TN_DTYPE_S8, GA, 1, 2, 2, 4);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, 2, 2, 4, 0);
        su = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GB, 1, 2, 2, 4);
        sg_out = mk_desc(ctx, &d);
        strd = tdr_make_stride(1, 4, 0, 0); /* w=1, h=4, c=0 (broadcast) */
        sstr2 = tdr_alloc(ctx);
        if (sstr2 < 0 || tdr_shadow_write_stride(ctx, (uint8_t)sstr2, &strd) != TN_SUCCESS) {
            printf("  FATAL: stride setup failed\n");
            g_fail++;
            sstr2 = 0;
        }

        dmr_ld(su, sg_in, (uint8_t)sstr2, DMR_MODE_NORMAL);
        dmr_st(sg_out, 0, su, REDUCE_MODE_NONE, DMR_MODE_NORMAL);
        memcpy(dst, ctx->global + GB, 16);
        fails = 0;
        for (c = 0; c < 2; c++)
            if (memcmp(dst + (size_t)c * 8u, src, 8) != 0)
                fails++;
        CHECK(fails == 0, "ld FREE c_str=0: one plane broadcast to every channel");

        tdr_free(ctx, sg_in);
        tdr_free(ctx, su);
        tdr_free(ctx, sg_out);
        tdr_free(ctx, (uint8_t)sstr2);
    }

    printf("== dmr negatives ==\n");
    {
        uint8_t wsrc[320]; /* (2,4,4,10) WAlign via top_ldst_load */

        for (i = 0; i < 320; i++)
            wsrc[i] = (uint8_t)(i * 11u + 5u);
        memcpy(ctx->global + GA, wsrc, sizeof(wsrc));
        memset(ctx->global + GO, 0xAA, 320);

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 2, 4, 4, 10);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_WALIGN, TN_DTYPE_S8, UA, 2, 4, 4, 10, 0);
        su = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 2, 4, 4, 10);
        sg_out = mk_desc(ctx, &d);
        top_ldst_load(su, sg_in, 0, LDST_MODE_NORMAL);
        TNVI_BEGIN("URF side layout must be HWAlign") {
            dmr_st(sg_out, 0, su, REDUCE_MODE_NONE, DMR_MODE_NORMAL);
        }
        TNVI_END();
        fails = 0;
        for (i = 0; i < 320; i++)
            if (ctx->global[GO + i] != 0xAA)
                fails++;
        CHECK(fails == 0, "st from WAlign URF input -> violation, target untouched");

        tdr_free(ctx, sg_in);
        tdr_free(ctx, su);
        tdr_free(ctx, sg_out);
    }

    /* ---- dmr_ld accepts WAlign (conv kernel rows): with w*esz == 32B the
     * row layout coincides with HWAlign, so the read-back runs through a
     * HWAlign view of the same bytes — end-user perspective, no URF byte
     * assertions ---- */
    printf("== dmr_ld WAlign ==\n");
    {
        uint8_t src[256], dst[256]; /* (2,2,2,32) S8: w*esz = 32B */

        for (i = 0; i < 256; i++)
            src[i] = (uint8_t)(i * 7u + 3u);
        memcpy(ctx->global + GA, src, sizeof(src));
        memset(ctx->global + GO, 0, sizeof(dst));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 2, 2, 2, 32);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_WALIGN, TN_DTYPE_S8, UA, 2, 2, 2, 32, 0);
        su = mk_desc(ctx, &d);
        dmr_ld(su, sg_in, 0, DMR_MODE_NORMAL);

        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 2, 2, 2, 32, 0);
        su2 = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 2, 2, 2, 32);
        sg_out = mk_desc(ctx, &d);
        dmr_st(sg_out, 0, su2, REDUCE_MODE_NONE, DMR_MODE_NORMAL);
        memcpy(dst, ctx->global + GO, sizeof(dst));
        CHECK(memcmp(src, dst, sizeof(src)) == 0, "dmr_ld WAlign rows land like HWAlign planes");

        tdr_free(ctx, sg_in);
        tdr_free(ctx, su);
        tdr_free(ctx, su2);
        tdr_free(ctx, sg_out);
    }

    /* ---- negative: WAlign URF output only pairs with the normal load
     * (mode 0); a transpose mode is a violation ---- */
    printf("== dmr_ld WAlign x transpose ==\n");
    {
        uint64_t lane_bytes = ctx->info.urf_bytes / ctx->info.smc_num;
        uint8_t src[256];

        for (i = 0; i < 256; i++)
            src[i] = (uint8_t)(i * 3u + 1u);
        memcpy(ctx->global + GA, src, sizeof(src));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 2, 2, 2, 32);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_WALIGN, TN_DTYPE_S8, UA, 2, 32, 2, 2, 0);
        su = mk_desc(ctx, &d);

        for (c = 0; c < 2; c++)
            memset(ctx->urf + (uint64_t)c * lane_bytes, 0x55, 128);
        TNVI_BEGIN("only legal for the normal load") { dmr_ld(su, sg_in, 0, DMR_MODE_CW_TRANS); }
        TNVI_END();
        fails = 0;
        for (c = 0; c < 2; c++)
            for (i = 0; i < 128; i++)
                if (ctx->urf[(uint64_t)c * lane_bytes + i] != 0x55)
                    fails++;
        CHECK(fails == 0, "WAlign x transpose -> violation, URF untouched");

        tdr_free(ctx, sg_in);
        tdr_free(ctx, su);
    }

    /* ---- c-block: C > smc_num wraps to the same lane's next c-block ---- */
    printf("== dmr c-block (C > smc_num) ==\n");
    {
        const uint32_t C = ctx->info.smc_num + 2;
        uint8_t src[C * 32], dst[C * 32]; /* (1,C,4,8): 32 elements/channel */

        for (c = 0; c < C; c++)
            for (i = 0; i < 32; i++)
                src[c * 32 + i] = (uint8_t)(c * 29u + i * 13u + 7u);
        memcpy(ctx->global + GA, src, C * 32);
        memset(ctx->global + GO, 0, C * 32);

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, (uint16_t)C, 4, 8);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, (uint16_t)C, 4, 8, 0);
        su = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, (uint16_t)C, 4, 8);
        sg_out = mk_desc(ctx, &d);

        dmr_ld(su, sg_in, 0, DMR_MODE_NORMAL);
        dmr_st(sg_out, 0, su, REDUCE_MODE_NONE, DMR_MODE_NORMAL);
        memcpy(dst, ctx->global + GO, C * 32);
        CHECK(memcmp(src, dst, C * 32) == 0, "ld -> st loopback: C > smc_num wraps c-blocks");

        /* CW transpose off the c-block input: out (1,8,4,C) — the reads
         * wrap c-blocks on lanes, the writes land on 8 direct lanes */
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UB, 1, 8, 4, (uint16_t)C, 0);
        su2 = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GB, 1, 8, 4, (uint16_t)C);
        sg_out2 = mk_desc(ctx, &d);
        dmr_ld(su2, sg_in, 0, DMR_MODE_CW_TRANS);
        dmr_st(sg_out2, 0, su2, REDUCE_MODE_NONE, DMR_MODE_NORMAL);
        memcpy(dst, ctx->global + GB, C * 32);
        fails = 0;
        for (w = 0; w < 8; w++)
            for (h = 0; h < 4; h++)
                for (c = 0; c < C; c++)
                    if (dst[(w * 4 + h) * C + c] != src[(c * 4 + h) * 8 + w])
                        fails++;
        CHECK(fails == 0, "ld CW off c-block: out(n,w,h,c) = in(n,c,h,w)");

        tdr_free(ctx, sg_in);
        tdr_free(ctx, su);
        tdr_free(ctx, sg_out);
        tdr_free(ctx, su2);
        tdr_free(ctx, sg_out2);
    }
}
