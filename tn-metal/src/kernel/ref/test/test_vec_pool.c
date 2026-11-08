// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/test/test_vec_pool.c
 *
 * top/vec pool cases: max / min / sum over KHxKW windows with stride and
 * asymmetric padding; padding cells contribute the operation identity
 * (max: -inf, min: +inf, sum: 0); OH/OW follow the floor formula.
 * Expectations are computed independently on the test side (own window
 * loop over the float image). Plus violation negatives (stride 0,
 * kernel exceeding the padded input, integer input).
 * ========================================================================== */
#include "smoke.h"

typedef void (*pool_fn)(tdr_idx_t, tdr_idx_t, const pool_attr_t *);

/* mechanical BF16 pattern of a float (top 16 bits of the FP32 image) */
static uint16_t bf16_of(float f) {
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return (uint16_t)(u >> 16);
}

/* Standard pool case: the image at GA is pooled; the result lands in OUT */
static void run_pool(tnkr_ctx_t *ctx, pool_fn fn, uint16_t H, uint16_t W, uint16_t OH, uint16_t OW,
                     const pool_attr_t *attr, tn_dtype_t dt, const void *IN, void *OUT) {
    size_t isz = (size_t)H * W * tn_dtype_size(dt);
    size_t osz = (size_t)OH * OW * tn_dtype_size(dt);
    tdr_t d;
    uint8_t sga, sgo, sua, suo;

    memcpy(ctx->global + GA, IN, isz);
    memset(ctx->global + GO, 0xAA, osz);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, dt, GA, 1, 1, H, W);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, dt, GO, 1, 1, OH, OW);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, dt, UA, 1, 1, H, W, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, dt, UO, 1, 1, OH, OW, 0);
    suo = mk_desc(ctx, &d);

    top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
    fn(suo, sua, attr);
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(OUT, ctx->global + GO, osz);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgo);
    tdr_free(ctx, sua);
    tdr_free(ctx, suo);
}

/* Test-side independent expectation: own window loop, identity-filled
 * padding; op: 'x' max, 'n' min, 's' sum (no all-padding windows in the
 * cases below) */
static void pool_expect(const float *IN, uint16_t H, uint16_t W, const pool_attr_t *attr,
                        uint16_t OH, uint16_t OW, char op, float *EXP) {
    uint16_t oh, ow;
    uint32_t kh, kw;

    for (oh = 0; oh < OH; oh++)
        for (ow = 0; ow < OW; ow++) {
            double acc = (op == 'x') ? -1e30 : (op == 'n') ? 1e30 : 0.0;
            for (kh = 0; kh < attr->kernel_h; kh++)
                for (kw = 0; kw < attr->kernel_w; kw++) {
                    int r = (int)oh * attr->stride_h + (int)kh - attr->pad_t;
                    int q = (int)ow * attr->stride_w + (int)kw - attr->pad_l;
                    float v;
                    if (r < 0 || r >= H || q < 0 || q >= W)
                        continue; /* padding: identity */
                    v = IN[(uint16_t)r * W + (uint16_t)q];
                    if (op == 'x') {
                        if (v > acc)
                            acc = v;
                    } else if (op == 'n') {
                        if (v < acc)
                            acc = v;
                    } else {
                        acc += v;
                    }
                }
            EXP[oh * OW + ow] = (float)acc;
        }
}

/* Negative selector for check_pool_reject */
enum { PJ_STRIDE0, PJ_KBIG, PJ_INTIN };

/* One-illegality pool pipeline (FP32 (1,1,4,8) K2 s2 -> (1,1,2,4), except
 * the selected flaw); the poisoned output must survive */
static void check_pool_reject(tnkr_ctx_t *ctx, const char *name, int why) {
    const tn_dtype_t dt = (why == PJ_INTIN) ? TN_DTYPE_S8 : TN_DTYPE_FP32;
    const size_t osz = 8 * tn_dtype_size(dt); /* stored extent (1,1,2,4) */
    pool_attr_t attr = {2, 2, 0, 0, 0, 0, 2, 2};
    uint8_t out[32], poison[32];
    tdr_t d;
    uint8_t sga, sgo, sua, suo;

    if (why == PJ_STRIDE0)
        attr.stride_h = 0;
    if (why == PJ_KBIG)
        attr.kernel_h = 5;

    memset(ctx->global + GA, 0x11, 4 * 8 * tn_dtype_size(dt));
    memset(poison, 0xAA, sizeof(poison));

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, dt, GA, 1, 1, 4, 8);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, dt, GO, 1, 1, 2, 4);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, dt, UA, 1, 1, 4, 8, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, dt, UO, 1, 1, 2, 4, 0);
    suo = mk_desc(ctx, &d);

    top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
    memset(ctx->urf + UO, 0xAA, osz); /* poison the output plane */
    TNVI_BEGIN("vec_pool") { top_vec_pool_max(suo, sua, &attr); }
    TNVI_END();
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(out, ctx->global + GO, osz);
    CHECK(memcmp(out, poison, osz) == 0, name);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgo);
    tdr_free(ctx, sua);
    tdr_free(ctx, suo);
}

void suite_vec_pool(tnkr_ctx_t *ctx) {
    uint32_t i;

    printf("== vec_pool ==\n");

    /* ---- pool_max: (1,1,4,8) K2 stride 2, no pad -> (1,1,2,4) ---- */
    {
        pool_attr_t attr = {2, 2, 0, 0, 0, 0, 2, 2};
        float in[32], out[8], expct[8];

        for (i = 0; i < 32; i++)
            in[i] = (float)((i * 37) % 13) * 0.5f - 3.0f;
        pool_expect(in, 4, 8, &attr, 2, 4, 'x', expct);
        run_pool(ctx, top_vec_pool_max, 4, 8, 2, 4, &attr, TN_DTYPE_FP32, in, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "pool_max K2 s2");
    }

    /* ---- pool_max with padding: (1,1,2,4) K2 stride 1 pad 1 ->
     * (1,1,3,5); edge windows see fewer real cells, never the identity ---- */
    {
        pool_attr_t attr = {1, 1, 1, 1, 1, 1, 2, 2};
        float in[8], out[15], expct[15];

        for (i = 0; i < 8; i++)
            in[i] = (float)(i + 1);
        pool_expect(in, 2, 4, &attr, 3, 5, 'x', expct);
        run_pool(ctx, top_vec_pool_max, 2, 4, 3, 5, &attr, TN_DTYPE_FP32, in, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "pool_max K2 s1 pad1");
    }

    /* ---- pool_min, same padded geometry ---- */
    {
        pool_attr_t attr = {1, 1, 1, 1, 1, 1, 2, 2};
        float in[8], out[15], expct[15];

        for (i = 0; i < 8; i++)
            in[i] = (float)(i + 1);
        pool_expect(in, 2, 4, &attr, 3, 5, 'n', expct);
        run_pool(ctx, top_vec_pool_min, 2, 4, 3, 5, &attr, TN_DTYPE_FP32, in, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "pool_min K2 s1 pad1");
    }

    /* ---- pool_sum: (1,1,2,4) K2 stride 1 -> (1,1,1,3) ---- */
    {
        pool_attr_t attr = {1, 1, 0, 0, 0, 0, 2, 2};
        float in[8], out[3], expct[3];
        static const float V[8] = {0.5f, 1.0f, -1.0f, 2.0f, 1.5f, -0.5f, 0.25f, 3.0f};

        for (i = 0; i < 8; i++)
            in[i] = V[i];
        pool_expect(in, 2, 4, &attr, 1, 3, 's', expct);
        run_pool(ctx, top_vec_pool_sum, 2, 4, 1, 3, &attr, TN_DTYPE_FP32, in, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "pool_sum K2 s1");
    }

    /* ---- pool_sum BF16: (1,1,2,2) K2 -> (1,1,1,1), BF16-exact sum ---- */
    {
        pool_attr_t attr = {1, 1, 0, 0, 0, 0, 2, 2};
        uint16_t in[4], out[1], expct[1];
        static const float V[4] = {0.5f, 1.0f, -1.0f, 2.0f};

        for (i = 0; i < 4; i++)
            in[i] = bf16_of(V[i]);
        expct[0] = bf16_of(2.5f);
        run_pool(ctx, top_vec_pool_sum, 2, 2, 1, 1, &attr, TN_DTYPE_BF16, in, out);
        CHECK(memcmp(out, expct, sizeof(expct)) == 0, "pool_sum BF16");
    }

    /* ---- negatives: violation raised, output keeps its poison pattern ---- */
    check_pool_reject(ctx, "pool stride 0 -> violation", PJ_STRIDE0);
    check_pool_reject(ctx, "pool kernel > padded input -> violation", PJ_KBIG);
    check_pool_reject(ctx, "pool S8 (int) -> violation", PJ_INTIN);
}
