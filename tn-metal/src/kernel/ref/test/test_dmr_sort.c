// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/test/test_dmr_sort.c
 *
 * dmr sort & statistics cases: sort (ascend full + descend topk, carried
 * coordinates, result VALUE/INDEX/BOTH with unused slots untouched, BF16
 * NaN-above-everything and +-0 stability, the Global FREE shared c_stride
 * — FREE coordinate pairs resolve with the values companion stride),
 * sort_mrg (stable merge with the opd0-first tie rule, topk, carried
 * coordinates), histogram_count (bucket tally, shape-driven E,
 * saturation, empty input), plus violation negatives per legality rule
 * and reserved-encoding silent returns. Expectations are test-side
 * insertion sort / two-pointer merge — independent of the ref
 * implementation.
 * ========================================================================== */
#include "smoke.h"

/* extra global areas beyond smoke.h's GA/GB/GO: res1, opd2, opd3 */
#define GI (GO + 512u)
#define GJ (GO + 1024u)
#define GK (GO + 1536u)

/* test-side stable insertion sort: keys per direction, ties keep input
 * order (moves only strictly greater/lesser elements) */
static void ts_sort(int64_t *key, uint64_t *pos, uint32_t n, int asc) {
    uint32_t i, j;

    for (i = 1; i < n; i++) {
        int64_t kv = key[i];
        uint64_t pv = pos[i];

        j = i;
        while (j > 0 && (asc ? kv < key[j - 1] : kv > key[j - 1])) {
            key[j] = key[j - 1];
            pos[j] = pos[j - 1];
            j--;
        }
        key[j] = kv;
        pos[j] = pv;
    }
}

/* test-side two-pointer merge over two direction-sorted rows; ties pick
 * the opd0 row first ("opd1 ranks after opd0") */
static void ts_merge(const int64_t *a, const uint64_t *ca, const int64_t *b, const uint64_t *cb,
                     uint32_t n, uint32_t k, int asc, int64_t *ko, uint64_t *co) {
    uint32_t i = 0, j = 0, t;

    for (t = 0; t < k && (i < n || j < n); t++) {
        int take0;

        if (i >= n)
            take0 = 0;
        else if (j >= n)
            take0 = 1;
        else
            take0 = asc ? (a[i] <= b[j]) : (a[i] >= b[j]);
        ko[t] = take0 ? a[i] : b[j];
        co[t] = take0 ? ca[i] : cb[j];
        if (take0)
            i++;
        else
            j++;
    }
}

void suite_dmr_sort(tnkr_ctx_t *ctx) {
    tdr_t d;
    uint32_t i, c, w, k, fails;
    uint8_t sg_in, sg_in1, sg_res0, sg_res1, sg_idx, sg_idx1, sg_bad, sg_bad2;
    uint8_t su_in, su_res, su_res2;
    int64_t key[8];
    uint64_t pos[8];

    /* ---- sort: ascend full + descend topk, S8, all-Global ---- */
    printf("== dmr sort ==\n");
    {
        int8_t in[16] = {5, -3, 5, 0, 127, -128, 5, 1,  /* ch0 */
                         2, 2,  1, 3, 3,   0,    9, 9}; /* ch1 */
        int8_t got[16];
        uint16_t gidx[16];

        memcpy(ctx->global + GA, in, sizeof(in));
        memset(ctx->global + GO, 0xAA, 16);
        memset(ctx->global + GI, 0xAA, 32);

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 2, 1, 8);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, 2, 1, 8);
        sg_res0 = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GI, 1, 2, 1, 8);
        sg_res1 = mk_desc(ctx, &d);

        dmr_sort(sg_res0, 0, sg_res1, sg_in, 0, 31, SORT_DIR_ASCEND, SORT_IDX_NONE,
                 SORT_RESULT_BOTH);
        memcpy(got, ctx->global + GO, 16);
        memcpy(gidx, ctx->global + GI, 32);
        fails = 0;
        for (c = 0; c < 2; c++) {
            for (w = 0; w < 8; w++) {
                key[w] = in[c * 8 + w];
                pos[w] = w;
            }
            ts_sort(key, pos, 8, 1);
            for (k = 0; k < 8; k++)
                if (got[c * 8 + k] != (int8_t)key[k] || gidx[c * 8 + k] != pos[k])
                    fails++;
        }
        CHECK(fails == 0, "sort ascend: stable values + positions (duplicates keep order)");

        /* descend topk K=4: first 4 of the descending order */
        memset(ctx->global + GO, 0xAA, 8);
        memset(ctx->global + GI, 0xAA, 16);
        tdr_free(ctx, sg_res0);
        tdr_free(ctx, sg_res1);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, 2, 1, 4);
        sg_res0 = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GI, 1, 2, 1, 4);
        sg_res1 = mk_desc(ctx, &d);
        dmr_sort(sg_res0, 0, sg_res1, sg_in, 0, 31, SORT_DIR_DESCEND, SORT_IDX_NONE,
                 SORT_RESULT_BOTH);
        memcpy(got, ctx->global + GO, 8);
        memcpy(gidx, ctx->global + GI, 16);
        fails = 0;
        for (c = 0; c < 2; c++) {
            for (w = 0; w < 8; w++) {
                key[w] = in[c * 8 + w];
                pos[w] = w;
            }
            ts_sort(key, pos, 8, 0);
            for (k = 0; k < 4; k++)
                if (got[c * 4 + k] != (int8_t)key[k] || gidx[c * 4 + k] != pos[k])
                    fails++;
        }
        CHECK(fails == 0, "sort descend topk K=4: first 4 of the descending order");

        /* ascend topk K=4: same outputs, ascending expectation */
        memset(ctx->global + GO, 0xAA, 8);
        memset(ctx->global + GI, 0xAA, 16);
        dmr_sort(sg_res0, 0, sg_res1, sg_in, 0, 31, SORT_DIR_ASCEND, SORT_IDX_NONE,
                 SORT_RESULT_BOTH);
        memcpy(got, ctx->global + GO, 8);
        memcpy(gidx, ctx->global + GI, 16);
        fails = 0;
        for (c = 0; c < 2; c++) {
            for (w = 0; w < 8; w++) {
                key[w] = in[c * 8 + w];
                pos[w] = w;
            }
            ts_sort(key, pos, 8, 1);
            for (k = 0; k < 4; k++)
                if (got[c * 4 + k] != (int8_t)key[k] || gidx[c * 4 + k] != pos[k])
                    fails++;
        }
        CHECK(fails == 0, "sort ascend topk K=4: first 4 of the ascending order");

        tdr_free(ctx, sg_in);
        tdr_free(ctx, sg_res0);
        tdr_free(ctx, sg_res1);
    }

    /* ---- sort: carried coordinates + result VALUE / INDEX unused slots ---- */
    {
        int8_t in[8] = {4, 1, 3, 1, 8, 0, 8, 2};
        uint16_t icoord[8] = {10, 11, 12, 13, 14, 15, 16, 17};
        int8_t got[8];
        uint16_t gidx[8];

        memcpy(ctx->global + GA, in, sizeof(in));
        memcpy(ctx->global + GB, icoord, sizeof(icoord));
        memset(ctx->global + GO, 0xAA, 8);
        memset(ctx->global + GI, 0xAA, 16);

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 1, 1, 8);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GB, 1, 1, 1, 8);
        sg_idx = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, 1, 1, 8);
        sg_res0 = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GI, 1, 1, 1, 8);
        sg_res1 = mk_desc(ctx, &d);

        /* carried coordinates travel with the values */
        dmr_sort(sg_res0, 0, sg_res1, sg_in, 0, sg_idx, SORT_DIR_ASCEND, SORT_IDX_CARRIED,
                 SORT_RESULT_BOTH);
        memcpy(got, ctx->global + GO, 8);
        memcpy(gidx, ctx->global + GI, 16);
        for (w = 0; w < 8; w++) {
            key[w] = in[w];
            pos[w] = icoord[w];
        }
        ts_sort(key, pos, 8, 1);
        fails = 0;
        for (k = 0; k < 8; k++)
            if (got[k] != (int8_t)key[k] || gidx[k] != pos[k])
                fails++;
        CHECK(fails == 0, "sort carried: opd1 coordinates travel with the values");

        /* result VALUE / INDEX: the unused output slot is never resolved
         * (slot 31 holds nothing) */
        memset(ctx->global + GO, 0xAA, 8);
        dmr_sort(sg_res0, 0, 31, sg_in, 0, sg_idx, SORT_DIR_ASCEND, SORT_IDX_CARRIED,
                 SORT_RESULT_VALUE);
        memcpy(got, ctx->global + GO, 8);
        fails = 0;
        for (k = 0; k < 8; k++)
            if (got[k] != (int8_t)key[k])
                fails++;
        CHECK(fails == 0, "sort result VALUE: unused res1 slot untouched (slot 31)");

        memset(ctx->global + GI, 0xAA, 16);
        dmr_sort(31, 0, sg_res1, sg_in, 0, sg_idx, SORT_DIR_ASCEND, SORT_IDX_CARRIED,
                 SORT_RESULT_INDEX);
        memcpy(gidx, ctx->global + GI, 16);
        fails = 0;
        for (k = 0; k < 8; k++)
            if (gidx[k] != pos[k])
                fails++;
        CHECK(fails == 0, "sort result INDEX: unused res0 slot untouched (slot 31)");

        /* carried coordinates + topk K=4: the first 4 of the sorted order
         * (key/pos above) keep their opd1 coordinates */
        memset(ctx->global + GO, 0xAA, 4);
        memset(ctx->global + GI, 0xAA, 8);
        tdr_free(ctx, sg_res0);
        tdr_free(ctx, sg_res1);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, 1, 1, 4);
        sg_res0 = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GI, 1, 1, 1, 4);
        sg_res1 = mk_desc(ctx, &d);
        dmr_sort(sg_res0, 0, sg_res1, sg_in, 0, sg_idx, SORT_DIR_ASCEND, SORT_IDX_CARRIED,
                 SORT_RESULT_BOTH);
        memcpy(got, ctx->global + GO, 4);
        memcpy(gidx, ctx->global + GI, 8);
        fails = 0;
        for (k = 0; k < 4; k++)
            if (got[k] != (int8_t)key[k] || gidx[k] != pos[k])
                fails++;
        CHECK(fails == 0, "sort carried topk K=4: first 4 keep their opd1 coordinates");

        tdr_free(ctx, sg_in);
        tdr_free(ctx, sg_idx);
        tdr_free(ctx, sg_res0);
        tdr_free(ctx, sg_res1);
    }

    /* ---- sort BF16: NaN above every number, +0/-0 stable ---- */
    {
        uint16_t in[6] = {0x3F80, 0xBF80, 0x7FC0, 0x0000, 0x8000, 0x4000};
        /* ascend: -1.0(pos1), +0(pos3), -0(pos4, equal to +0 -> input
         * order), 1.0(pos0), 2.0(pos5), NaN(pos2, above every number) */
        uint16_t eval[6] = {0xBF80, 0x0000, 0x8000, 0x3F80, 0x4000, 0x7FC0};
        uint8_t eidx[6] = {1, 3, 4, 0, 5, 2};
        uint16_t got[6];
        uint8_t gidx[6];

        memcpy(ctx->global + GA, in, sizeof(in));
        memset(ctx->global + GO, 0xAA, 12);
        memset(ctx->global + GI, 0xAA, 6);

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_BF16, GA, 1, 1, 1, 6);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_BF16, GO, 1, 1, 1, 6);
        sg_res0 = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U8, GI, 1, 1, 1, 6);
        sg_res1 = mk_desc(ctx, &d);

        dmr_sort(sg_res0, 0, sg_res1, sg_in, 0, 31, SORT_DIR_ASCEND, SORT_IDX_NONE,
                 SORT_RESULT_BOTH);
        memcpy(got, ctx->global + GO, 12);
        memcpy(gidx, ctx->global + GI, 6);
        fails = 0;
        for (k = 0; k < 6; k++)
            if (got[k] != eval[k] || gidx[k] != eidx[k])
                fails++;
        CHECK(fails == 0, "sort BF16: NaN above every number, +-0 keep input order");

        tdr_free(ctx, sg_in);
        tdr_free(ctx, sg_res0);
        tdr_free(ctx, sg_res1);
    }

    /* ---- sort on URF: channels spread across lanes, C > smc_num wraps to
     * the same lane's next c-block (lane law, tensor.cpp bit_addr) ---- */
    {
        const uint32_t C = ctx->info.smc_num + 2;
        uint8_t src[C * 8];
        int8_t got[C * 8];
        uint16_t gidx[C * 8];

        /* channel-dependent pattern: a lane/c-block placement bug crosses
         * channels and the case sees wrong values immediately */
        for (c = 0; c < C; c++)
            for (w = 0; w < 8; w++)
                src[c * 8 + w] = (uint8_t)(c * 29u + w * 13u + 7u);

        memcpy(ctx->global + GA, src, C * 8);
        memset(ctx->global + GO, 0, C * 8);
        memset(ctx->global + GI, 0, C * 8 * 2);

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, (uint16_t)C, 1, 8);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, (uint16_t)C, 1, 8, 0);
        su_in = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UB, 1, (uint16_t)C, 1, 8, 0);
        su_res = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_U16, UO, 1, (uint16_t)C, 1, 8, 0);
        su_res2 = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, (uint16_t)C, 1, 8);
        sg_res0 = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GI, 1, (uint16_t)C, 1, 8);
        sg_res1 = mk_desc(ctx, &d);

        top_ldst_load(su_in, sg_in, 0, LDST_MODE_NORMAL);
        dmr_sort(su_res, 0, su_res2, su_in, 0, 31, SORT_DIR_ASCEND, SORT_IDX_NONE,
                 SORT_RESULT_BOTH);
        top_ldst_store(sg_res0, 0, su_res, LDST_MODE_NORMAL);
        top_ldst_store(sg_res1, 0, su_res2, LDST_MODE_NORMAL);
        memcpy(got, ctx->global + GO, C * 8);
        memcpy(gidx, ctx->global + GI, C * 8 * 2);

        fails = 0;
        for (c = 0; c < C; c++) {
            for (w = 0; w < 8; w++) {
                key[w] = (int8_t)src[c * 8 + w];
                pos[w] = w;
            }
            ts_sort(key, pos, 8, 1);
            for (k = 0; k < 8; k++)
                if (got[c * 8 + k] != (int8_t)key[k] || gidx[c * 8 + k] != pos[k])
                    fails++;
        }
        CHECK(fails == 0, "sort URF: C > smc_num wraps to c-blocks, lanes independent");

        tdr_free(ctx, sg_in);
        tdr_free(ctx, su_in);
        tdr_free(ctx, su_res);
        tdr_free(ctx, su_res2);
        tdr_free(ctx, sg_res0);
        tdr_free(ctx, sg_res1);
    }

    /* ---- sort Global FREE shared c_stride: strided values view over a
     * (1,C,1,32) buffer (cs=32); the FREE coordinate sides resolve with
     * the values companion stride — one c_str register, each side scaled
     * by its own dtype size (S32 rows stride 128B, U8 rows stride 32B).
     * A dense-walk bug reads/writes the wrong rows immediately. ---- */
    {
        const uint32_t C = 2, W = 8, ROW = 32;
        int32_t vbuf[64]; /* C * ROW */
        uint8_t ibuf[64]; /* C * ROW */
        int32_t obuf[64]; /* C * ROW */
        uint8_t kbuf[64]; /* C * ROW */
        tdr_stride_t st;
        int sidx;
        uint8_t sw_in, sw_out, dummy;
        uint32_t c, w, k, r;

        for (c = 0; c < C; c++) {
            for (w = 0; w < W; w++) {
                vbuf[c * ROW + w] = (int32_t)(7u * w + c); /* distinct in w */
                ibuf[c * ROW + w] = (uint8_t)(w + 100u);
            }
            for (r = W; r < ROW; r++) {
                vbuf[c * ROW + r] = 0x5A5A5A5A; /* poison past the view */
                ibuf[c * ROW + r] = 0xA5;
            }
        }
        memset(obuf, 0xAA, sizeof(obuf));
        memset(kbuf, 0xAA, sizeof(kbuf));
        memcpy(ctx->global + GA, vbuf, sizeof(vbuf));
        memcpy(ctx->global + GB, ibuf, sizeof(ibuf));
        memset(ctx->global + GO, 0xAA, sizeof(obuf));
        memset(ctx->global + GI, 0xAA, sizeof(kbuf));

        st = tdr_make_stride(1, 0, ROW, 0); /* w=1, c=32 elements */
        /* pin the lowest slot first so the shared stride slots below sit
         * above slot 0 — a dropped companion (si=0) must resolve to an
         * unresolvable slot, not accidentally to this stride */
        sidx = tdr_alloc(ctx);
        if (sidx < 0) {
            printf("  FATAL: slot setup failed\n");
            g_fail++;
            return;
        }
        dummy = (uint8_t)sidx;
        sidx = tdr_alloc(ctx);
        if (sidx < 0 || tdr_shadow_write_stride(ctx, (uint8_t)sidx, &st) != TN_SUCCESS) {
            printf("  FATAL: stride setup failed\n");
            g_fail++;
            return;
        }
        sw_in = (uint8_t)sidx;
        sidx = tdr_alloc(ctx);
        if (sidx < 0 || tdr_shadow_write_stride(ctx, (uint8_t)sidx, &st) != TN_SUCCESS) {
            printf("  FATAL: stride setup failed\n");
            g_fail++;
            return;
        }
        sw_out = (uint8_t)sidx;

        d = tdr_make_global(TDR_LAYOUT_FREE, TN_DTYPE_S32, GA, 1, C, 1, W);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_FREE, TN_DTYPE_U8, GB, 1, C, 1, W);
        sg_idx = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_FREE, TN_DTYPE_S32, GO, 1, C, 1, W);
        sg_res0 = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_FREE, TN_DTYPE_U8, GI, 1, C, 1, W);
        sg_res1 = mk_desc(ctx, &d);

        dmr_sort(sg_res0, sw_out, sg_res1, sg_in, sw_in, sg_idx, SORT_DIR_ASCEND, SORT_IDX_CARRIED,
                 SORT_RESULT_BOTH);

        memcpy(obuf, ctx->global + GO, sizeof(obuf));
        memcpy(kbuf, ctx->global + GI, sizeof(kbuf));
        fails = 0;
        for (c = 0; c < C; c++) {
            for (w = 0; w < W; w++) {
                key[w] = vbuf[c * ROW + w];
                pos[w] = ibuf[c * ROW + w];
            }
            ts_sort(key, pos, W, 1);
            for (k = 0; k < W; k++)
                if (obuf[c * ROW + k] != (int32_t)key[k] || kbuf[c * ROW + k] != pos[k])
                    fails++;
            for (r = W; r < ROW; r++)
                if (obuf[c * ROW + r] != (int32_t)0xAAAAAAAA || kbuf[c * ROW + r] != 0xAA)
                    fails++; /* past the view: untouched poison */
        }
        CHECK(fails == 0, "sort Global FREE: shared c_stride walks the strided rows");

        /* pair layout mismatch: opd0 FREE, opd1 Continuous -> violation */
        tdr_free(ctx, sg_idx);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U8, GB, 1, C, 1, W);
        sg_idx = mk_desc(ctx, &d);
        TNVI_BEGIN("layout must match") {
            dmr_sort(sg_res0, sw_out, sg_res1, sg_in, sw_in, sg_idx, SORT_DIR_ASCEND,
                     SORT_IDX_CARRIED, SORT_RESULT_BOTH);
        }
        TNVI_END();

        tdr_free(ctx, sg_in);
        tdr_free(ctx, sg_idx);
        tdr_free(ctx, sg_res0);
        tdr_free(ctx, sg_res1);
        tdr_free(ctx, sw_in);
        tdr_free(ctx, sw_out);
        tdr_free(ctx, dummy);
    }

    /* ---- sort negatives ---- */
    {
        int8_t in[4] = {3, 1, 2, 0};
        uint8_t big[300];

        memcpy(ctx->global + GA, in, sizeof(in));
        for (i = 0; i < 300; i++)
            big[i] = (uint8_t)(i % 251);

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 1, 1, 4);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, 1, 1, 4);
        sg_res0 = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GI, 1, 1, 1, 4);
        sg_res1 = mk_desc(ctx, &d);

        /* res0 on URF while opd0 is Global -> location mismatch */
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, 1, 1, 4, 0);
        su_res = mk_desc(ctx, &d);
        TNVI_BEGIN("res0 must share") {
            dmr_sort(su_res, 0, sg_res1, sg_in, 0, 31, SORT_DIR_ASCEND, SORT_IDX_NONE,
                     SORT_RESULT_BOTH);
        }
        TNVI_END();
        tdr_free(ctx, su_res);

        /* res1 dtype S16 -> violation */
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GI, 1, 1, 1, 4);
        sg_bad = mk_desc(ctx, &d);
        TNVI_BEGIN("res1 dtype") {
            dmr_sort(sg_res0, 0, sg_bad, sg_in, 0, 31, SORT_DIR_ASCEND, SORT_IDX_NONE,
                     SORT_RESULT_BOTH);
        }
        TNVI_END();
        tdr_free(ctx, sg_bad);

        /* opd0 dtype FP4 -> not in the sort set */
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_FP4, GA, 1, 1, 1, 4);
        sg_bad = mk_desc(ctx, &d);
        TNVI_BEGIN("sort set") {
            dmr_sort(sg_res0, 0, sg_res1, sg_bad, 0, 31, SORT_DIR_ASCEND, SORT_IDX_NONE,
                     SORT_RESULT_BOTH);
        }
        TNVI_END();
        tdr_free(ctx, sg_bad);

        /* K > W (res0/res1 K=5 over W=4) -> violation */
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, 1, 1, 5);
        sg_bad = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GI, 1, 1, 1, 5);
        sg_bad2 = mk_desc(ctx, &d);
        TNVI_BEGIN("topk K exceeds") {
            dmr_sort(sg_bad, 0, sg_bad2, sg_in, 0, 31, SORT_DIR_ASCEND, SORT_IDX_NONE,
                     SORT_RESULT_BOTH);
        }
        TNVI_END();
        tdr_free(ctx, sg_bad);
        tdr_free(ctx, sg_bad2);

        /* implicit positions 0..299 do not fit a U8 res1 -> violation */
        memcpy(ctx->global + GA, big, sizeof(big));
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 1, 1, 300);
        sg_bad = mk_desc(ctx, &d);
        tdr_free(ctx, sg_res0);
        tdr_free(ctx, sg_res1);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, 1, 1, 300);
        sg_res0 = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U8, GI, 1, 1, 1, 300);
        sg_res1 = mk_desc(ctx, &d);
        TNVI_BEGIN("does not fit the res1 dtype") {
            dmr_sort(sg_res0, 0, sg_res1, sg_bad, 0, 31, SORT_DIR_ASCEND, SORT_IDX_NONE,
                     SORT_RESULT_BOTH);
        }
        TNVI_END();

        /* reserved ascend=2 encoding: silent, outputs keep their poison */
        memset(ctx->global + GO, 0xAA, 300);
        memset(ctx->global + GI, 0xAA, 300);
        dmr_sort(sg_res0, 0, sg_res1, sg_bad, 0, 31, (sort_dir_t)2, SORT_IDX_NONE,
                 SORT_RESULT_BOTH);
        fails = 0;
        for (i = 0; i < 300; i++)
            if (ctx->global[GO + i] != 0xAA || ctx->global[GI + i] != 0xAA)
                fails++;
        CHECK(fails == 0, "sort reserved ascend=2: silent, outputs untouched");

        tdr_free(ctx, sg_in);
        tdr_free(ctx, sg_bad);
        tdr_free(ctx, sg_res0);
        tdr_free(ctx, sg_res1);
    }

    /* ---- sort_mrg: stable merge, opd0-first ties, topk, carried ---- */
    printf("== dmr sort_mrg ==\n");
    {
        int16_t in0[12] = {1,  3,  5,  5, 7, 9,   /* ch0 asc-sorted */
                           -5, -3, -1, 0, 2, 4};  /* ch1 */
        int16_t in1[12] = {2,  3,  5,  6,  8, 9,  /* ch0 */
                           -4, -4, -2, -1, 1, 3}; /* ch1 */
        uint16_t c0[6] = {100, 101, 102, 103, 104, 105};
        uint16_t c1[6] = {200, 201, 202, 203, 204, 205};
        int16_t got[16];
        uint16_t gidx[16];
        int64_t a6[6], b6[6], ekey[12];
        uint64_t epos[12];
        uint64_t ca[6], cb[6];

        memcpy(ctx->global + GA, in0, sizeof(in0));
        memcpy(ctx->global + GB, in1, sizeof(in1));
        memset(ctx->global + GO, 0xAA, 32);
        memset(ctx->global + GI, 0xAA, 32);

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GA, 1, 2, 1, 6);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GB, 1, 2, 1, 6);
        sg_in1 = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GO, 1, 2, 1, 8);
        sg_res0 = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GI, 1, 2, 1, 8);
        sg_res1 = mk_desc(ctx, &d);

        /* ascend merge, topk K=8 of 12, implicit coords (opd0 0..5, opd1 6..11) */
        dmr_sort_mrg(sg_res0, sg_res1, sg_in, sg_in1, 31, 31, SORT_DIR_ASCEND, SORT_IDX_NONE,
                     SORT_RESULT_BOTH);
        memcpy(got, ctx->global + GO, 32);
        memcpy(gidx, ctx->global + GI, 32);
        fails = 0;
        for (c = 0; c < 2; c++) {
            for (w = 0; w < 6; w++) {
                a6[w] = in0[c * 6 + w];
                b6[w] = in1[c * 6 + w];
                ca[w] = w;     /* implicit opd0 positions */
                cb[w] = 6 + w; /* implicit opd1 positions */
            }
            ts_merge(a6, ca, b6, cb, 6, 8, 1, ekey, epos);
            for (k = 0; k < 8; k++)
                if (got[c * 8 + k] != (int16_t)ekey[k] || gidx[c * 8 + k] != epos[k])
                    fails++;
        }
        CHECK(fails == 0, "sort_mrg ascend topk: opd0-first ties, implicit coords");

        tdr_free(ctx, sg_res0);
        tdr_free(ctx, sg_res1);

        /* descend merge, full K=12, carried coordinates (opd2/opd3) */
        {
            int16_t d0[6] = {9, 7, 5, 3, 1, -1};
            int16_t d1[6] = {8, 6, 5, 2, 0, -2};

            memcpy(ctx->global + GA, d0, sizeof(d0));
            memcpy(ctx->global + GB, d1, sizeof(d1));
            memcpy(ctx->global + GJ, c0, sizeof(c0));
            memcpy(ctx->global + GK, c1, sizeof(c1));
            memset(ctx->global + GO, 0xAA, 24);
            memset(ctx->global + GI, 0xAA, 24);

            tdr_free(ctx, sg_in);
            tdr_free(ctx, sg_in1);
            d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GA, 1, 1, 1, 6);
            sg_in = mk_desc(ctx, &d);
            d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GB, 1, 1, 1, 6);
            sg_in1 = mk_desc(ctx, &d);
            d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GJ, 1, 1, 1, 6);
            sg_idx = mk_desc(ctx, &d);
            d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GK, 1, 1, 1, 6);
            sg_idx1 = mk_desc(ctx, &d);
            d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GO, 1, 1, 1, 12);
            sg_res0 = mk_desc(ctx, &d);
            d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GI, 1, 1, 1, 12);
            sg_res1 = mk_desc(ctx, &d);

            dmr_sort_mrg(sg_res0, sg_res1, sg_in, sg_in1, sg_idx, sg_idx1, SORT_DIR_DESCEND,
                         SORT_IDX_CARRIED, SORT_RESULT_BOTH);
            memcpy(got, ctx->global + GO, 24);
            memcpy(gidx, ctx->global + GI, 24);
            for (w = 0; w < 6; w++) {
                a6[w] = d0[w];
                b6[w] = d1[w];
                ca[w] = c0[w];
                cb[w] = c1[w];
            }
            ts_merge(a6, ca, b6, cb, 6, 12, 0, ekey, epos);
            fails = 0;
            for (k = 0; k < 12; k++)
                if (got[k] != (int16_t)ekey[k] || gidx[k] != epos[k])
                    fails++;
            CHECK(fails == 0, "sort_mrg descend full: carried coordinates ride along");

            /* carried coordinates + topk K=8: the first 8 of the merged
             * order (ekey/epos above) keep their opd2/opd3 coordinates */
            memset(ctx->global + GO, 0xAA, 16);
            memset(ctx->global + GI, 0xAA, 16);
            tdr_free(ctx, sg_res0);
            tdr_free(ctx, sg_res1);
            d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GO, 1, 1, 1, 8);
            sg_res0 = mk_desc(ctx, &d);
            d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GI, 1, 1, 1, 8);
            sg_res1 = mk_desc(ctx, &d);
            dmr_sort_mrg(sg_res0, sg_res1, sg_in, sg_in1, sg_idx, sg_idx1, SORT_DIR_DESCEND,
                         SORT_IDX_CARRIED, SORT_RESULT_BOTH);
            memcpy(got, ctx->global + GO, 16);
            memcpy(gidx, ctx->global + GI, 16);
            fails = 0;
            for (k = 0; k < 8; k++)
                if (got[k] != (int16_t)ekey[k] || gidx[k] != epos[k])
                    fails++;
            CHECK(fails == 0, "sort_mrg descend topk K=8: carried coords truncated");

            tdr_free(ctx, sg_idx);
            tdr_free(ctx, sg_idx1);
        }

        /* ---- sort_mrg negatives ---- */
        {
            int16_t s0[3] = {1, 2, 0}; /* NOT sorted */
            int16_t s1[3] = {0, 1, 2};
            int16_t t0[3] = {1, 2, 3};
            int16_t t1[3] = {4, 5, 6};
            uint16_t k0[3] = {10, 11, 12};
            uint16_t k1[3] = {13, 11, 14}; /* 11 conflicts with opd2 */

            memcpy(ctx->global + GA, s0, sizeof(s0));
            memcpy(ctx->global + GB, s1, sizeof(s1));
            tdr_free(ctx, sg_in);
            tdr_free(ctx, sg_in1);
            tdr_free(ctx, sg_res0);
            tdr_free(ctx, sg_res1);
            d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GA, 1, 1, 1, 3);
            sg_in = mk_desc(ctx, &d);
            d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GB, 1, 1, 1, 3);
            sg_in1 = mk_desc(ctx, &d);
            d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GO, 1, 1, 1, 4);
            sg_res0 = mk_desc(ctx, &d);
            d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GI, 1, 1, 1, 4);
            sg_res1 = mk_desc(ctx, &d);

            TNVI_BEGIN("not sorted") {
                dmr_sort_mrg(sg_res0, sg_res1, sg_in, sg_in1, 31, 31, SORT_DIR_ASCEND,
                             SORT_IDX_NONE, SORT_RESULT_BOTH);
            }
            TNVI_END();

            /* length mismatch -> violation */
            d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GB, 1, 1, 1, 4);
            sg_bad = mk_desc(ctx, &d);
            memcpy(ctx->global + GB, t1, sizeof(t1));
            TNVI_BEGIN("must share shape and dtype") {
                dmr_sort_mrg(sg_res0, sg_res1, sg_in, sg_bad, 31, 31, SORT_DIR_ASCEND,
                             SORT_IDX_NONE, SORT_RESULT_BOTH);
            }
            TNVI_END();
            tdr_free(ctx, sg_bad);

            /* carried coordinates conflict across opd2/opd3 -> violation */
            memcpy(ctx->global + GA, t0, sizeof(t0));
            memcpy(ctx->global + GB, t1, sizeof(t1));
            memcpy(ctx->global + GJ, k0, sizeof(k0));
            memcpy(ctx->global + GK, k1, sizeof(k1));
            d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GJ, 1, 1, 1, 3);
            sg_idx = mk_desc(ctx, &d);
            d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GK, 1, 1, 1, 3);
            sg_idx1 = mk_desc(ctx, &d);
            TNVI_BEGIN("coordinates conflict") {
                dmr_sort_mrg(sg_res0, sg_res1, sg_in, sg_in1, sg_idx, sg_idx1, SORT_DIR_ASCEND,
                             SORT_IDX_CARRIED, SORT_RESULT_BOTH);
            }
            TNVI_END();
            tdr_free(ctx, sg_idx);
            tdr_free(ctx, sg_idx1);

            /* K > 2W (res0/res1 K=7 over 2W=6) -> violation */
            d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S16, GO, 1, 1, 1, 7);
            sg_bad = mk_desc(ctx, &d);
            d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GI, 1, 1, 1, 7);
            sg_bad2 = mk_desc(ctx, &d);
            TNVI_BEGIN("exceeds the merged length") {
                dmr_sort_mrg(sg_bad, sg_bad2, sg_in, sg_in1, 31, 31, SORT_DIR_ASCEND, SORT_IDX_NONE,
                             SORT_RESULT_BOTH);
            }
            TNVI_END();
            tdr_free(ctx, sg_bad);
            tdr_free(ctx, sg_bad2);
        }

        tdr_free(ctx, sg_in);
        tdr_free(ctx, sg_in1);
        tdr_free(ctx, sg_res0);
        tdr_free(ctx, sg_res1);
    }

    /* ---- histogram_count: tally, E tie, saturation, empty input ---- */
    printf("== dmr histogram_count ==\n");
    {
        uint8_t in[12] = {0, 1, 2,  /* ch0 */
                          3, 0, 1,  /* ch1 */
                          3, 3, 3,  /* ch2 */
                          2, 2, 0}; /* ch3: E = 4 buckets from the shape */
        uint8_t exp[4] = {3, 2, 3, 4};
        uint8_t got[4];

        memcpy(ctx->global + GA, in, sizeof(in));
        memset(ctx->global + GO, 0xAA, 4);

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U8, GA, 1, 4, 1, 3);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U8, GO, 1, 4, 1, 1);
        sg_res0 = mk_desc(ctx, &d);

        dmr_histogram_count(sg_res0, sg_in, 0);
        memcpy(got, ctx->global + GO, 4);
        CHECK(memcmp(got, exp, 4) == 0, "histogram: bucket tally");

        /* mixed placement: Global in -> URF out, store back */
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_U8, UA, 1, 4, 1, 1, 0);
        su_res = mk_desc(ctx, &d);
        dmr_histogram_count(su_res, sg_in, 0);
        top_ldst_store(sg_res0, 0, su_res, LDST_MODE_NORMAL);
        memcpy(got, ctx->global + GO, 4);
        CHECK(memcmp(got, exp, 4) == 0, "histogram: Global in -> URF out");
        tdr_free(ctx, su_res);

        /* URF in (loaded, lanes live) -> Global out */
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_U8, UA, 1, 4, 1, 3, 0);
        su_in = mk_desc(ctx, &d);
        top_ldst_load(su_in, sg_in, 0, LDST_MODE_NORMAL);
        dmr_histogram_count(sg_res0, su_in, 0);
        memcpy(got, ctx->global + GO, 4);
        CHECK(memcmp(got, exp, 4) == 0, "histogram: URF in -> Global out");
        tdr_free(ctx, su_in);

        /* E is shape-driven: E = 6 over the same values leaves buckets
         * 4/5 at zero (no tie to the data max) */
        {
            uint8_t exp6[6] = {3, 2, 3, 4, 0, 0};
            uint8_t got6[6];

            d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U8, GO, 1, 6, 1, 1);
            sg_bad = mk_desc(ctx, &d);
            memset(ctx->global + GO, 0xAA, 6);
            dmr_histogram_count(sg_bad, sg_in, 0);
            memcpy(got6, ctx->global + GO, 6);
            CHECK(memcmp(got6, exp6, 6) == 0,
                  "histogram: E from the shape, buckets above max count zero");
            tdr_free(ctx, sg_bad);
        }

        tdr_free(ctx, sg_in);
        tdr_free(ctx, sg_res0);
    }
    {
        /* Global FREE input: topk rows strided over a wider buffer — view
         * (1,8,1,4) with cs=8 over a (1,8,1,8) buffer; only the first 4
         * of each row count (the dense tail is zeros a dense-walk bug
         * would tally into bucket 0) */
        uint8_t buf[8 * 8];
        uint8_t exp[4] = {8, 8, 8, 8}; /* rotations of 0..3, 8 channels */
        uint8_t got[4];
        tdr_stride_t st;
        int sidx;
        uint8_t sw_in;
        uint32_t c, w;

        for (c = 0; c < 8; c++) {
            for (w = 0; w < 4; w++)
                buf[c * 8 + w] = (uint8_t)((c + w) % 4u); /* rotation of 0..3 */
            for (w = 4; w < 8; w++)
                buf[c * 8 + w] = 0; /* dense tail: would add 32 to bucket 0 */
        }
        memcpy(ctx->global + GA, buf, sizeof(buf));
        memset(ctx->global + GO, 0xAA, 4);

        /* pin the lowest slot so the stride slot below is not slot 0
         * (a dropped companion must not resolve to a wrong-but-valid slot) */
        sidx = tdr_alloc(ctx);
        if (sidx < 0) {
            printf("  FATAL: slot setup failed\n");
            g_fail++;
            return;
        }
        sg_bad = (uint8_t)sidx;
        st = tdr_make_stride(1, 0, 8, 0); /* w=1, c=8 elements */
        sidx = tdr_alloc(ctx);
        if (sidx < 0 || tdr_shadow_write_stride(ctx, (uint8_t)sidx, &st) != TN_SUCCESS) {
            printf("  FATAL: stride setup failed\n");
            g_fail++;
            return;
        }
        sw_in = (uint8_t)sidx;

        d = tdr_make_global(TDR_LAYOUT_FREE, TN_DTYPE_U8, GA, 1, 8, 1, 4);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U8, GO, 1, 4, 1, 1);
        sg_res0 = mk_desc(ctx, &d);

        dmr_histogram_count(sg_res0, sg_in, sw_in);
        memcpy(got, ctx->global + GO, 4);
        CHECK(memcmp(got, exp, 4) == 0, "histogram: Global FREE input walks the strided topk rows");

        tdr_free(ctx, sg_in);
        tdr_free(ctx, sg_res0);
        tdr_free(ctx, sw_in);
        tdr_free(ctx, sg_bad);
    }
    {
        /* saturation: 298 zeros + 2 ones -> bucket0 saturates U8 at 255 */
        uint8_t big[300];
        uint8_t exp[2] = {255, 2};
        uint8_t got[2];

        for (i = 0; i < 300; i++)
            big[i] = (i % 150 == 0) ? 1 : 0; /* max 1 -> E = 2 */
        memcpy(ctx->global + GA, big, sizeof(big));
        memset(ctx->global + GO, 0xAA, 2);

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U8, GA, 1, 100, 1, 3);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U8, GO, 1, 2, 1, 1);
        sg_res0 = mk_desc(ctx, &d);

        dmr_histogram_count(sg_res0, sg_in, 0);
        memcpy(got, ctx->global + GO, 2);
        CHECK(memcmp(got, exp, 2) == 0, "histogram: count saturates at the res0 dtype");

        tdr_free(ctx, sg_in);
        tdr_free(ctx, sg_res0);
    }
    {
        /* negative: value over E -> violation (U16 input dtype) */
        uint16_t in[2] = {1, 4};

        memcpy(ctx->global + GA, in, sizeof(in));
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GA, 1, 1, 1, 2);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U8, GO, 1, 4, 1, 1);
        sg_res0 = mk_desc(ctx, &d);
        TNVI_BEGIN("over the bucket count") { dmr_histogram_count(sg_res0, sg_in, 0); }
        TNVI_END();

        /* empty input: every bucket zero-fills */
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U16, GA, 1, 0, 1, 3);
        tdr_free(ctx, sg_in);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_U8, GO, 1, 3, 1, 1);
        tdr_free(ctx, sg_res0);
        sg_res0 = mk_desc(ctx, &d);
        memset(ctx->global + GO, 0xAA, 3);
        dmr_histogram_count(sg_res0, sg_in, 0);
        uint8_t zres[3] = {0, 0, 0};
        uint8_t zgot[3];
        memcpy(zgot, ctx->global + GO, 3);
        CHECK(memcmp(zgot, zres, 3) == 0, "histogram: empty input zero-fills every bucket");

        tdr_free(ctx, sg_in);
        tdr_free(ctx, sg_res0);
    }
}
