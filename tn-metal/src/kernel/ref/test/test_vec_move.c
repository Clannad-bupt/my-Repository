// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/test/test_vec_move.c
 *
 * top/vec data-movement family cases: a FREE-stride transposing move
 * (4x8 row-major -> column-major byte stream)
 * ========================================================================== */
#include "smoke.h"

void suite_vec_move(tnkr_ctx_t *ctx) {
    uint8_t src[NELEM], expct[NELEM];
    tdr_t d;
    tdr_stride_t s;
    uint8_t sga, sua, suo, sstr;
    int idx;
    uint32_t i;

    printf("== vec_move ==\n");

    for (i = 0; i < NELEM; i++)
        src[i] = (uint8_t)i;
    memcpy(ctx->global + GA, src, sizeof(src));

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 1, 4, 8);
    sga = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, 1, 4, 8, 0);
    sua = mk_desc(ctx, &d);
    /* output FREE: h_str=1, w_str=4 — same logical coordinates, transposed
     * storage */
    d = tdr_make_urf(TDR_LAYOUT_FREE, TN_DTYPE_S8, 1024, 1, 1, 4, 8, 0);
    suo = mk_desc(ctx, &d);
    s = tdr_make_stride(4, 1, 0, 0);
    idx = tdr_alloc(ctx);
    if (idx < 0 || tdr_shadow_write_stride(ctx, (uint8_t)idx, &s) != TN_SUCCESS) {
        printf("  FATAL: stride setup failed\n");
        g_fail++;
        sstr = 0;
    } else {
        sstr = (uint8_t)idx;
    }

    top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
    memset(ctx->urf + 1024, 0xAA, NELEM);
    top_vec_move(suo, sstr, sua, 0);
    /* byte j = w*4 + h -> value in(h,w) = src[h*8+w] */
    for (i = 0; i < NELEM; i++)
        expct[i] = src[(i % 4) * 8 + i / 4];
    CHECK(memcmp(ctx->urf + 1024, expct, NELEM) == 0, "move FREE transpose (w_str=4,h_str=1)");

    tdr_free(ctx, sga);
    tdr_free(ctx, sua);
    tdr_free(ctx, suo);
    tdr_free(ctx, sstr);

    /* ---- FREE cs=0: C-broadcast move, lane-local (pins the lane law) ----
     * B stores (1,smc,1,8) HWALIGN: lane c holds its own 8-byte vector.
     * The FREE source view (1,2*smc,1,8) with c_str=0 wraps c-blocks to
     * this lane's c-block 0, so logical channel c reads lane (c%smc)'s
     * local vector — same lane-local semantics as ZEROSTRIDE CB reads.
     * The pre-lane-law flat formula would make every channel read lane 0's
     * vector, so this case distinguishes the two. */
    {
        const uint32_t smc = ctx->info.smc_num;
        uint8_t in_b[256], out[512], expct[512];
        tdr_t d;
        tdr_stride_t s;
        uint8_t sgb, sb_ld, sfree, suo, sgo, sstr;
        int idx;
        uint32_t c, w, i;

        for (c = 0; c < smc; c++)
            for (w = 0; w < 8; w++)
                in_b[c * 8 + w] = (uint8_t)(c * 8 + w);
        memcpy(ctx->global + GB, in_b, smc * 8);
        memset(ctx->global + GO, 0, sizeof(out));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GB, 1, (uint16_t)smc, 1, 8);
        sgb = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, (uint16_t)(2 * smc), 1, 8);
        sgo = mk_desc(ctx, &d);
        /* HWALIGN staging: ldst's URF side takes HWALIGN only; the FREE
         * view below aliases the same addresses */
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UB, 1, (uint16_t)smc, 1, 8, 0);
        sb_ld = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_FREE, TN_DTYPE_S8, UB, 1, (uint16_t)(2 * smc), 1, 8, 0);
        sfree = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UO, 1, (uint16_t)(2 * smc), 1, 8, 0);
        suo = mk_desc(ctx, &d);
        s = tdr_make_stride(1, 0, 0, 0); /* w_str=1, h/c/n_str=0 */
        idx = tdr_alloc(ctx);
        if (idx < 0 || tdr_shadow_write_stride(ctx, (uint8_t)idx, &s) != TN_SUCCESS) {
            printf("  FATAL: stride setup failed\n");
            g_fail++;
            sstr = 0;
        } else {
            sstr = (uint8_t)idx;
        }

        top_ldst_load(sb_ld, sgb, 0, LDST_MODE_NORMAL);
        top_vec_move(suo, 0, sfree, sstr);
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, 2 * smc * 8);

        /* out(0,c,0,w) = b_lane(c%smc)[w]; linear i = c*8+w */
        for (i = 0; i < 2 * smc * 8; i++)
            expct[i] = in_b[(i / 8 % smc) * 8 + i % 8];
        CHECK(memcmp(out, expct, 2 * smc * 8) == 0, "move FREE cs=0 C broadcast (lane-local)");

        tdr_free(ctx, sgb);
        tdr_free(ctx, sb_ld);
        tdr_free(ctx, sfree);
        tdr_free(ctx, suo);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sstr);
    }

    /* ---- FREE cs=0 C expansion: dst.dc > src.dc is sound when the
     * source's c-block 0 covers every lane the dst touches
     * (min(src.dc,smc) >= min(dst.dc,smc)). Source (1,smc,1,8) cs=0 ->
     * dst (1,2*smc,1,8): every dst channel reads its lane's c-block 0. ---- */
    {
        const uint32_t smc = ctx->info.smc_num;
        uint8_t in_b[256], out[512], expct[512];
        tdr_t d;
        tdr_stride_t s;
        uint8_t sgb, sb_ld, sfree, suo, sgo, sstr;
        int idx;
        uint32_t c, w, i;

        for (c = 0; c < smc; c++)
            for (w = 0; w < 8; w++)
                in_b[c * 8 + w] = (uint8_t)(c * 13u + w);
        memcpy(ctx->global + GB, in_b, smc * 8);
        memset(ctx->global + GO, 0, sizeof(out));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GB, 1, (uint16_t)smc, 1, 8);
        sgb = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, (uint16_t)(2 * smc), 1, 8);
        sgo = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UB, 1, (uint16_t)smc, 1, 8, 0);
        sb_ld = mk_desc(ctx, &d);
        /* FREE source with dc=smc (one c-block per lane), cs=0 */
        d = tdr_make_urf(TDR_LAYOUT_FREE, TN_DTYPE_S8, UB, 1, (uint16_t)smc, 1, 8, 0);
        sfree = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UO, 1, (uint16_t)(2 * smc), 1, 8, 0);
        suo = mk_desc(ctx, &d);
        s = tdr_make_stride(1, 0, 0, 0); /* w_str=1, h/c/n_str=0 */
        idx = tdr_alloc(ctx);
        if (idx < 0 || tdr_shadow_write_stride(ctx, (uint8_t)idx, &s) != TN_SUCCESS) {
            printf("  FATAL: stride setup failed\n");
            g_fail++;
            sstr = 0;
        } else {
            sstr = (uint8_t)idx;
        }

        top_ldst_load(sb_ld, sgb, 0, LDST_MODE_NORMAL);
        top_vec_move(suo, 0, sfree, sstr);
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, 2 * smc * 8);

        /* out(0,c,0,w) = b_lane(c%smc)[w]; linear i = c*8+w */
        for (i = 0; i < 2 * smc * 8; i++)
            expct[i] = in_b[(i / 8 % smc) * 8 + i % 8];
        CHECK(memcmp(out, expct, 2 * smc * 8) == 0,
              "move FREE cs=0 expands (1,smc,1,8)->(1,2*smc,1,8)");

        tdr_free(ctx, sgb);
        tdr_free(ctx, sb_ld);
        tdr_free(ctx, sfree);
        tdr_free(ctx, suo);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sstr);
    }

    /* ---- FREE cs=0 with a narrow source: src (1,smc-1,1,8) cs=0 cannot
     * feed dst (1,smc,1,8) — the source's c-block 0 leaves lane smc-1
     * without src data (min(src.dc,smc) < min(dst.dc,smc)), so the move
     * must be rejected before any write. ---- */
    {
        const uint32_t smc = ctx->info.smc_num;
        const uint32_t lb = ctx->info.urf_bytes / smc;
        uint8_t out[512], poison[512];
        tdr_t d;
        tdr_stride_t s;
        uint8_t sfree, suo, sgo, sstr;
        int idx;
        uint32_t l;

        d = tdr_make_urf(TDR_LAYOUT_FREE, TN_DTYPE_S8, UB, 1, (uint16_t)(smc - 1), 1, 8, 0);
        sfree = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UO, 1, (uint16_t)smc, 1, 8, 0);
        suo = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, (uint16_t)smc, 1, 8);
        sgo = mk_desc(ctx, &d);
        s = tdr_make_stride(1, 0, 0, 0); /* w_str=1, h/c/n_str=0 */
        idx = tdr_alloc(ctx);
        if (idx < 0 || tdr_shadow_write_stride(ctx, (uint8_t)idx, &s) != TN_SUCCESS) {
            printf("  FATAL: stride setup failed\n");
            g_fail++;
            sstr = 0;
        } else {
            sstr = (uint8_t)idx;
        }

        /* poison every lane's output plane: a successful move would write
         * smc*8 bytes across all lanes */
        memset(poison, 0xAA, smc * 8);
        for (l = 0; l < smc; l++)
            memset(ctx->urf + (size_t)l * lb + UO, 0xAA, 8);
        TNVI_BEGIN("shapes must pair legally") { top_vec_move(suo, 0, sfree, sstr); }
        TNVI_END();
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, smc * 8);
        CHECK(memcmp(out, poison, smc * 8) == 0,
              "move FREE cs=0 narrow src -> violation, output untouched");

        tdr_free(ctx, sfree);
        tdr_free(ctx, suo);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sstr);
    }

    /* ---- ZEROSTRIDE source: broadcast dims expand, others must match
     * (tnkr_view_copy_shape_ok). Source (1,smc,1,1) bcast=CB|WB: lane c
     * holds one byte; the move expands it to (1,smc,1,8) — every dst
     * element in the lane reads the lane's own byte. A non-broadcast dim
     * mismatch (dst (1,smc,2,1)) must be rejected before any write. ---- */
    {
        const uint32_t smc = ctx->info.smc_num;
        uint8_t in_b[64], out[512], expct[512];
        tdr_t d;
        uint8_t sgb, sb_ld, szs, suo, sgo;
        uint32_t c, w;

        for (c = 0; c < smc; c++)
            in_b[c] = (uint8_t)(c * 7u + 3u);
        memcpy(ctx->global + GB, in_b, smc);
        memset(ctx->global + GO, 0, sizeof(out));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GB, 1, (uint16_t)smc, 1, 1);
        sgb = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UB, 1, (uint16_t)smc, 1, 1, 0);
        sb_ld = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_ZEROSTRIDE, TN_DTYPE_S8, UB, 1, (uint16_t)smc, 1, 1,
                         TDR_BCAST_CB | TDR_BCAST_WB);
        szs = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UO, 1, (uint16_t)smc, 1, 8, 0);
        suo = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, (uint16_t)smc, 1, 8);
        sgo = mk_desc(ctx, &d);

        top_ldst_load(sb_ld, sgb, 0, LDST_MODE_NORMAL);
        top_vec_move(suo, 0, szs, 0);
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, smc * 8);
        for (c = 0; c < smc; c++)
            for (w = 0; w < 8; w++)
                expct[c * 8 + w] = in_b[c];
        CHECK(memcmp(out, expct, smc * 8) == 0,
              "move ZEROSTRIDE CB|WB expands (1,smc,1,1)->(1,smc,1,8)");

        /* negative: a non-broadcast dim must still match — H extends 1->2 */
        tdr_free(ctx, suo);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UO, 1, (uint16_t)smc, 2, 1, 0);
        suo = mk_desc(ctx, &d);
        TNVI_BEGIN("shapes must pair legally") { top_vec_move(suo, 0, szs, 0); }
        TNVI_END();

        tdr_free(ctx, sgb);
        tdr_free(ctx, sb_ld);
        tdr_free(ctx, szs);
        tdr_free(ctx, suo);
        tdr_free(ctx, sgo);
    }

    /* ---- FREE footprint crossing the lane window -> violation ----
     * input FREE (1,1,4,8) with addr = lane_bytes - 8 and h/w strides 1/4:
     * span = 3*1 + 3*4 + 1 = 16B, so addr+span exceeds lane_bytes — the
     * flat pre-lane-law formula would reach into the next lane's window.
     * mv must not execute; the poisoned output plane comes back unchanged. */
    {
        const uint32_t lb = ctx->info.urf_bytes / ctx->info.smc_num;
        uint8_t src[NELEM], out[NELEM];
        tdr_t d;
        tdr_stride_t s;
        uint8_t sga, sua, s_in, suo, sgo, sstr;
        int idx;
        uint32_t i;

        for (i = 0; i < NELEM; i++) {
            src[i] = (uint8_t)(i * 3u + 1u);
            out[i] = 0xAA;
        }
        memcpy(ctx->global + GA, src, sizeof(src));
        memcpy(ctx->global + GO, out, sizeof(out));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 1, 4, 8);
        sga = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, 1, 4, 8);
        sgo = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, 1, 4, 8, 0);
        sua = mk_desc(ctx, &d);
        /* output plane gets poisoned first; a successful move would
         * overwrite it with src data */
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UO, 1, 1, 4, 8, 0);
        suo = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_FREE, TN_DTYPE_S8, (uint32_t)(lb - 8), 1, 1, 4, 8, 0);
        s_in = mk_desc(ctx, &d);
        s = tdr_make_stride(4, 1, 0, 0);
        idx = tdr_alloc(ctx);
        if (idx < 0 || tdr_shadow_write_stride(ctx, (uint8_t)idx, &s) != TN_SUCCESS) {
            printf("  FATAL: stride setup failed\n");
            g_fail++;
            sstr = 0;
        } else {
            sstr = (uint8_t)idx;
        }

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
        memset(ctx->urf + UO, 0xAA, NELEM); /* poison output plane */
        TNVI_BEGIN("footprint over lane window") {
            top_vec_move(suo, 0, s_in, sstr); /* must raise a violation */
        }
        TNVI_END();
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));
        for (i = 0; i < NELEM && out[i] == 0xAA; i++)
            ;
        CHECK(i == NELEM, "move FREE crossing lane window -> violation, output untouched");

        tdr_free(ctx, sga);
        tdr_free(ctx, sua);
        tdr_free(ctx, s_in);
        tdr_free(ctx, suo);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sstr);
    }
}
