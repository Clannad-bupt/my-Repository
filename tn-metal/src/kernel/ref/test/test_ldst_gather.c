// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/test/test_ldst_gather.c
 *
 * ldst gather / scatter cases (the only top-engine indexed pick/drop;
 * the traversal core in tensor.cpp is shared with dmr_gather/scatter):
 *   gather W/H with all four OOB fills (zero/one/-1/keep), S16/U8/FP8/
 *   FP32 dtypes, N=2 batching; scatter W/H with OOB drop, duplicate
 *   coordinates (last-write-wins) and untouched-cell preservation;
 *   plus violation negatives for every legality rule.
 * Expectations are pure test-side index arithmetic on the element bit
 * patterns — no numeric decoding anywhere (a NaN pattern must survive
 * the round trip).
 * ========================================================================== */
#include "smoke.h"

/* poison every lane window at addr: an OOB-coordinate read/write that
 * escapes the tensor planes lands on the pattern, never on plausible
 * zeros */
static void poison_urf(tnkr_ctx_t *ctx, uint32_t addr, uint32_t bytes, uint8_t pat) {
    uint32_t l, lane = ctx->info.urf_bytes / ctx->info.smc_num;
    for (l = 0; l < ctx->info.smc_num; l++)
        memset(ctx->urf + (uint64_t)l * lane + addr, pat, bytes);
}

typedef struct {
    int scatter; /* 0 = gather, 1 = scatter */
    tn_dtype_t dtype;
    tn_dtype_t idx_dtype; /* U8/U16/U32 */
    uint16_t n, c, h, w;  /* gather input / scatter output (expanded side) */
    uint16_t k;           /* pick count (index (1,C,1,K)) */
    axis_mode_t mode;
    pad_val_t value; /* gather only */
} gs_cfg_t;

/* input (sh) and output (oh) shapes derived from the expanded side */
static void gs_shapes(const gs_cfg_t *cfg, uint16_t sh[4], uint16_t oh[4]) {
    if (!cfg->scatter) {
        sh[0] = cfg->n;
        sh[1] = cfg->c;
        sh[2] = cfg->h;
        sh[3] = cfg->w;
        oh[0] = cfg->n;
        oh[1] = cfg->c;
        oh[2] = (cfg->mode == AXIS_MODE_W) ? cfg->h : cfg->k;
        oh[3] = (cfg->mode == AXIS_MODE_W) ? cfg->k : cfg->w;
    } else {
        oh[0] = cfg->n;
        oh[1] = cfg->c;
        oh[2] = cfg->h;
        oh[3] = cfg->w;
        sh[0] = cfg->n;
        sh[1] = cfg->c;
        sh[2] = (cfg->mode == AXIS_MODE_W) ? cfg->h : cfg->k;
        sh[3] = (cfg->mode == AXIS_MODE_W) ? cfg->k : cfg->w;
    }
}

/* element byte offset of (n,c,h,w) in a contiguous NCHW buffer */
static size_t gs_off(uint16_t n, uint16_t c, uint16_t h, uint16_t w, uint16_t C, uint16_t H,
                     uint16_t W, unsigned esz) {
    return (((size_t)n * C + c) * H + h) * W * esz + (size_t)w * esz;
}

/* OOB fill bytes, test-side: zero / one / -1 (integers: all-ones, the
 * width-truncated -1; FP: +-1.0 narrowed from the exact IEEE float
 * pattern) */
static void gs_fill_bytes(tn_dtype_t dt, pad_val_t value, uint8_t *dst) {
    const unsigned esz = tn_dtype_size(dt);

    memset(dst, 0, esz);
    if (value == PAD_VAL_ZERO)
        return;
    switch (dt) {
    case TN_DTYPE_FP32: {
        const float v = (value == PAD_VAL_ONE) ? 1.0f : -1.0f;
        memcpy(dst, &v, sizeof(v));
        return;
    }
    case TN_DTYPE_BF16: {
        const float v = (value == PAD_VAL_ONE) ? 1.0f : -1.0f;
        uint32_t b;
        uint16_t h;
        memcpy(&b, &v, sizeof(b));
        h = (uint16_t)(b >> 16); /* +-1.0 are exact: truncation == RNE */
        memcpy(dst, &h, sizeof(h));
        return;
    }
    case TN_DTYPE_FP8_E4M3:
        dst[0] = (value == PAD_VAL_ONE) ? 0x38u : 0xB8u; /* E4M3 +-1.0 */
        return;
    default: /* integers */
        if (value == PAD_VAL_ONE)
            dst[0] = 1;
        else
            memset(dst, 0xFF, esz);
        return;
    }
}

/* one coordinate out of the contiguous (C,K) index buffer */
static uint32_t gs_coord(const uint8_t *IDX, tn_dtype_t idt, uint16_t c, uint16_t k, uint16_t K) {
    const unsigned esz = tn_dtype_size(idt);
    uint32_t v = 0;

    memcpy(&v, IDX + ((size_t)c * K + k) * esz, esz);
    return v;
}

/* Test-side expectation: gather walks the output, scatter walks the
 * input in ascending k (duplicates: last write wins); PRE supplies the
 * untouched cells (keep fills / scatter gaps) */
static void gs_expect(const gs_cfg_t *cfg, const uint8_t *IN, const uint8_t *IDX,
                      const uint8_t *PRE, uint8_t *EXP) {
    const unsigned esz = tn_dtype_size(cfg->dtype);
    uint16_t sh[4], oh[4];
    uint16_t n, c, k, x;

    gs_shapes(cfg, sh, oh);
    for (n = 0; n < cfg->n; n++)
        for (c = 0; c < cfg->c; c++)
            for (k = 0; k < cfg->k; k++) {
                const uint32_t coord = gs_coord(IDX, cfg->idx_dtype, c, k, cfg->k);
                const uint32_t span = (cfg->mode == AXIS_MODE_W) ? cfg->w : cfg->h;
                const uint16_t free_len = (cfg->mode == AXIS_MODE_W) ? cfg->h : cfg->w;

                for (x = 0; x < free_len; x++) {
                    /* output element (gather) / input element (scatter):
                     * mode W: (n,c,x,k), mode H: (n,c,k,x) */
                    const uint16_t eh = (cfg->mode == AXIS_MODE_W) ? x : k;
                    const uint16_t ew = (cfg->mode == AXIS_MODE_W) ? k : x;
                    uint8_t *dst;
                    const uint8_t *src;

                    if (!cfg->scatter) {
                        dst = EXP + gs_off(n, c, eh, ew, oh[1], oh[2], oh[3], esz);
                        if (coord >= span) {
                            if (cfg->value == PAD_VAL_KEEP)
                                memcpy(dst, PRE + gs_off(n, c, eh, ew, oh[1], oh[2], oh[3], esz),
                                       esz);
                            else
                                gs_fill_bytes(cfg->dtype, cfg->value, dst);
                            continue;
                        }
                        src = IN + gs_off(n, c, (cfg->mode == AXIS_MODE_W) ? x : coord,
                                          (cfg->mode == AXIS_MODE_W) ? coord : x, sh[1], sh[2],
                                          sh[3], esz);
                        memcpy(dst, src, esz);
                    } else {
                        /* scatter: input k -> output coord; OOB dropped */
                        if (coord >= span)
                            continue;
                        dst = EXP + gs_off(n, c, (cfg->mode == AXIS_MODE_W) ? x : coord,
                                           (cfg->mode == AXIS_MODE_W) ? coord : x, oh[1], oh[2],
                                           oh[3], esz);
                        src = IN + gs_off(n, c, eh, ew, sh[1], sh[2], sh[3], esz);
                        memcpy(dst, src, esz);
                    }
                }
            }
    if (cfg->scatter) {
        /* untouched output cells keep the prefill: seed EXP from PRE,
         * then apply the writes (ascending k, duplicates: last wins) */
        size_t out_bytes = (size_t)oh[0] * oh[1] * oh[2] * oh[3] * esz;
        memcpy(EXP, PRE, out_bytes);
        for (n = 0; n < cfg->n; n++)
            for (c = 0; c < cfg->c; c++)
                for (k = 0; k < cfg->k; k++) {
                    const uint32_t coord = gs_coord(IDX, cfg->idx_dtype, c, k, cfg->k);
                    const uint32_t span = (cfg->mode == AXIS_MODE_W) ? cfg->w : cfg->h;
                    const uint16_t free_len = (cfg->mode == AXIS_MODE_W) ? cfg->h : cfg->w;
                    if (coord >= span)
                        continue;
                    for (x = 0; x < free_len; x++) {
                        const uint16_t oh_ = (cfg->mode == AXIS_MODE_W) ? x : coord;
                        const uint16_t ow = (cfg->mode == AXIS_MODE_W) ? coord : x;
                        const uint16_t ih = (cfg->mode == AXIS_MODE_W) ? x : k;
                        const uint16_t iw = (cfg->mode == AXIS_MODE_W) ? k : x;
                        memcpy(EXP + gs_off(n, c, oh_, ow, oh[1], oh[2], oh[3], esz),
                               IN + gs_off(n, c, ih, iw, sh[1], sh[2], sh[3], esz), esz);
                    }
                }
    }
}

/* Standard gather/scatter case: IN and IDX go global->URF, the output
 * URF tensor is prefilled from GO (PRE), the op runs, and the
 * stored-back output lands in OUT */
static void run_gs(tnkr_ctx_t *ctx, const gs_cfg_t *cfg, const void *IN, const void *IDX,
                   const void *PRE, void *OUT) {
    uint16_t sh[4], oh[4];
    size_t in_bytes, idx_bytes, out_bytes;
    tdr_t d;
    uint8_t sgi, sgix, sgo, sui, sux, suo;

    gs_shapes(cfg, sh, oh);
    in_bytes = (size_t)sh[0] * sh[1] * sh[2] * sh[3] * tn_dtype_size(cfg->dtype);
    idx_bytes = (size_t)cfg->c * cfg->k * tn_dtype_size(cfg->idx_dtype);
    out_bytes = (size_t)oh[0] * oh[1] * oh[2] * oh[3] * tn_dtype_size(cfg->dtype);

    memcpy(ctx->global + GA, IN, in_bytes);
    memcpy(ctx->global + GB, IDX, idx_bytes);
    memcpy(ctx->global + GO, PRE, out_bytes);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, cfg->dtype, GA, sh[0], sh[1], sh[2], sh[3]);
    sgi = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, cfg->idx_dtype, GB, 1, cfg->c, 1, cfg->k);
    sgix = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, cfg->dtype, GO, oh[0], oh[1], oh[2], oh[3]);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, cfg->dtype, UA, sh[0], sh[1], sh[2], sh[3], 0);
    sui = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, cfg->idx_dtype, UB, 1, cfg->c, 1, cfg->k, 0);
    sux = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, cfg->dtype, UO, oh[0], oh[1], oh[2], oh[3], 0);
    suo = mk_desc(ctx, &d);

    poison_urf(ctx, UA, 1024, 0xAA); /* real cells are (re)written by the loads */
    top_ldst_load(sui, sgi, 0, LDST_MODE_NORMAL);
    top_ldst_load(sux, sgix, 0, LDST_MODE_NORMAL);
    top_ldst_load(suo, sgo, 0, LDST_MODE_NORMAL); /* prefill */
    if (cfg->scatter)
        top_ldst_scatter(suo, sui, sux, cfg->mode);
    else
        top_ldst_gather(suo, sui, sux, cfg->mode, cfg->value);
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(OUT, ctx->global + GO, out_bytes);

    tdr_free(ctx, sgi);
    tdr_free(ctx, sgix);
    tdr_free(ctx, sgo);
    tdr_free(ctx, sui);
    tdr_free(ctx, sux);
    tdr_free(ctx, suo);
}

/* run + compare against the test-side expectation; PRE == NULL poisons
 * the prefill with 0xAA */
static void check_gs(tnkr_ctx_t *ctx, const char *name, const gs_cfg_t *cfg, const void *IN,
                     const void *IDX, const void *PRE) {
    uint16_t sh[4], oh[4];
    const unsigned esz = tn_dtype_size(cfg->dtype);
    size_t out_bytes;
    uint8_t out[128], expct[128], pre[128];

    gs_shapes(cfg, sh, oh);
    out_bytes = (size_t)oh[0] * oh[1] * oh[2] * oh[3] * esz;
    if (out_bytes > sizeof(out)) {
        CHECK(0, "case too large for the harness");
        return;
    }
    if (PRE)
        memcpy(pre, PRE, out_bytes);
    else
        memset(pre, 0xAA, out_bytes);
    gs_expect(cfg, IN, IDX, pre, expct);
    run_gs(ctx, cfg, IN, IDX, pre, out);
    CHECK(memcmp(out, expct, out_bytes) == 0, name);
}

/* Negative selector for check_gs_reject */
enum {
    RJ_IDXDT,  /* index S8 (U8/U16/U32 only) */
    RJ_IDXSH,  /* index dc != input dc (index is (1,C,1,K)) */
    RJ_OUTDT,  /* output dtype != input dtype */
    RJ_OUTSH,  /* gather W: output dw != K */
    RJ_INLYS,  /* input ZEROSTRIDE (all operands HWAlign) */
    RJ_IDXLYS, /* index ZEROSTRIDE */
    RJ_SCATSH, /* scatter: input dw != K */
    RJ_FP4,    /* input FP4 (outside the supported dtype set) */
    RJ_MODE,   /* mode 2 (reserved axis) */
    RJ_PADV    /* pad value 4 (outside the 2-bit field) */
};

/* One-illegality pipeline (gather W, N=1 C=2 H=3 W=4 K=5, S16): valid
 * descriptors except the selected flaw, poisoned output, run, store
 * and require the poison pattern to survive. The index holds valid
 * (zero) coordinates and the URF is pre-poisoned with 0x55, so a
 * wrongly-executing op always leaves a visible trace: an unloadable
 * (bad-layout) operand reads 0x55, anything else reads real data. */
static void check_gs_reject(tnkr_ctx_t *ctx, const char *name, int why) {
    const uint16_t N = 1, C = 2, H = 3, W = 4, K = 5;
    const uint32_t smc = ctx->info.smc_num;
    const int scatter = (why == RJ_SCATSH);
    const axis_mode_t mode = (why == RJ_MODE) ? (axis_mode_t)2 : AXIS_MODE_W;
    const pad_val_t padv = (why == RJ_PADV) ? (pad_val_t)4 : PAD_VAL_ZERO;
    const tn_dtype_t dt = (why == RJ_FP4) ? TN_DTYPE_FP4 : TN_DTYPE_S16;
    const tn_dtype_t idt = (why == RJ_IDXDT) ? TN_DTYPE_S8 : TN_DTYPE_U16;
    const tn_dtype_t odt = (why == RJ_OUTDT) ? TN_DTYPE_S32 : dt;
    const tdr_layout_t inly = (why == RJ_INLYS) ? TDR_LAYOUT_ZEROSTRIDE : TDR_LAYOUT_HWALIGN;
    const tdr_layout_t ixly = (why == RJ_IDXLYS) ? TDR_LAYOUT_ZEROSTRIDE : TDR_LAYOUT_HWALIGN;
    /* RJ_INLYS: a ZEROSTRIDE C-broadcast input (dc == smc_num) keeps
     * every shape check satisfied, so only the layout check guards */
    const uint8_t inbc = (why == RJ_INLYS) ? (uint8_t)TDR_BCAST_CB : 0;
    const uint16_t bigC = (why == RJ_INLYS) ? (uint16_t)smc : C;
    const uint16_t idxC = (why == RJ_IDXSH) ? bigC + 1 : bigC;
    /* gather out (N,C,H,K); RJ_OUTSH widens dw, RJ_MODE takes the H-mode
     * shape so only the mode check stands between valid and invalid */
    const uint16_t oH = (why == RJ_MODE) ? K : H;
    const uint16_t oW = (why == RJ_MODE) ? W : ((why == RJ_OUTSH) ? K + 1 : K);
    /* scatter in (N,C,H,K); RJ_SCATSH shrinks dw */
    const uint16_t sW = (why == RJ_SCATSH) ? K - 1 : K;
    const char *vexp;
    uint8_t out[1024], poison[1024];
    size_t out_bytes;
    tdr_t d;
    uint8_t sgi, sgix, sgo, sui, sux, suo;

    /* expected violation substring per flaw */
    switch (why) {
    case RJ_IDXDT:
        vexp = "index dtype";
        break;
    case RJ_IDXSH:
        vexp = "index shape";
        break;
    case RJ_OUTDT:
    case RJ_FP4:
        vexp = "supported set";
        break;
    case RJ_OUTSH:
        vexp = "output H must match";
        break;
    case RJ_INLYS:
    case RJ_IDXLYS:
        vexp = "operands must be HWAlign";
        break;
    case RJ_MODE:
        vexp = "mode must be W or H";
        break;
    case RJ_PADV:
        vexp = "pad value encoding unsupported";
        break;
    default: /* RJ_SCATSH */
        vexp = "input W must match";
        break;
    }

    memset(ctx->global + GA, 0x11, sizeof(out));
    memset(ctx->global + GB, 0, sizeof(out)); /* valid zero coordinates */
    memset(ctx->global + GO, 0xAA, sizeof(out));
    memset(poison, 0xAA, sizeof(poison));
    poison_urf(ctx, UA, 1024, 0x55);

    if (scatter) {
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, dt, GA, N, C, H, sW);
        sgi = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, dt, UA, N, C, H, sW, 0);
        sui = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, odt, GO, N, C, H, W);
        sgo = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, odt, UO, N, C, H, W, 0);
        suo = mk_desc(ctx, &d);
        out_bytes = (size_t)N * C * H * W * tn_dtype_size(odt);
    } else {
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, dt, GA, N, bigC, H, W);
        sgi = mk_desc(ctx, &d);
        d = tdr_make_urf(inly, dt, UA, N, bigC, H, W, inbc);
        sui = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, odt, GO, N, bigC, oH, oW);
        sgo = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, odt, UO, N, bigC, oH, oW, 0);
        suo = mk_desc(ctx, &d);
        out_bytes = (size_t)N * bigC * oH * oW * tn_dtype_size(odt);
    }
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, idt, GB, 1, idxC, 1, K);
    sgix = mk_desc(ctx, &d);
    d = tdr_make_urf(ixly, idt, UB, 1, idxC, 1, K, 0);
    sux = mk_desc(ctx, &d);

    if (inly == TDR_LAYOUT_HWALIGN)
        top_ldst_load(sui, sgi, 0, LDST_MODE_NORMAL); /* ZS input is built in URF */
    if (ixly == TDR_LAYOUT_HWALIGN)
        top_ldst_load(sux, sgix, 0, LDST_MODE_NORMAL);
    top_ldst_load(suo, sgo, 0, LDST_MODE_NORMAL); /* poison prefill */
    TNVI_BEGIN(vexp) {
        if (scatter)
            top_ldst_scatter(suo, sui, sux, mode);
        else
            top_ldst_gather(suo, sui, sux, mode, padv);
    }
    TNVI_END();
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(out, ctx->global + GO, out_bytes);
    CHECK(memcmp(out, poison, out_bytes) == 0, name);

    tdr_free(ctx, sgi);
    tdr_free(ctx, sgix);
    tdr_free(ctx, sgo);
    tdr_free(ctx, sui);
    tdr_free(ctx, sux);
    tdr_free(ctx, suo);
}

void suite_ldst_gather(tnkr_ctx_t *ctx) {
    /* base shape: N=1 C=2 H=3 W=4, K=5; per-channel coordinate rows with
     * OOB picks (>= W) at k=4 of channel 0 and k=4 of channel 1 */
    static const uint16_t IN1[24] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
                                     13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24};
    static const uint16_t IX1[10] = {0, 1, 2, 3, 4, /* ch0 */ 3, 2, 1, 0, 5 /* ch1 */};
    static const uint16_t KEEP1[30] = {100, 101, 102, 103, 104, 105, 106, 107, 108, 109,
                                       110, 111, 112, 113, 114, 115, 116, 117, 118, 119,
                                       120, 121, 122, 123, 124, 125, 126, 127, 128, 129};
    uint16_t i;

    printf("== ldst_gather ==\n");

    /* ---- gather W, S16: all four OOB fills ---- */
    {
        static const pad_val_t VALS[4] = {PAD_VAL_ZERO, PAD_VAL_ONE, PAD_VAL_MINUS_ONE,
                                          PAD_VAL_KEEP};
        static const char *const VNAME[4] = {"zero", "one", "-1(all-ones)", "keep"};
        for (i = 0; i < 4; i++) {
            char name[64];
            gs_cfg_t cfg = {0, TN_DTYPE_S16, TN_DTYPE_U16, 1, 2, 3, 4, 5, AXIS_MODE_W, VALS[i]};
            snprintf(name, sizeof(name), "gather W S16 OOB fill %s", VNAME[i]);
            check_gs(ctx, name, &cfg, IN1, IX1, VALS[i] == PAD_VAL_KEEP ? KEEP1 : NULL);
        }
    }

    /* ---- gather H, S16 (coordinates >= H are OOB) ---- */
    {
        static const uint16_t IXH[10] = {0, 1, 2, 3, 5, /* ch0 */ 2, 1, 0, 4, 3 /* ch1 */};
        gs_cfg_t cfg = {0, TN_DTYPE_S16, TN_DTYPE_U16, 1, 2, 3, 4, 5, AXIS_MODE_H, PAD_VAL_ZERO};
        check_gs(ctx, "gather H S16", &cfg, IN1, IXH, NULL);
    }

    /* ---- gather W, U8 data + U8 index: -1 fills 0xFF ---- */
    {
        static const uint8_t IN8[12] = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21};
        static const uint8_t IX8[8] = {2, 0, 1, 3, /* ch0 */ 1, 3, 2, 0 /* ch1 */};
        gs_cfg_t cfg = {0, TN_DTYPE_U8, TN_DTYPE_U8, 1, 2, 2, 3, 4, AXIS_MODE_W, PAD_VAL_MINUS_ONE};
        check_gs(ctx, "gather W U8 fill -1", &cfg, IN8, IX8, NULL);
    }

    /* ---- gather W, FP8: raw bit patterns copy verbatim (0x7F = NaN,
     * 0x80 = -0); fill one = E4M3 1.0 ---- */
    {
        static const uint8_t INF8[8] = {0x01, 0x7F, 0x80, 0xC0, /* ch0 */
                                        0x3C, 0xFE, 0x55, 0x00 /* ch1 */};
        static const uint16_t IXF[6] = {0, 4, 2, /* ch0 */ 1, 3, 9 /* ch1 */};
        gs_cfg_t cfg = {0, TN_DTYPE_FP8_E4M3, TN_DTYPE_U16, 1, 2, 1, 4,
                        3, AXIS_MODE_W,       PAD_VAL_ONE};
        check_gs(ctx, "gather W FP8 bit patterns (NaN/-0 copy)", &cfg, INF8, IXF, NULL);
    }

    /* ---- gather W, FP32: fill -1.0 ---- */
    {
        static const float INF32[3] = {1.5f, -2.0f, 3.25f};
        static const uint16_t IX32[3] = {2, 3, 0};
        gs_cfg_t cfg = {0, TN_DTYPE_FP32, TN_DTYPE_U16,     1, 1, 1, 3,
                        3, AXIS_MODE_W,   PAD_VAL_MINUS_ONE};
        check_gs(ctx, "gather W FP32 fill -1", &cfg, INF32, IX32, NULL);
    }

    /* ---- gather W, N=2 batch (n indexing on both sides) ---- */
    {
        static const uint16_t IXB[4] = {0, 2, /* ch0 */ 1, 1 /* ch1 */};
        gs_cfg_t cfg = {0, TN_DTYPE_S16, TN_DTYPE_U16, 2, 2, 2, 3, 2, AXIS_MODE_W, PAD_VAL_ZERO};
        check_gs(ctx, "gather W S16 N=2", &cfg, IN1, IXB, NULL);
    }

    /* ---- scatter W, S16: duplicate coordinates (last write wins), OOB
     * dropped, untouched cells keep the poison prefill ---- */
    {
        static const uint16_t IXS[8] = {0, 2, 2, 7, /* ch0 */ 4, 1, 1, 3 /* ch1 */};
        gs_cfg_t cfg = {1, TN_DTYPE_S16, TN_DTYPE_U16, 1, 2, 2, 5, 4, AXIS_MODE_W, PAD_VAL_ZERO};
        check_gs(ctx, "scatter W S16 dup last-wins + OOB drop", &cfg, IN1, IXS, NULL);
    }

    /* ---- scatter H, S16 ---- */
    {
        static const uint16_t IXSH[6] = {0, 1, 3, /* ch0 */ 2, 0, 9 /* ch1 */};
        gs_cfg_t cfg = {1, TN_DTYPE_S16, TN_DTYPE_U16, 1, 2, 4, 2, 3, AXIS_MODE_H, PAD_VAL_ZERO};
        check_gs(ctx, "scatter H S16", &cfg, IN1, IXSH, NULL);
    }

    /* ---- scatter W, S32: duplicate coordinate across k ---- */
    {
        static const int32_t IN32[3] = {10, 20, 30};
        static const uint16_t IXS32[3] = {3, 1, 1};
        gs_cfg_t cfg = {1, TN_DTYPE_S32, TN_DTYPE_U16, 1, 1, 1, 4, 3, AXIS_MODE_W, PAD_VAL_ZERO};
        check_gs(ctx, "scatter W S32 dup last-wins", &cfg, IN32, IXS32, NULL);
    }

    /* ---- negatives: violation raised, output keeps its poison pattern ---- */
    check_gs_reject(ctx, "index S8 -> violation", RJ_IDXDT);
    check_gs_reject(ctx, "index dc != input dc -> violation", RJ_IDXSH);
    check_gs_reject(ctx, "output dtype != input dtype -> violation", RJ_OUTDT);
    check_gs_reject(ctx, "gather W output dw != K -> violation", RJ_OUTSH);
    check_gs_reject(ctx, "input ZEROSTRIDE -> violation", RJ_INLYS);
    check_gs_reject(ctx, "index ZEROSTRIDE -> violation", RJ_IDXLYS);
    check_gs_reject(ctx, "scatter input dw != K -> violation", RJ_SCATSH);
    check_gs_reject(ctx, "input FP4 -> violation", RJ_FP4);
    check_gs_reject(ctx, "mode 2 (reserved) -> violation", RJ_MODE);
    check_gs_reject(ctx, "pad value 4 (unsupported) -> violation", RJ_PADV);
}
