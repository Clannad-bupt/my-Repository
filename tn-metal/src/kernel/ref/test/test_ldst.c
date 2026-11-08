// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/test/test_ldst.c
 *
 * top/ldst family cases: load -> store loopback + HWALIGN address-formula
 * direct assertions + NC_TRANS transpose (load and store sides verified
 * independently) + WALIGN load (per-row urf_width placement, gap poison,
 * violation negatives) + unsupported mode encodings (violation, the
 * destination keeps its poison pattern). This file is the sole "poke
 * ctx->urf directly" exception (needed by the formula assertions), see the
 * smoke.h data-path convention.
 * ========================================================================== */
#include "smoke.h"

void suite_ldst(tnkr_ctx_t *ctx) {
    uint8_t src[128], dst[128]; /* (2,2,4,8) = 128 elements */
    uint8_t ncs[192], ncd[192]; /* NC_TRANS: (2,3,4,8) = 192 */
    uint8_t wsrc[320];          /* WALIGN: (2,4,4,10) = 320 elements */
    tdr_t d;
    uint8_t sg_in, su, sg_out;
    uint8_t su2, sg_out2, sg_bad;
    uint8_t sg_wout, sg_big, su_big;
    uint32_t i, n, c, h, w, e, fails;
    uint64_t lane_bytes;

    printf("== load/store loopback ==\n");

    /* (2,2,4,8) S8: a plane fills exactly one alignment unit; c -> lane;
     * planes stack in n/c-block order */
    for (i = 0; i < 128; i++)
        src[i] = (uint8_t)(i * 7u + 3u);
    memcpy(ctx->global + GA, src, sizeof(src));
    memset(ctx->global + GO, 0, sizeof(dst));

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 2, 2, 4, 8);
    sg_in = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, 0, 2, 2, 4, 8, 0);
    su = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 2, 2, 4, 8);
    sg_out = mk_desc(ctx, &d);

    top_ldst_load(su, sg_in, 0, LDST_MODE_NORMAL);
    /* formula assertions (equal-capacity window model): lane l's window
     * base = l * lane_bytes; planes within a window stack in [n][cb] */
    lane_bytes = ctx->info.urf_bytes / ctx->info.smc_num;
    CHECK(memcmp(ctx->urf, src, TDR_URF_WIDTH_BYTES) == 0,
          "HWALIGN: (n0,c0) -> smc0 window 1st plane");
    CHECK(memcmp(ctx->urf + lane_bytes, src + 4 * 8, TDR_URF_WIDTH_BYTES) == 0,
          "HWALIGN: (n0,c1) -> smc1 window 1st plane");
    CHECK(memcmp(ctx->urf + TDR_URF_WIDTH_BYTES, src + 2 * 4 * 8, TDR_URF_WIDTH_BYTES) == 0,
          "HWALIGN: (n1,c0) -> smc0 window 2nd plane");
    CHECK(memcmp(ctx->urf + lane_bytes + TDR_URF_WIDTH_BYTES, src + 3 * 4 * 8,
                 TDR_URF_WIDTH_BYTES) == 0,
          "HWALIGN: (n1,c1) -> smc1 window 2nd plane");

    top_ldst_store(sg_out, 0, su, LDST_MODE_NORMAL);
    memcpy(dst, ctx->global + GO, sizeof(dst));
    CHECK(memcmp(src, dst, sizeof(src)) == 0, "load->store loopback identical");

    tdr_free(ctx, sg_in);
    tdr_free(ctx, su);
    tdr_free(ctx, sg_out);

    /* ---- NC_TRANS: N/C swap transpose (the semantics are the ref model's
     * interpretation; the cases pin the current interpretation so a behavior
     * change becomes visible when the documentation is aligned). Each
     * direction is read back with a NORMAL op on the opposite side, so the
     * load-side and store-side transposes are checked independently ---- */
    printf("== NC_TRANS transpose ==\n");

    /* (2,3,4,8) S8 = 192 elements */
    for (i = 0; i < 192; i++)
        ncs[i] = (uint8_t)(i * 5u + 1u);
    memcpy(ctx->global + GA, ncs, 192);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 2, 3, 4, 8);
    sg_in = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 3, 2, 4, 8, 0);
    su = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 3, 2, 4, 8);
    sg_out = mk_desc(ctx, &d);

    /* load NC_TRANS + store NORMAL readout: expect out(n,c,h,w) = src(c,n,h,w) */
    top_ldst_load(su, sg_in, 0, LDST_MODE_NC_TRANS);
    top_ldst_store(sg_out, 0, su, LDST_MODE_NORMAL);
    memcpy(ncd, ctx->global + GO, 192);
    fails = 0;
    for (n = 0; n < 2; n++)
        for (c = 0; c < 3; c++)
            for (h = 0; h < 4; h++)
                for (w = 0; w < 8; w++)
                    if (ncd[((c * 2 + n) * 4 + h) * 8 + w] != ncs[((n * 3 + c) * 4 + h) * 8 + w])
                        fails++;
    CHECK(fails == 0, "load NC_TRANS: out(n,c,h,w) = in(c,n,h,w)");

    /* store NC_TRANS: NORMAL load into URF (2,3,4,8), transposed write-out
     * (3,2,4,8); same expectation — together with the previous case this
     * proves each side implements the transpose itself */
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UB, 2, 3, 4, 8, 0);
    su2 = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GB, 3, 2, 4, 8);
    sg_out2 = mk_desc(ctx, &d);
    top_ldst_load(su2, sg_in, 0, LDST_MODE_NORMAL);
    top_ldst_store(sg_out2, 0, su2, LDST_MODE_NC_TRANS);
    memcpy(ncd, ctx->global + GB, 192);
    fails = 0;
    for (n = 0; n < 2; n++)
        for (c = 0; c < 3; c++)
            for (h = 0; h < 4; h++)
                for (w = 0; w < 8; w++)
                    if (ncd[((c * 2 + n) * 4 + h) * 8 + w] != ncs[((n * 3 + c) * 4 + h) * 8 + w])
                        fails++;
    CHECK(fails == 0, "store NC_TRANS: out(n,c,h,w) = in(c,n,h,w)");

    /* negative: shape does not satisfy the N/C swap (source (2,3), target
     * (4,3)) -> violation */
    memset(ctx->global + GB, 0xAA, 192);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GB, 4, 3, 4, 8);
    sg_bad = mk_desc(ctx, &d);
    TNVI_BEGIN("do not pair") { top_ldst_store(sg_bad, 0, su2, LDST_MODE_NC_TRANS); }
    TNVI_END();
    fails = 0;
    for (i = 0; i < 192; i++)
        if (ctx->global[GB + i] != 0xAA)
            fails++;
    CHECK(fails == 0, "NC_TRANS shape mismatch -> violation, target untouched");

    tdr_free(ctx, sg_in);
    tdr_free(ctx, su);
    tdr_free(ctx, sg_out);
    tdr_free(ctx, su2);
    tdr_free(ctx, sg_out2);
    tdr_free(ctx, sg_bad);

    /* ---- WALIGN load: per-row urf_width placement (address-formula
     * direct assertion) ----
     * (2,4,4,10) S8: each h row is 10B, so row_bytes = 32 and the 22B
     * gap is dead space the load materializes; c -> lane direct map,
     * planes stack in [n] order (dc=4 < smc_num -> one c-block) */
    printf("== WALIGN load ==\n");

    for (i = 0; i < 320; i++)
        wsrc[i] = (uint8_t)(i * 11u + 5u);
    memcpy(ctx->global + GA, wsrc, sizeof(wsrc));
    for (c = 0; c < 4; c++)
        memset(ctx->urf + (size_t)c * lane_bytes, 0xAA, 512);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 2, 4, 4, 10);
    sg_in = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_WALIGN, TN_DTYPE_S8, 0, 2, 4, 4, 10, 0);
    su = mk_desc(ctx, &d);
    top_ldst_load(su, sg_in, 0, LDST_MODE_NORMAL);

    /* row (n,c,h) lives at lane c, offset n*(H rows)*32B + h*32B; the
     * 10 data bytes match the global row, bytes [10,32) keep the poison */
    fails = 0;
    for (n = 0; n < 2 && fails == 0; n++)
        for (c = 0; c < 4 && fails == 0; c++)
            for (h = 0; h < 4 && fails == 0; h++) {
                const uint8_t *row =
                    ctx->urf + (size_t)c * lane_bytes + (size_t)n * (4u * 32u) + (size_t)h * 32u;
                if (memcmp(row, wsrc + ((size_t)n * 4u + c) * 4u * 10u + (size_t)h * 10u, 10) != 0)
                    fails++;
                for (e = 10; e < 32; e++)
                    if (row[e] != 0xAA)
                        fails++;
            }
    CHECK(fails == 0, "WALIGN load: rows packed at urf_width, gaps keep poison");

    /* negative: store.s URF side does not accept WALIGN -> violation */
    memset(ctx->global + GO, 0xAA, 320);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 2, 4, 4, 10);
    sg_wout = mk_desc(ctx, &d);
    TNVI_BEGIN("URF side layout must be HWALIGN") {
        top_ldst_store(sg_wout, 0, su, LDST_MODE_NORMAL);
    }
    TNVI_END();
    fails = 0;
    for (i = 0; i < 320; i++)
        if (ctx->global[GO + i] != 0xAA)
            fails++;
    CHECK(fails == 0, "WALIGN store (URF side) -> violation");

    /* negative: WALIGN footprint crosses the lane window (8192 rows x
     * 32B = 256KB > lane_bytes) -> resolve rejects with a violation */
    for (c = 0; c < 8; c++)
        memset(ctx->urf + (size_t)c * lane_bytes, 0x55, 64);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GB, 1, 8, 8192, 10);
    sg_big = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_WALIGN, TN_DTYPE_S8, 0, 1, 8, 8192, 10, 0);
    su_big = mk_desc(ctx, &d);
    TNVI_BEGIN("footprint over lane window") { top_ldst_load(su_big, sg_big, 0, LDST_MODE_NORMAL); }
    TNVI_END();
    fails = 0;
    for (c = 0; c < 8; c++)
        for (i = 0; i < 64; i++)
            if (ctx->urf[(size_t)c * lane_bytes + i] != 0x55)
                fails++;
    CHECK(fails == 0, "WALIGN footprint over lane window -> violation");

    tdr_free(ctx, sg_in);
    tdr_free(ctx, su);
    tdr_free(ctx, sg_wout);
    tdr_free(ctx, sg_big);
    tdr_free(ctx, su_big);

    /* ---- c-block stacking: C > smc_num wraps to the next c-block of
     * the same lane (no cross-SMC data path) ----
     * (1, smc_num+2, 4, 8) S8: plane = align_up(32B) = 32B, cblocks = 2;
     * channels c >= smc_num land on lane c%smc_num at c-block 1 */
    printf("== c-block (C > smc_num) ==\n");
    {
        const uint32_t C = ctx->info.smc_num + 2;
        const uint32_t plane = TDR_URF_WIDTH_BYTES; /* align_up(4*8*S8) */
        uint8_t csrc[(ctx->info.smc_num + 2) * 32];
        uint8_t cdst[(ctx->info.smc_num + 2) * 32];
        uint32_t smc = ctx->info.smc_num;

        /* channel-dependent pattern: planes must all differ (a pure i-based
         * pattern with period 256B makes c and c+smc planes identical and
         * the case blind to c-block placement) */
        for (c = 0; c < C; c++)
            for (i = 0; i < 32u; i++)
                csrc[c * 32u + i] = (uint8_t)(c * 29u + i * 13u + 7u);
        memcpy(ctx->global + GA, csrc, C * 32u);
        memset(ctx->global + GO, 0, sizeof(cdst));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, (uint16_t)C, 4, 8);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, 0, 1, (uint16_t)C, 4, 8, 0);
        su = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, (uint16_t)C, 4, 8);
        sg_out = mk_desc(ctx, &d);

        top_ldst_load(su, sg_in, 0, LDST_MODE_NORMAL);
        fails = 0;
        for (c = 0; c < C; c++) {
            const uint8_t *p =
                ctx->urf + (size_t)(c % smc) * lane_bytes + (size_t)(c / smc) * plane;
            if (memcmp(p, csrc + (size_t)c * 32u, 32) != 0)
                fails++;
        }
        CHECK(fails == 0, "HWALIGN c-block: c >= smc_num wraps to same lane, next c-block");

        top_ldst_store(sg_out, 0, su, LDST_MODE_NORMAL);
        memcpy(cdst, ctx->global + GO, sizeof(cdst));
        CHECK(memcmp(csrc, cdst, sizeof(csrc)) == 0, "c-block load->store loopback identical");

        /* negative: cblocks x plane crosses the lane window ((1,2048,64,64)
         * S8: 64 c-blocks x 4KB = 256KB > lane_bytes) -> violation */
        memset(ctx->urf, 0x55, 64);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GB, 1, 2048, 64, 64);
        sg_big = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, 0, 1, 2048, 64, 64, 0);
        su_big = mk_desc(ctx, &d);
        TNVI_BEGIN("footprint over lane window") {
            top_ldst_load(su_big, sg_big, 0, LDST_MODE_NORMAL);
        }
        TNVI_END();
        fails = 0;
        for (i = 0; i < 64; i++)
            if (ctx->urf[i] != 0x55)
                fails++;
        CHECK(fails == 0, "HWALIGN c-block footprint over lane window -> violation");

        tdr_free(ctx, sg_in);
        tdr_free(ctx, su);
        tdr_free(ctx, sg_out);
        tdr_free(ctx, sg_big);
        tdr_free(ctx, su_big);
    }

    /* ---- broadcast load: global FREE with c_str=0, URF HWALIGN
     * (1,smc,4,8) -> one source plane physically replicated per lane ----
     * load has no broadcast mode; C-dim broadcast is materialized at
     * load time by a zero c-stride on the read side (the source is one
     * plane, every logical channel reads it), so each lane window ends
     * up holding its own full copy — the prerequisite of ZEROSTRIDE CB
     * compute-time reads */
    printf("== broadcast load ==\n");
    {
        const uint32_t smc = ctx->info.smc_num;
        uint8_t bsrc[32];
        uint8_t bgot[smc * 32];
        tdr_stride_t s;
        uint8_t sg_in, su, sg_out, sstr;
        int idx;

        for (i = 0; i < 32; i++)
            bsrc[i] = (uint8_t)(i * 17u + 9u);
        memcpy(ctx->global + GA, bsrc, 32);
        memset(ctx->global + GO, 0, sizeof(bgot));

        d = tdr_make_global(TDR_LAYOUT_FREE, TN_DTYPE_S8, GA, 1, (uint16_t)smc, 4, 8);
        sg_in = mk_desc(ctx, &d);
        s = tdr_make_stride(1, 8, 0, 0); /* w=1, h=8, c=0, n=0 */
        idx = tdr_alloc(ctx);
        if (idx < 0 || tdr_shadow_write_stride(ctx, (uint8_t)idx, &s) != TN_SUCCESS) {
            printf("  FATAL: stride setup failed\n");
            g_fail++;
            sstr = 0;
        } else {
            sstr = (uint8_t)idx;
        }
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, 0, 1, (uint16_t)smc, 4, 8, 0);
        su = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, (uint16_t)smc, 4, 8);
        sg_out = mk_desc(ctx, &d);

        top_ldst_load(su, sg_in, sstr, LDST_MODE_NORMAL);
        fails = 0;
        for (c = 0; c < smc; c++)
            if (memcmp(ctx->urf + (size_t)c * lane_bytes, bsrc, 32) != 0)
                fails++;
        CHECK(fails == 0, "broadcast load: FREE c_str=0 replicates one plane per lane");

        top_ldst_store(sg_out, 0, su, LDST_MODE_NORMAL);
        memcpy(bgot, ctx->global + GO, sizeof(bgot));
        fails = 0;
        for (c = 0; c < smc; c++)
            if (memcmp(bgot + (size_t)c * 32u, bsrc, 32) != 0)
                fails++;
        CHECK(fails == 0, "broadcast load->store loopback: every channel holds the plane");

        tdr_free(ctx, sg_in);
        tdr_free(ctx, su);
        tdr_free(ctx, sg_out);
        tdr_free(ctx, sstr);
    }

    /* ---- GLOBAL FREE wstride != 1 -> violation (spec: W is the
     * contiguous dim on the external side; only stride 0 broadcast and 1
     * are meaningful, and a W gap breaks the lane-window load model).
     * The load must raise before any write — poisoned URF stays ---- */
    printf("== GLOBAL FREE wstride pin ==\n");
    {
        tdr_stride_t s;
        uint8_t sg_in, su, sstr;
        int idx;

        d = tdr_make_global(TDR_LAYOUT_FREE, TN_DTYPE_S8, GA, 1, 1, 4, 8);
        sg_in = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, 1, 4, 8, 0);
        su = mk_desc(ctx, &d);
        s = tdr_make_stride(2, 8, 0, 0); /* w_str=2: illegal on GLOBAL FREE */
        idx = tdr_alloc(ctx);
        if (idx < 0 || tdr_shadow_write_stride(ctx, (uint8_t)idx, &s) != TN_SUCCESS) {
            printf("  FATAL: stride setup failed\n");
            g_fail++;
            sstr = 0;
        } else {
            sstr = (uint8_t)idx;
        }

        memset(ctx->urf + UA, 0xAA, NELEM);
        TNVI_BEGIN("GLOBAL FREE wstride != 1") { top_ldst_load(su, sg_in, sstr, LDST_MODE_NORMAL); }
        TNVI_END();
        fails = 0;
        for (i = 0; i < NELEM; i++)
            if (ctx->urf[UA + i] != 0xAA)
                fails++;
        CHECK(fails == 0, "GLOBAL FREE ws=2 load -> violation, URF untouched");

        tdr_free(ctx, sg_in);
        tdr_free(ctx, su);
        tdr_free(ctx, sstr);
    }

    /* ---- unsupported mode encodings are violations: CW_TRANS (arrives
     * with dmr) and 3 (undefined) must raise before any write — the
     * poisoned destination proves nothing ran. Shapes are the
     * proven-valid (2,2,4,8) loopback pair, so mode handling is the only
     * variable ---- */
    printf("== unsupported load/store modes ==\n");

    for (i = 0; i < 128; i++)
        src[i] = (uint8_t)(i * 3u + 1u);
    memcpy(ctx->global + GA, src, sizeof(src));

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 2, 2, 4, 8);
    sg_in = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 2, 2, 4, 8, 0);
    su = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 2, 2, 4, 8);
    sg_out = mk_desc(ctx, &d);

    /* load: the URF footprint (n x c-block planes per lane) keeps its
     * poison — a wrong write would land exactly there */
    for (e = 0; e < 2; e++) { /* e: 0 = CW_TRANS, 1 = mode 3 */
        const ldst_mode_t md = e == 0 ? LDST_MODE_CW_TRANS : (ldst_mode_t)3;
        for (c = 0; c < 2; c++)
            memset(ctx->urf + (size_t)c * lane_bytes, 0xAA, 2 * TDR_URF_WIDTH_BYTES);
        TNVI_BEGIN("mode must be") { top_ldst_load(su, sg_in, 0, md); }
        TNVI_END();
        fails = 0;
        for (c = 0; c < 2; c++)
            for (i = 0; i < 2 * TDR_URF_WIDTH_BYTES; i++)
                if (ctx->urf[(size_t)c * lane_bytes + i] != 0xAA)
                    fails++;
        CHECK(fails == 0, e == 0 ? "load CW_TRANS -> violation, URF untouched"
                                 : "load mode 3 -> violation, URF untouched");
    }

    /* store: the global destination keeps its poison (the URF source is
     * loaded normally first, so a wrong write would be the loopback
     * data) */
    top_ldst_load(su, sg_in, 0, LDST_MODE_NORMAL);
    for (e = 0; e < 2; e++) {
        const ldst_mode_t md = e == 0 ? LDST_MODE_CW_TRANS : (ldst_mode_t)3;
        memset(ctx->global + GO, 0xAA, 128);
        TNVI_BEGIN("mode must be") { top_ldst_store(sg_out, 0, su, md); }
        TNVI_END();
        fails = 0;
        for (i = 0; i < 128; i++)
            if (ctx->global[GO + i] != 0xAA)
                fails++;
        CHECK(fails == 0, e == 0 ? "store CW_TRANS -> violation, global untouched"
                                 : "store mode 3 -> violation, global untouched");
    }

    tdr_free(ctx, sg_in);
    tdr_free(ctx, su);
    tdr_free(ctx, sg_out);
}
