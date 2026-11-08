// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/test/test_vec_reduce.c
 *
 * top/vec reduction + prefix-sum cases: reduce max / min / sum / absmax
 * (W and H axes, integer saturate, FP32) and cumsum (exclusive/inclusive,
 * integer + FP32), plus violation negatives for illegal shapes and
 * layouts. Expectations are computed independently on the test side.
 * ========================================================================== */
#include "smoke.h"

#include <math.h>

typedef void (*reduce_fn)(tdr_idx_t, tdr_idx_t, axis_mode_t);

/* Standard reduce case: input (1,1,4,8) goes through the
 * global->URF->op->global loop; the output shape follows the axis
 * (W: (1,1,4,1), H: (1,1,1,8)) */
static void run_reduce(tnkr_ctx_t *ctx, reduce_fn fn, axis_mode_t axis, tn_dtype_t dt,
                       const void *A, void *OUT) {
    size_t isz = NELEM * tn_dtype_size(dt);
    size_t osz = (axis == AXIS_MODE_W ? 4 : 8) * tn_dtype_size(dt);
    tdr_t d;
    uint8_t sga, sgo, sua, suo;

    memcpy(ctx->global + GA, A, isz);
    memset(ctx->global + GO, 0xAA, osz);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, dt, GA, 1, 1, 4, 8);
    sga = mk_desc(ctx, &d);
    if (axis == AXIS_MODE_W)
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, dt, GO, 1, 1, 4, 1);
    else
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, dt, GO, 1, 1, 1, 8);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, dt, UA, 1, 1, 4, 8, 0);
    sua = mk_desc(ctx, &d);
    if (axis == AXIS_MODE_W)
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, dt, UO, 1, 1, 4, 1, 0);
    else
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, dt, UO, 1, 1, 1, 8, 0);
    suo = mk_desc(ctx, &d);

    top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
    fn(suo, sua, axis);
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(OUT, ctx->global + GO, osz);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgo);
    tdr_free(ctx, sua);
    tdr_free(ctx, suo);
}

static void cumsum_inc(tdr_idx_t o, tdr_idx_t i) { top_vec_cumsum(o, i, CUMSUM_INCLUSIVE); }
static void cumsum_exc(tdr_idx_t o, tdr_idx_t i) { top_vec_cumsum(o, i, CUMSUM_EXCLUSIVE); }

void suite_vec_reduce(tnkr_ctx_t *ctx) {
    int64_t A[NELEM];
    uint32_t i;
    uint32_t h, w;

    printf("== vec_reduce ==\n");

    /* ---- W-axis reduce, S8: max / min / sum / absmax ----
     * input (1,1,4,8) -> (1,1,4,1); row h folds its 8 elements */
    for (i = 0; i < NELEM; i++)
        A[i] = (int64_t)(i * 20) - 300; /* wide spread: sums saturate */
    {
        uint8_t in_a[NELEM], out[4], expct[4];
        int64_t aw[NELEM]; /* width-wrapped values = the in-memory patterns */
        for (i = 0; i < NELEM; i++) {
            aw[i] = A[i] & 0xFF;
            if (aw[i] >= 128)
                aw[i] -= 256;
            in_a[i] = (uint8_t)aw[i];
        }

        for (h = 0; h < 4; h++) {
            int64_t v = aw[h * 8];
            for (w = 1; w < 8; w++)
                if (aw[h * 8 + w] > v)
                    v = aw[h * 8 + w];
            expct[h] = (uint8_t)t_expect_int(v, 8, 1, OVF_MODE_SATURATE);
        }
        run_reduce(ctx, top_vec_reduce_max, AXIS_MODE_W, TN_DTYPE_S8, in_a, out);
        CHECK(memcmp(out, expct, sizeof(out)) == 0, "reduce_max S8 W-axis");

        for (h = 0; h < 4; h++) {
            int64_t v = aw[h * 8];
            for (w = 1; w < 8; w++)
                if (aw[h * 8 + w] < v)
                    v = aw[h * 8 + w];
            expct[h] = (uint8_t)t_expect_int(v, 8, 1, OVF_MODE_SATURATE);
        }
        run_reduce(ctx, top_vec_reduce_min, AXIS_MODE_W, TN_DTYPE_S8, in_a, out);
        CHECK(memcmp(out, expct, sizeof(out)) == 0, "reduce_min S8 W-axis");

        for (h = 0; h < 4; h++) {
            int64_t v = 0;
            for (w = 0; w < 8; w++)
                v += aw[h * 8 + w];
            expct[h] = (uint8_t)t_expect_int(v, 8, 1, OVF_MODE_SATURATE);
        }
        run_reduce(ctx, top_vec_reduce_sum, AXIS_MODE_W, TN_DTYPE_S8, in_a, out);
        CHECK(memcmp(out, expct, sizeof(out)) == 0, "reduce_sum S8 W-axis (saturate)");

        for (h = 0; h < 4; h++) {
            int64_t v = 0;
            for (w = 0; w < 8; w++) {
                int64_t x = aw[h * 8 + w];
                int64_t ax = x < 0 ? -x : x;
                if (ax > v)
                    v = ax;
            }
            expct[h] = (uint8_t)t_expect_int(v, 8, 1, OVF_MODE_SATURATE);
        }
        run_reduce(ctx, top_vec_reduce_absmax, AXIS_MODE_W, TN_DTYPE_S8, in_a, out);
        CHECK(memcmp(out, expct, sizeof(out)) == 0, "reduce_absmax S8 W-axis");
    }

    /* ---- H-axis reduce, S16 sum: (1,1,4,8) -> (1,1,1,8) ---- */
    for (i = 0; i < NELEM; i++)
        A[i] = (int64_t)(i * 90) - 1400;
    {
        uint16_t in_a[NELEM], out[8], expct[8];
        for (i = 0; i < NELEM; i++)
            in_a[i] = (uint16_t)(int16_t)A[i];
        for (w = 0; w < 8; w++) {
            int64_t v = 0;
            for (h = 0; h < 4; h++)
                v += A[h * 8 + w];
            expct[w] = (uint16_t)t_expect_int(v, 16, 1, OVF_MODE_SATURATE);
        }
        run_reduce(ctx, top_vec_reduce_sum, AXIS_MODE_H, TN_DTYPE_S16, in_a, out);
        CHECK(memcmp(out, expct, sizeof(out)) == 0, "reduce_sum S16 H-axis");
    }

    /* ---- FP32: max W-axis, absmax H-axis (negative spread) ---- */
    {
        float fa[NELEM], out[8], fexp[8];
        for (i = 0; i < NELEM; i++)
            fa[i] = (float)((int)(i * 13) - 200) * 0.5f;

        for (h = 0; h < 4; h++) {
            float v = fa[h * 8];
            for (w = 1; w < 8; w++)
                if (fa[h * 8 + w] > v)
                    v = fa[h * 8 + w];
            fexp[h] = v;
        }
        run_reduce(ctx, top_vec_reduce_max, AXIS_MODE_W, TN_DTYPE_FP32, fa, out);
        CHECK(memcmp(out, fexp, 4 * sizeof(float)) == 0, "reduce_max FP32 W-axis");

        for (w = 0; w < 8; w++) {
            float v = 0.0f;
            for (h = 0; h < 4; h++) {
                float x = fabsf(fa[h * 8 + w]);
                if (x > v)
                    v = x;
            }
            fexp[w] = v;
        }
        run_reduce(ctx, top_vec_reduce_absmax, AXIS_MODE_H, TN_DTYPE_FP32, fa, out);
        CHECK(memcmp(out, fexp, 8 * sizeof(float)) == 0, "reduce_absmax FP32 H-axis");
    }

    /* ---- negatives: violation raised, output keeps its poison pattern ---- */

    /* wrong output shape: W-axis reduce with out dw=2 */
    {
        uint8_t in_a[NELEM], out[8], poison[8];
        tdr_t d;
        uint8_t sga, sgo, sua, suo_bad;

        for (i = 0; i < NELEM; i++)
            in_a[i] = (uint8_t)i;
        for (i = 0; i < 8; i++)
            poison[i] = 0xAA;
        memcpy(ctx->global + GA, in_a, sizeof(in_a));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 1, 4, 8);
        sga = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, 1, 4, 2);
        sgo = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, 1, 4, 8, 0);
        sua = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UO, 1, 1, 4, 2, 0);
        suo_bad = mk_desc(ctx, &d);

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
        memset(ctx->urf + UO, 0xAA, 8); /* poison the output plane */
        TNVI_BEGIN("vec_reduce") { top_vec_reduce_max(suo_bad, sua, AXIS_MODE_W); }
        TNVI_END();
        top_ldst_store(sgo, 0, suo_bad, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));
        CHECK(memcmp(out, poison, sizeof(out)) == 0, "reduce out dw!=1 -> violation");

        tdr_free(ctx, sga);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sua);
        tdr_free(ctx, suo_bad);
    }

    /* ZEROSTRIDE input: reductions require HWAlign */
    {
        uint8_t in_a[NELEM], out[4], poison[4];
        tdr_t d;
        uint8_t sga, sgo, sua_ld, sua_zs, suo;

        for (i = 0; i < NELEM; i++)
            in_a[i] = (uint8_t)i;
        for (i = 0; i < 4; i++)
            poison[i] = 0xAA;
        memcpy(ctx->global + GA, in_a, sizeof(in_a));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 1, 4, 8);
        sga = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, 1, 4, 1);
        sgo = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, 1, 4, 8, 0);
        sua_ld = mk_desc(ctx, &d);
        /* same-address ZEROSTRIDE view: N/H broadcast, extent legal */
        d = tdr_make_urf(TDR_LAYOUT_ZEROSTRIDE, TN_DTYPE_S8, UA, 1, 1, 1, 8,
                         TDR_BCAST_NB | TDR_BCAST_HB);
        sua_zs = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UO, 1, 1, 4, 1, 0);
        suo = mk_desc(ctx, &d);

        top_ldst_load(sua_ld, sga, 0, LDST_MODE_NORMAL);
        memset(ctx->urf + UO, 0xAA, 4); /* poison the output plane */
        TNVI_BEGIN("vec_reduce") { top_vec_reduce_max(suo, sua_zs, AXIS_MODE_W); }
        TNVI_END();
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));
        CHECK(memcmp(out, poison, sizeof(out)) == 0, "reduce ZEROSTRIDE input -> violation");

        tdr_free(ctx, sga);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sua_ld);
        tdr_free(ctx, sua_zs);
        tdr_free(ctx, suo);
    }

    /* reserved axis encoding: kernel_types.h reserves 2.. for future axes
     * — an unsupported axis is a violation and must not write (TNVI +
     * poison comparison) */
    {
        uint8_t in_a[NELEM], out[4], poison[4];
        tdr_t d;
        uint8_t sga, sgo, sua, suo;

        for (i = 0; i < NELEM; i++)
            in_a[i] = (uint8_t)i;
        for (i = 0; i < 4; i++)
            poison[i] = 0xAA;
        memcpy(ctx->global + GA, in_a, sizeof(in_a));

        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 1, 4, 8);
        sga = mk_desc(ctx, &d);
        d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GO, 1, 1, 4, 1);
        sgo = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UA, 1, 1, 4, 8, 0);
        sua = mk_desc(ctx, &d);
        d = tdr_make_urf(TDR_LAYOUT_HWALIGN, TN_DTYPE_S8, UO, 1, 1, 4, 1, 0);
        suo = mk_desc(ctx, &d);

        top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
        memset(ctx->urf + UO, 0xAA, 4); /* poison the output plane */
        TNVI_BEGIN("axis must be W or H") { top_vec_reduce_max(suo, sua, (axis_mode_t)2); }
        TNVI_END();
        top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
        memcpy(out, ctx->global + GO, sizeof(out));
        CHECK(memcmp(out, poison, sizeof(out)) == 0,
              "reduce reserved axis 2 -> violation, no write");

        tdr_free(ctx, sga);
        tdr_free(ctx, sgo);
        tdr_free(ctx, sua);
        tdr_free(ctx, suo);
    }

    /* ==================================================================
     * cumsum (W axis, exclusive / inclusive)
     * ================================================================== */
    printf("== vec_cumsum ==\n");

    /* S8 inclusive + exclusive, partial sums saturate */
    for (i = 0; i < NELEM; i++)
        A[i] = (int64_t)(i * 10) - 40;
    {
        uint8_t in_a[NELEM], out[NELEM], expct[NELEM];
        int64_t aw[NELEM]; /* width-wrapped values = the in-memory patterns */
        int64_t acc;
        for (i = 0; i < NELEM; i++) {
            aw[i] = A[i] & 0xFF;
            if (aw[i] >= 128)
                aw[i] -= 256;
            in_a[i] = (uint8_t)aw[i];
        }

        /* per (h): running sum along w (shape (1,1,4,8)) */
        for (h = 0; h < 4; h++) {
            acc = 0;
            for (w = 0; w < 8; w++) {
                acc += aw[h * 8 + w];
                expct[h * 8 + w] = (uint8_t)t_expect_int(acc, 8, 1, OVF_MODE_SATURATE);
            }
        }
        run_unary(ctx, cumsum_inc, TN_DTYPE_S8, TN_DTYPE_S8, in_a, out);
        CHECK(memcmp(out, expct, sizeof(out)) == 0, "cumsum S8 inclusive (saturate)");

        for (h = 0; h < 4; h++) {
            acc = 0;
            for (w = 0; w < 8; w++) {
                expct[h * 8 + w] = (uint8_t)t_expect_int(acc, 8, 1, OVF_MODE_SATURATE);
                acc += aw[h * 8 + w];
            }
        }
        run_unary(ctx, cumsum_exc, TN_DTYPE_S8, TN_DTYPE_S8, in_a, out);
        CHECK(memcmp(out, expct, sizeof(out)) == 0, "cumsum S8 exclusive");
    }

    /* FP32 inclusive via float arithmetic (exact dyadic values) */
    {
        float fa[NELEM], fout[NELEM], fexp[NELEM];
        float acc;
        for (i = 0; i < NELEM; i++)
            fa[i] = (float)((int)i - 16) * 0.25f;
        for (h = 0; h < 4; h++) {
            acc = 0.0f;
            for (w = 0; w < 8; w++) {
                acc += fa[h * 8 + w];
                fexp[h * 8 + w] = acc;
            }
        }
        run_unary(ctx, cumsum_inc, TN_DTYPE_FP32, TN_DTYPE_FP32, fa, fout);
        CHECK(memcmp(fout, fexp, sizeof(fout)) == 0, "cumsum FP32 inclusive");
    }
}
