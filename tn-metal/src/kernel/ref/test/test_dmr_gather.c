// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/test/test_dmr_gather.c
 *
 * dmr rearrange family cases: gather with Global-side operands (in/idx read
 * straight from Global; a Global output keeps its prefill under
 * PAD_VAL_KEEP), scatter with the dmr row-index shape (mode H index
 * (1,C,K,1)), scatter reduce_add on a Global output (duplicates
 * accumulate, S16 saturate, OOB drop), plus pinning/placement/reduce
 * negatives. The all-URF equivalence matrix of the traversal core lives in
 * test_vec_gather.c (shared tensor.cpp core); expectations are pure
 * test-side index arithmetic.
 * ========================================================================== */
#include "smoke.h"

static void gs_free_case(tnkr_ctx_t *ctx);

void suite_dmr_gather(tnkr_ctx_t *ctx) {
    tdr_t d;
    uint32_t i, c, h, k, w, fails;
    uint8_t sg_in, sg_idx, sg_out, su_in, su_idx, su_out, sg_bad;

    /* ---- gather W: in/idx on Global, out URF, zero fill ---- */
    printf("== dmr gather ==\n");
    {
        int16_t in[24];              /* (1,2,3,4) */
        uint16_t idx[6] = {0, 2, 7,  /* ch0: coord 7 OOB (span W = 4) */
                           3, 1, 4}; /* ch1: coord 4 OOB */
        uint16_t poison[18];
        int16_t got[18]; /* out (1,2,3,3) */

        for (c = 0; c < 2; c++)
            for (h = 0; h < 3; h++)
                for (w = 0; w < 4; w++)
                    in[(c * 3 + h) * 4 + w] = (int16_t)(c * 100 + h * 10 + w);
        for (i = 0; i < 18; i++)
            poison[i] = 0xAAAA;

        memcpy(ctx->global + GA, in, sizeof(in));
        memcpy(ctx->global + GB, idx, sizeof(idx));
        memcpy(ctx->global + GO, poison, sizeof(poison));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GA, 1, 2, 3, 4);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GB, 1, 2, 1, 3);
        sg_idx = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GO, 1, 2, 3, 3);
        sg_out = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S16, UA, 1, 2, 3, 3, 0);
        su_out = mk_desc(ctx, &d);

        top_ldst_load(su_out, sg_out, 0, LDST_MODE_NORMAL); /* poison prefill */
        dmr_gather(su_out, sg_in, 0, sg_idx, 0, AXIS_MODE_W, PAD_VAL_ZERO);
        top_ldst_store(sg_out, 0, su_out, LDST_MODE_NORMAL);
        memcpy(got, ctx->global + GO, sizeof(got));

        fails = 0;
        for (c = 0; c < 2; c++)
            for (h = 0; h < 3; h++)
                for (k = 0; k < 3; k++) {
                    uint16_t coord = idx[c * 3 + k];
                    int16_t exp = (coord < 4) ? in[(c * 3 + h) * 4 + coord] : 0;
                    if (got[(c * 3 + h) * 3 + k] != exp)
                        fails++;
                }
        CHECK(fails == 0, "gather W: Global in/idx -> URF out, zero fill");

        tdr_free(ctx, sg_in);
        tdr_free(ctx, sg_idx);
        tdr_free(ctx, sg_out);
        tdr_free(ctx, su_out);
    }

    /* ---- gather H: out on Global, KEEP fill keeps the prefill ---- */
    {
        int16_t in[24];              /* (1,2,3,4) */
        uint16_t idx[6] = {0, 2, 3,  /* ch0: coord 3 OOB (span H = 3) */
                           2, 1, 0}; /* ch1 */
        int16_t got[24];             /* out (1,2,3,4): (c,k,w) */

        for (c = 0; c < 2; c++)
            for (h = 0; h < 3; h++)
                for (w = 0; w < 4; w++)
                    in[(c * 3 + h) * 4 + w] = (int16_t)(c * 100 + h * 10 + w);

        memcpy(ctx->global + GA, in, sizeof(in));
        memcpy(ctx->global + GB, idx, sizeof(idx));
        memset(ctx->global + GO, 0xAA, sizeof(got)); /* KEEP prefill */

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GA, 1, 2, 3, 4);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S16, UA, 1, 2, 3, 4, 0);
        su_in = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GB, 1, 2, 1, 3);
        sg_idx = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_U16, UB, 1, 2, 1, 3, 0);
        su_idx = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GO, 1, 2, 3, 4);
        sg_out = mk_desc(ctx, &d);

        top_ldst_load(su_in, sg_in, 0, LDST_MODE_NORMAL);
        top_ldst_load(su_idx, sg_idx, 0, LDST_MODE_NORMAL);
        dmr_gather(sg_out, su_in, 0, su_idx, 0, AXIS_MODE_H, PAD_VAL_KEEP);
        memcpy(got, ctx->global + GO, sizeof(got));

        fails = 0;
        for (c = 0; c < 2; c++)
            for (k = 0; k < 3; k++) {
                uint16_t coord = idx[c * 3 + k];
                for (w = 0; w < 4; w++) {
                    int16_t exp = (int16_t)0xAAAA; /* KEEP: untouched prefill */
                    if (coord < 3)
                        exp = in[(c * 3 + coord) * 4 + w];
                    if (got[(c * 3 + k) * 4 + w] != exp)
                        fails++;
                }
            }
        CHECK(fails == 0, "gather H: Global out, KEEP keeps the prefill on OOB");

        /* negative: N pinning — in (2,2,3,4) -> violation */
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GB, 2, 2, 3, 4);
        sg_bad = mk_desc(ctx, &d);
        TNVI_BEGIN("pin N to 1") {
            dmr_gather(sg_out, sg_bad, 0, su_idx, 0, AXIS_MODE_W, PAD_VAL_ZERO);
        }
        TNVI_END();

        tdr_free(ctx, sg_in);
        tdr_free(ctx, su_in);
        tdr_free(ctx, sg_idx);
        tdr_free(ctx, su_idx);
        tdr_free(ctx, sg_out);
        tdr_free(ctx, sg_bad);
    }

    /* ---- scatter W: URF in/out, Global idx, OOB drop + dup last-wins ---- */
    printf("== dmr scatter ==\n");
    {
        int16_t in[18];              /* compacted (1,2,3,3) */
        uint16_t idx[6] = {0, 2, 2,  /* ch0: dup 2 -> last write wins */
                           3, 1, 7}; /* ch1: coord 7 OOB -> dropped */
        int16_t got[24];             /* out (1,2,3,4) */

        for (c = 0; c < 2; c++)
            for (h = 0; h < 3; h++)
                for (k = 0; k < 3; k++)
                    in[(c * 3 + h) * 3 + k] = (int16_t)(c * 100 + h * 10 + k);

        memcpy(ctx->global + GA, in, sizeof(in));
        memcpy(ctx->global + GB, idx, sizeof(idx));
        memset(ctx->global + GO, 0xAA, sizeof(got));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GA, 1, 2, 3, 3);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S16, UA, 1, 2, 3, 3, 0);
        su_in = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GB, 1, 2, 1, 3);
        sg_idx = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GO, 1, 2, 3, 4);
        sg_out = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S16, UB, 1, 2, 3, 4, 0);
        su_out = mk_desc(ctx, &d);

        top_ldst_load(su_in, sg_in, 0, LDST_MODE_NORMAL);
        top_ldst_load(su_out, sg_out, 0, LDST_MODE_NORMAL); /* poison prefill */
        dmr_scatter(su_out, su_in, 0, sg_idx, 0, REDUCE_MODE_NONE, AXIS_MODE_W);
        top_ldst_store(sg_out, 0, su_out, LDST_MODE_NORMAL);
        memcpy(got, ctx->global + GO, sizeof(got));

        fails = 0;
        for (c = 0; c < 2; c++)
            for (h = 0; h < 3; h++) {
                /* cell (c,h,w): starts at the prefill, then ascending-k
                 * writes land last-wins; OOB coords dropped */
                int16_t cell[4] = {(int16_t)0xAAAA, (int16_t)0xAAAA, (int16_t)0xAAAA,
                                   (int16_t)0xAAAA};
                for (k = 0; k < 3; k++) {
                    uint16_t coord = idx[c * 3 + k];
                    if (coord < 4)
                        cell[coord] = in[(c * 3 + h) * 3 + k];
                }
                for (w = 0; w < 4; w++)
                    if (got[(c * 3 + h) * 4 + w] != cell[w])
                        fails++;
            }
        CHECK(fails == 0, "scatter W: dup last-wins, OOB drop, prefill elsewhere");

        tdr_free(ctx, sg_in);
        tdr_free(ctx, su_in);
        tdr_free(ctx, sg_idx);
        tdr_free(ctx, sg_out);
        tdr_free(ctx, su_out);
    }

    /* ---- scatter H: row-index shape (1,C,K,1), dup last-wins ---- */
    {
        int16_t in[30];              /* compacted (1,2,3,5): K = dh = 3 */
        uint16_t idx[6] = {0, 2, 1,  /* ch0 */
                           3, 3, 0}; /* ch1: dup 3 -> last write wins */
        int16_t got[40];             /* out (1,2,4,5): H = 4 */

        for (c = 0; c < 2; c++)
            for (k = 0; k < 3; k++)
                for (w = 0; w < 5; w++)
                    in[(c * 3 + k) * 5 + w] = (int16_t)(c * 100 + k * 10 + w);

        memcpy(ctx->global + GA, in, sizeof(in));
        memcpy(ctx->global + GB, idx, sizeof(idx));
        memset(ctx->global + GO, 0xAA, sizeof(got));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GA, 1, 2, 3, 5);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S16, UA, 1, 2, 3, 5, 0);
        su_in = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GB, 1, 2, 3, 1);
        sg_idx = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GO, 1, 2, 4, 5);
        sg_out = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S16, UB, 1, 2, 4, 5, 0);
        su_out = mk_desc(ctx, &d);

        top_ldst_load(su_in, sg_in, 0, LDST_MODE_NORMAL);
        top_ldst_load(su_out, sg_out, 0, LDST_MODE_NORMAL); /* poison prefill */
        dmr_scatter(su_out, su_in, 0, sg_idx, 0, REDUCE_MODE_NONE, AXIS_MODE_H);
        top_ldst_store(sg_out, 0, su_out, LDST_MODE_NORMAL);
        memcpy(got, ctx->global + GO, sizeof(got));

        fails = 0;
        for (c = 0; c < 2; c++)
            for (w = 0; w < 5; w++) {
                int16_t cell[4] = {(int16_t)0xAAAA, (int16_t)0xAAAA, (int16_t)0xAAAA,
                                   (int16_t)0xAAAA};
                for (k = 0; k < 3; k++) {
                    uint16_t coord = idx[c * 3 + k];
                    if (coord < 4)
                        cell[coord] = in[(c * 3 + k) * 5 + w];
                }
                for (h = 0; h < 4; h++)
                    if (got[(c * 4 + h) * 5 + w] != cell[h])
                        fails++;
            }
        CHECK(fails == 0, "scatter H: row idx (1,C,K,1), dup last-wins");

        /* negative: mode H index (1,C,1,K) — the twin shape, wrong axis
         * for the dmr row encoding (extent check fires) -> violation */
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GB, 1, 2, 1, 3);
        sg_bad = mk_desc(ctx, &d);
        TNVI_BEGIN("input H must match index K") {
            dmr_scatter(su_out, su_in, 0, sg_bad, 0, REDUCE_MODE_NONE, AXIS_MODE_H);
        }
        TNVI_END();
        tdr_free(ctx, sg_bad);

        /* negative: index (1,C,K,2) — right axis, wrong W extent -> violation */
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GB, 1, 2, 3, 2);
        sg_bad = mk_desc(ctx, &d);
        TNVI_BEGIN("(1,C,K,1)") {
            dmr_scatter(su_out, su_in, 0, sg_bad, 0, REDUCE_MODE_NONE, AXIS_MODE_H);
        }
        TNVI_END();

        tdr_free(ctx, sg_in);
        tdr_free(ctx, su_in);
        tdr_free(ctx, sg_idx);
        tdr_free(ctx, sg_out);
        tdr_free(ctx, su_out);
        tdr_free(ctx, sg_bad);
    }

    /* ---- scatter reduce_add: all-Global, dup accumulate, S16 saturate ---- */
    /* ---- gather/scatter with C > smc_num: the per-channel pick wraps to
     * c-blocks of the same lane (tensor.cpp lane law) ---- */
    printf("== dmr gather/scatter c-block (C > smc_num) ==\n");
    {
        const uint32_t C = ctx->info.smc_num + 2;
        int16_t gin[C * 4];  /* gather in (1,C,1,4) */
        uint16_t gix[C * 3]; /* (1,C,1,3) */
        int16_t ggot[C * 3]; /* gather out (1,C,1,3) */

        /* channel-dependent patterns: a c-block placement bug crosses
         * channels and the case sees wrong values immediately */
        for (c = 0; c < C; c++) {
            for (w = 0; w < 4; w++)
                gin[c * 4 + w] = (int16_t)(c * 97 + w * 11);
            for (k = 0; k < 3; k++)
                gix[c * 3 + k] = (uint16_t)((c + k) % 4);
        }
        memcpy(ctx->global + GA, gin, C * 8);
        memcpy(ctx->global + GB, gix, C * 6);

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GA, 1, (uint16_t)C, 1, 4);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GB, 1, (uint16_t)C, 1, 3);
        sg_idx = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S16, UA, 1, (uint16_t)C, 1, 4, 0);
        su_in = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_U16, UB, 1, (uint16_t)C, 1, 3, 0);
        su_idx = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S16, UO, 1, (uint16_t)C, 1, 3, 0);
        su_out = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GO, 1, (uint16_t)C, 1, 3);
        sg_out = mk_desc(ctx, &d);

        top_ldst_load(su_in, sg_in, 0, LDST_MODE_NORMAL);
        top_ldst_load(su_idx, sg_idx, 0, LDST_MODE_NORMAL);
        dmr_gather(su_out, su_in, 0, su_idx, 0, AXIS_MODE_W, PAD_VAL_ZERO);
        top_ldst_store(sg_out, 0, su_out, LDST_MODE_NORMAL);
        memcpy(ggot, ctx->global + GO, C * 6);
        fails = 0;
        for (c = 0; c < C; c++)
            for (k = 0; k < 3; k++) {
                uint16_t coord = gix[c * 3 + k];
                if (ggot[c * 3 + k] != gin[c * 4 + coord])
                    fails++;
            }
        CHECK(fails == 0, "gather W c-block: C > smc_num wraps, channels independent");

        tdr_free(ctx, sg_in);
        tdr_free(ctx, sg_idx);
        tdr_free(ctx, sg_out);
        tdr_free(ctx, su_in);
        tdr_free(ctx, su_idx);
        tdr_free(ctx, su_out);
    }
    {
        const uint32_t C = ctx->info.smc_num + 2;
        int16_t sin_[C * 6]; /* compacted (1,C,3,2): K = dh = 3, W = 2 */
        uint16_t six[C * 3]; /* row idx (1,C,3,1) */
        int16_t sgot[C * 8]; /* out (1,C,4,2): H = 4 */
        int16_t cell[4][2];

        for (c = 0; c < C; c++) {
            for (k = 0; k < 3; k++) {
                sin_[(c * 3 + k) * 2 + 0] = (int16_t)(c * 53 + k * 7);
                sin_[(c * 3 + k) * 2 + 1] = (int16_t)(c * 53 + k * 7 + 1);
                six[c * 3 + k] = (uint16_t)((c + k) % 4); /* 3 of 4 rows hit */
            }
        }
        memcpy(ctx->global + GA, sin_, C * 12);
        memcpy(ctx->global + GB, six, C * 6);
        memset(ctx->global + GO, 0xAA, C * 16); /* KEEP-style prefill */

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GA, 1, (uint16_t)C, 3, 2);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GB, 1, (uint16_t)C, 3, 1);
        sg_idx = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S16, UA, 1, (uint16_t)C, 3, 2, 0);
        su_in = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_U16, UB, 1, (uint16_t)C, 3, 1, 0);
        su_idx = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S16, UO, 1, (uint16_t)C, 4, 2, 0);
        su_out = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GO, 1, (uint16_t)C, 4, 2);
        sg_out = mk_desc(ctx, &d);

        top_ldst_load(su_in, sg_in, 0, LDST_MODE_NORMAL);
        top_ldst_load(su_idx, sg_idx, 0, LDST_MODE_NORMAL);
        top_ldst_load(su_out, sg_out, 0, LDST_MODE_NORMAL); /* poison prefill */
        dmr_scatter(su_out, su_in, 0, su_idx, 0, REDUCE_MODE_NONE, AXIS_MODE_H);
        top_ldst_store(sg_out, 0, su_out, LDST_MODE_NORMAL);
        memcpy(sgot, ctx->global + GO, C * 16);
        fails = 0;
        for (c = 0; c < C; c++) {
            for (h = 0; h < 4; h++)
                for (w = 0; w < 2; w++)
                    cell[h][w] = (int16_t)0xAAAA; /* untouched rows keep prefill */
            for (k = 0; k < 3; k++) {
                uint16_t coord = six[c * 3 + k];
                for (w = 0; w < 2; w++)
                    cell[coord][w] = sin_[(c * 3 + k) * 2 + w];
            }
            for (h = 0; h < 4; h++)
                for (w = 0; w < 2; w++)
                    if (sgot[(c * 4 + h) * 2 + w] != cell[h][w])
                        fails++;
        }
        CHECK(fails == 0, "scatter H row idx c-block: channels wrap, untouched rows keep");

        tdr_free(ctx, sg_in);
        tdr_free(ctx, sg_idx);
        tdr_free(ctx, sg_out);
        tdr_free(ctx, su_in);
        tdr_free(ctx, su_idx);
        tdr_free(ctx, su_out);
    }

    printf("== dmr scatter reduce_add ==\n");
    {
        int16_t base[20];            /* out (1,2,2,5) prefill */
        int16_t in[12];              /* compacted (1,2,2,3): K = dw = 3 */
        uint16_t idx[6] = {0, 2, 2,  /* ch0: dup 2 -> accumulates twice */
                           4, 1, 6}; /* ch1: coord 6 OOB -> dropped */
        int16_t got[20];

        for (c = 0; c < 2; c++)
            for (h = 0; h < 2; h++)
                for (w = 0; w < 5; w++)
                    base[(c * 2 + h) * 5 + w] = (int16_t)(10000 + c * 100 + h * 10 + w);
        for (c = 0; c < 2; c++)
            for (h = 0; h < 2; h++)
                for (k = 0; k < 3; k++)
                    in[(c * 2 + h) * 3 + k] = (int16_t)(20000 + 5000 * k); /* 20000/25000/25000 */

        memcpy(ctx->global + GO, base, sizeof(base));
        memcpy(ctx->global + GA, in, sizeof(in));
        memcpy(ctx->global + GB, idx, sizeof(idx));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GO, 1, 2, 2, 5);
        sg_out = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GA, 1, 2, 2, 3);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GB, 1, 2, 1, 3);
        sg_idx = mk_desc(ctx, &d);

        dmr_scatter(sg_out, sg_in, 0, sg_idx, 0, REDUCE_MODE_ADD, AXIS_MODE_W);
        memcpy(got, ctx->global + GO, sizeof(got));

        fails = 0;
        for (c = 0; c < 2; c++)
            for (h = 0; h < 2; h++) {
                int32_t cell[5];
                for (w = 0; w < 5; w++)
                    cell[w] = base[(c * 2 + h) * 5 + w];
                for (k = 0; k < 3; k++) {
                    uint16_t coord = idx[c * 3 + k];
                    if (coord < 5)
                        cell[coord] += in[(c * 2 + h) * 3 + k];
                }
                for (w = 0; w < 5; w++)
                    if (got[(c * 2 + h) * 5 + w] !=
                        (int16_t)t_expect_int(cell[w], 16, 1, OVF_MODE_SATURATE))
                        fails++;
            }
        CHECK(fails == 0, "scatter reduce_add: Global out, dup accumulate, OOB drop");

        /* negative: reduce_add onto a URF output -> violation */
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S16, UO, 1, 2, 2, 5, 0);
        su_out = mk_desc(ctx, &d);
        TNVI_BEGIN("requires a Global output") {
            dmr_scatter(su_out, sg_in, 0, sg_idx, 0, REDUCE_MODE_ADD, AXIS_MODE_W);
        }
        TNVI_END();

        /* negative: index dtype S8 -> violation */
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GB, 1, 2, 1, 3);
        sg_bad = mk_desc(ctx, &d);
        TNVI_BEGIN("index dtype") {
            dmr_scatter(sg_out, sg_in, 0, sg_bad, 0, REDUCE_MODE_NONE, AXIS_MODE_W);
        }
        TNVI_END();

        tdr_free(ctx, sg_out);
        tdr_free(ctx, sg_in);
        tdr_free(ctx, sg_idx);
        tdr_free(ctx, su_out);
        tdr_free(ctx, sg_bad);
    }

    gs_free_case(ctx);
}
/* ---- gather/scatter Global FREE: strided in/idx views over wider
 * buffers (cs over the row width); the dense tails are poison a
 * dense-walk bug would read. The output stays Continuous per spec. ---- */
static void gs_free_case(tnkr_ctx_t *ctx) {
    tdr_t d;
    uint32_t fails;

    tdr_stride_t st;
    int sidx;
    uint8_t pin, sw_in, sw_idx, sg_in, sg_idx, sg_out;
    uint8_t tab[128];
    uint8_t irow[64];
    uint16_t ixy[32];
    uint8_t got[16];
    uint8_t exp;
    uint32_t c, k, r;

    for (c = 0; c < 2; c++) {
        for (r = 0; r < 8; r++)
            tab[c * 64 + r] = (uint8_t)(c * 10u + r);
        for (r = 8; r < 64; r++)
            tab[c * 64 + r] = 0xA5;
        for (r = 0; r < 4; r++)
            irow[c * 32 + r] = (uint8_t)(c * 10u + r);
        for (r = 4; r < 32; r++)
            irow[c * 32 + r] = 0xA5;
        for (r = 0; r < 4; r++)
            ixy[c * 16 + r] = (uint16_t)((c * 4u + r * 3u) % 8u);
        for (r = 4; r < 16; r++)
            ixy[c * 16 + r] = 0xFFFF;
    }
    memcpy(ctx->global + GA, tab, sizeof(tab));
    memcpy(ctx->global + GB, ixy, sizeof(ixy));
    memset(ctx->global + GO, 0xAA, 8);

    sidx = tdr_alloc(ctx);
    if (sidx < 0) {
        printf("  FATAL: slot setup failed\n");
        g_fail++;
        return;
    }
    pin = (uint8_t)sidx;
    st = tdr_make_stride(1, 0, 64, 0);
    sidx = tdr_alloc(ctx);
    if (sidx < 0 || tdr_shadow_write_stride(ctx, (uint8_t)sidx, &st) != TN_SUCCESS) {
        printf("  FATAL: stride setup failed\n");
        g_fail++;
        return;
    }
    sw_in = (uint8_t)sidx;
    st = tdr_make_stride(1, 0, 16, 0);
    sidx = tdr_alloc(ctx);
    if (sidx < 0 || tdr_shadow_write_stride(ctx, (uint8_t)sidx, &st) != TN_SUCCESS) {
        printf("  FATAL: stride setup failed\n");
        g_fail++;
        return;
    }
    sw_idx = (uint8_t)sidx;

    d = tdr_make_global(TDR_LAYOUT_FREE, TN_DTYPE_U8, GA, 1, 2, 1, 8);
    sg_in = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_FREE, TN_DTYPE_U16, GB, 1, 2, 1, 4);
    sg_idx = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U8, GO, 1, 2, 1, 4);
    sg_out = mk_desc(ctx, &d);

    dmr_gather(sg_out, sg_in, sw_in, sg_idx, sw_idx, AXIS_MODE_W, PAD_VAL_ZERO);
    memcpy(got, ctx->global + GO, 8);
    fails = 0;
    for (c = 0; c < 2; c++)
        for (k = 0; k < 4; k++)
            if (got[c * 4 + k] != tab[c * 64 + ixy[c * 16 + k]])
                fails++;
    CHECK(fails == 0, "gather Global FREE: strided lut + index views");

    tdr_free(ctx, sg_in);
    tdr_free(ctx, sg_out);
    memcpy(ctx->global + GA, irow, sizeof(irow));
    memset(ctx->global + GO, 0xAA, 16);
    d = tdr_make_global(TDR_LAYOUT_FREE, TN_DTYPE_U8, GA, 1, 2, 1, 4);
    sg_in = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U8, GO, 1, 2, 1, 8);
    sg_out = mk_desc(ctx, &d);

    dmr_scatter(sg_out, sg_in, sw_in, sg_idx, sw_idx, REDUCE_MODE_NONE, AXIS_MODE_W);
    memcpy(got, ctx->global + GO, 16);
    fails = 0;
    for (c = 0; c < 2; c++)
        for (k = 0; k < 8; k++) {
            exp = 0xAA;
            for (r = 0; r < 4; r++)
                if (ixy[c * 16 + r] == k)
                    exp = irow[c * 32 + r];
            if (got[c * 8 + k] != exp)
                fails++;
        }
    CHECK(fails == 0, "scatter Global FREE: strided compacted + index views");

    tdr_free(ctx, sg_in);
    tdr_free(ctx, sg_idx);
    tdr_free(ctx, sg_out);
    tdr_free(ctx, sw_in);
    tdr_free(ctx, sw_idx);
    tdr_free(ctx, pin);
}
