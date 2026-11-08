// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/mm.cpp
 *
 * top/mm engine instructions (ref backend) — files are split by kernel.h
 * engine:
 *   matmul: fp family (BF16 x BF16 -> FP32), modes nnn / ntn / ntt
 *           bq family (FP8/FP4 x FP8/FP4, UE8M0/UE5M3 block scales ->
 *           FP32), modes ntn / ntt
 *   conv:   fp family (FP8/BF16 input x matching kernel -> BF16/FP32),
 *           WAlign kernel (1,OC,KH*KW,IC), see top_mm_conv_fp below
 *
 * Modes (kernel.h): nnn/ntn/ntt = per-operand transpose of
 * (left, right, output); operand shapes are all HWAlign:
 *   nnn: left (1,M,1,K), right (1,K,1,N), output (1,M,1,N)
 *   ntn: left (1,M,1,K), right (1,N,1,K), output (1,M,1,N)
 *   ntt: left (1,M,1,K), right (1,N,1,K), output (1,N,1,M)
 *
 * Numeric semantics (golden model):
 *   fp family: each output element is one K-reduction accumulated in
 *   FP32 (the golden model keeps the tail in double), with either the
 *   per-channel bias or (exclusively) the old output value added
 *   (kernel.h: the two flags are mutually exclusive; "apply bias first,
 *   then accumulate" is the recommended multi-call usage). bias exists
 *   only under PER_CHANNEL: with BIAS_MODE_NONE the bias index is
 *   ignored and need not name a configured TDR. The result
 *   is encoded to the output dtype (saturate + RNE, never inf): a BF16
 *   output is the FP32 accumulation cast at write-out, and ACCUMULATE
 *   confines the output to FP32 (kernel.h). The chip's MAC order and
 *   intermediate rounding are chip-backend tolerance questions.
 *   bq family (per the spec pseudocode): the K range is
 *   split into g_size blocks; each block is first reduced WITHOUT its
 *   scales (block dot product), then scaled once and accumulated:
 *     FP8/FP4 + UE8M0:  scale_b = 2^(ls_b + rs_b - 127 - 127)   (exact)
 *     FP4    + UE5M3 :  scale_b = cast(ls_b * rs_b, FP32)
 *                        — the product is taken in the UE5M3 domain
 *                        (tnkr_encode_fp to UE5M3, RNE) before widening;
 *                        per-operand decode-then-multiply is NOT
 *                        equivalent (mantissa rounding differs)
 *     acc += scale_b * blk_b   (block-granularity scaling)
 *   Scale decode (UE8M0/UE5M3) is the ref-model interpretation documented
 *   in dtype.h; scale pairing: FP8 -> UE8M0, FP4 -> UE8M0 (mxfp4) or
 *   UE5M3 (nxfp4), per operand.
 * ========================================================================== */
#include "dtype.h"
#include "kernel/common/context_internal.h"
#include "kernel/kernel.h"
#include "tensor.h"
#include "violate.h"

namespace {

typedef enum { MM_NNN, MM_NTN, MM_NTT } mm_mode_t;

/* group_size_t (kernel_types.h): GROUP_SIZE_16 -> 16, GROUP_SIZE_32 -> 32 */
uint32_t mm_group_width(group_size_t gs) { return (gs == GROUP_SIZE_32) ? 32u : 16u; }

bool scale_dtype_ok(tn_dtype_t dt) { return dt == TN_DTYPE_UE8M0 || dt == TN_DTYPE_UE5M3; }

void mm_matmul(const char *op, mm_mode_t mode, bool bq, tdr_idx_t out_i, tdr_idx_t l_i,
               tdr_idx_t r_i, tdr_idx_t bias_i, tdr_idx_t ls_i, tdr_idx_t rs_i, group_size_t g_size,
               acc_mode_t acc, bias_mode_t bias_mode) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, l, r, b, ls, rs;
    uint32_t m, k, n, c, w, kk;
    uint32_t group = 0; /* block-quant group width (bq only) */

    TNKR_REF_RESOLVE(ctx, &out, out_i, 0, op, "out");
    TNKR_REF_RESOLVE(ctx, &l, l_i, 0, op, "left");
    TNKR_REF_RESOLVE(ctx, &r, r_i, 0, op, "right");
    if (bq) {
        TNKR_REF_RESOLVE(ctx, &ls, ls_i, 0, op, "left_scale");
        TNKR_REF_RESOLVE(ctx, &rs, rs_i, 0, op, "right_scale");
    }
    TNKR_REF_CHECK(op, bias_mode == BIAS_MODE_NONE || bias_mode == BIAS_MODE_PER_CHANNEL,
                   "bias mode must be NONE or PER_CHANNEL");
    TNKR_REF_CHECK(op, acc == ACC_MODE_OVERWRITE || acc == ACC_MODE_ACCUMULATE,
                   "acc mode must be OVERWRITE or ACCUMULATE");
    /* bias exists only under PER_CHANNEL: with NONE the bias index is
     * ignored and need not name a configured TDR */
    if (bias_mode == BIAS_MODE_PER_CHANNEL)
        TNKR_REF_RESOLVE(ctx, &b, bias_i, 0, op, "bias");

    /* kernel.h: operand shapes (all HWAlign) */
    TNKR_REF_CHECK(op,
                   out.layout == TDR_LAYOUT_HWALIGN && l.layout == TDR_LAYOUT_HWALIGN &&
                       r.layout == TDR_LAYOUT_HWALIGN,
                   "operands must be HWAlign");
    if (bq)
        TNKR_REF_CHECK(op, ls.layout == TDR_LAYOUT_HWALIGN && rs.layout == TDR_LAYOUT_HWALIGN,
                       "scale operands must be HWAlign");

    /* family dtype rules */
    if (bq) {
        TNKR_REF_CHECK(op, l.dtype == r.dtype, "left/right dtype mismatch");
        TNKR_REF_CHECK(op, l.dtype == TN_DTYPE_FP8_E4M3 || l.dtype == TN_DTYPE_FP4,
                       "data dtype must be FP8_E4M3 or FP4");
        if (l.dtype == TN_DTYPE_FP8_E4M3) {
            TNKR_REF_CHECK(op, ls.dtype == TN_DTYPE_UE8M0 && rs.dtype == TN_DTYPE_UE8M0,
                           "FP8 requires UE8M0 scales on both sides");
        } else {
            TNKR_REF_CHECK(op, scale_dtype_ok(ls.dtype) && scale_dtype_ok(rs.dtype),
                           "FP4 scales must be UE8M0 (mxfp4) or UE5M3 (nxfp4)");
            TNKR_REF_CHECK(op, ls.dtype == rs.dtype,
                           "FP4 scales must use the same dtype on both sides");
        }
    } else {
        TNKR_REF_CHECK(op, l.dtype == TN_DTYPE_BF16 && r.dtype == TN_DTYPE_BF16,
                       "fp family requires BF16 left/right");
    }
    TNKR_REF_CHECK(op, out.dtype == TN_DTYPE_FP32 || out.dtype == TN_DTYPE_BF16,
                   "output dtype must be BF16/FP32");
    TNKR_REF_CHECK(op, acc != ACC_MODE_ACCUMULATE || out.dtype == TN_DTYPE_FP32,
                   "ACCUMULATE restricts the output to FP32");

    /* matmul operands are (1,X,1,Y) */
    TNKR_REF_CHECK(op,
                   out.dn == 1 && out.dh == 1 && l.dn == 1 && l.dh == 1 && r.dn == 1 && r.dh == 1,
                   "operands must be (1,X,1,Y)");

    m = l.dc;
    k = l.dw;
    if (mode == MM_NNN) {
        TNKR_REF_CHECK(op, r.dc == k && out.dc == m && out.dw == r.dw, "shape mismatch (nnn)");
        n = r.dw;
    } else if (mode == MM_NTN) {
        TNKR_REF_CHECK(op, r.dw == k && out.dc == m && out.dw == r.dc, "shape mismatch (ntn)");
        n = r.dc;
    } else {
        TNKR_REF_CHECK(op, r.dw == k && out.dc == r.dc && out.dw == m, "shape mismatch (ntt)");
        n = r.dc;
    }

    if (bias_mode == BIAS_MODE_PER_CHANNEL) {
        TNKR_REF_CHECK(op, b.layout == TDR_LAYOUT_HWALIGN && b.dn == 1 && b.dh == 1,
                       "bias must be HWAlign (1,C,1,W)");
        TNKR_REF_CHECK(op, b.dtype == TN_DTYPE_BF16 || b.dtype == TN_DTYPE_FP32,
                       "bias dtype must be BF16/FP32");
        if (mode == MM_NTT) {
            TNKR_REF_CHECK(op, b.dc == out.dc && b.dw == 1, "ntt bias must be (1,N,1,1)");
        } else {
            TNKR_REF_CHECK(op, b.dc == ctx->info.smc_num && b.dw == out.dw,
                           "nnn/ntn bias must be (1,SMC_NUM,1,N)");
        }
    }
    TNKR_REF_CHECK(op, acc != ACC_MODE_ACCUMULATE || bias_mode != BIAS_MODE_PER_CHANNEL,
                   "ACCUMULATE and PER_CHANNEL bias are mutually exclusive");

    if (bq) {
        group = mm_group_width(g_size);
        TNKR_REF_CHECK(op, k % group == 0,
                       "K must be a multiple of the group width (scale extent is K//g)");
        TNKR_REF_CHECK(op, ls.dn == 1 && ls.dh == 1 && ls.dc == m && ls.dw == k / group,
                       "left_scale shape must be (1,M,1,K//g)");
        TNKR_REF_CHECK(op, rs.dn == 1 && rs.dh == 1 && rs.dc == n && rs.dw == k / group,
                       "right_scale shape must be (1,N,1,K//g)");
    }

    /* fp family treats the whole K range as one block (no scales); bq
     * family: g_size-wide blocks, per-block dot product then one scale
     * multiply (spec pseudocode, block-granularity scaling) */
    const uint32_t nblk = bq ? k / group : 1u;
    const uint32_t blkw = bq ? group : k;

    for (c = 0; c < out.dc; c++)
        for (w = 0; w < out.dw; w++) {
            /* output element (0,c,0,w) is (m,n) = ... per mode */
            uint32_t om = (mode == MM_NTT) ? w : c;
            uint32_t on = (mode == MM_NTT) ? c : w;
            double acc_d = 0.0;
            uint32_t blk;

            for (blk = 0; blk < nblk; blk++) {
                double sum = 0.0;
                for (kk = blk * blkw; kk < (blk + 1) * blkw;
                     kk++) { /* block reduction, no scales */
                    double lv = tnkr_decode_fp(tnkr_view_read(&l, 0, om, 0, kk), l.dtype);
                    double rv;
                    if (mode == MM_NNN)
                        rv = tnkr_decode_fp(tnkr_view_read(&r, 0, kk, 0, on), r.dtype);
                    else
                        rv = tnkr_decode_fp(tnkr_view_read(&r, 0, on, 0, kk), r.dtype);
                    sum += lv * rv;
                }
                if (bq) {
                    double sl = tnkr_decode_fp(tnkr_view_read(&ls, 0, om, 0, blk), ls.dtype);
                    double sr = tnkr_decode_fp(tnkr_view_read(&rs, 0, on, 0, blk), rs.dtype);
                    double sc = sl * sr;
                    if (ls.dtype == TN_DTYPE_UE5M3)
                        sc = tnkr_decode_fp(tnkr_encode_fp(sc, TN_DTYPE_UE5M3), TN_DTYPE_UE5M3);
                    acc_d += sc * sum;
                } else {
                    acc_d += sum;
                }
            }
            if (bias_mode == BIAS_MODE_PER_CHANNEL) {
                uint64_t bb = (mode == MM_NTT) ? tnkr_view_read(&b, 0, c, 0, 0)
                                               : tnkr_view_read(&b, 0, c % ctx->info.smc_num, 0, w);
                acc_d += tnkr_decode_fp(bb, b.dtype);
            }
            if (acc == ACC_MODE_ACCUMULATE)
                acc_d += tnkr_decode_fp(tnkr_view_read(&out, 0, c, 0, w), out.dtype);
            tnkr_view_write(&out, 0, c, 0, w, tnkr_encode_fp(acc_d, out.dtype));
        }
}

} // anonymous namespace

extern "C" void top_mm_matmul_fp_nnn(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right,
                                     tdr_idx_t bias, acc_mode_t acc, bias_mode_t bias_mode) {
    mm_matmul("matmul.fp.nnn", MM_NNN, false, output, left, right, bias, 0, 0, GROUP_SIZE_32, acc,
              bias_mode);
}

extern "C" void top_mm_matmul_fp_ntn(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right,
                                     tdr_idx_t bias, acc_mode_t acc, bias_mode_t bias_mode) {
    mm_matmul("matmul.fp.ntn", MM_NTN, false, output, left, right, bias, 0, 0, GROUP_SIZE_32, acc,
              bias_mode);
}

extern "C" void top_mm_matmul_fp_ntt(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right,
                                     tdr_idx_t bias, acc_mode_t acc, bias_mode_t bias_mode) {
    mm_matmul("matmul.fp.ntt", MM_NTT, false, output, left, right, bias, 0, 0, GROUP_SIZE_32, acc,
              bias_mode);
}

extern "C" void top_mm_matmul_bq_ntn(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right,
                                     tdr_idx_t bias, tdr_idx_t left_scale, tdr_idx_t right_scale,
                                     group_size_t g_size, acc_mode_t acc, bias_mode_t bias_mode) {
    mm_matmul("matmul.bq.ntn", MM_NTN, true, output, left, right, bias, left_scale, right_scale,
              g_size, acc, bias_mode);
}

extern "C" void top_mm_matmul_bq_ntt(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right,
                                     tdr_idx_t bias, tdr_idx_t left_scale, tdr_idx_t right_scale,
                                     group_size_t g_size, acc_mode_t acc, bias_mode_t bias_mode) {
    mm_matmul("matmul.bq.ntt", MM_NTT, true, output, left, right, bias, left_scale, right_scale,
              g_size, acc, bias_mode);
}

/* ==========================================================================
 * conv (fp family)
 *   input (N,IC,H,W) HWAlign, kernel (1,OC,KH*KW,IC) WAlign,
 *   output (N,OC,OH,OW) HWAlign, bias (1,OC,1,1) HWAlign
 * Numeric semantics (golden model):
 *   each output element reduces K = KH*KW*IC in FP32 (the golden model
 *   keeps the tail in double). Coordinates are derived in the
 *   insert-expanded input space: a tap contributes only when its
 *   expanded coordinate is on a non-hole original input element and in
 *   bounds — padding cells and insert holes contribute zero. The
 *   per-channel bias is added after the reduction; the result is
 *   encoded to the output dtype (saturate + RNE, never inf). The chip's
 *   MAC order and intermediate rounding are chip-backend tolerance
 *   questions.
 * ========================================================================== */
extern "C" void top_mm_conv_fp(tdr_idx_t output, tdr_idx_t input, tdr_idx_t kernel, tdr_idx_t bias,
                               const conv_attr_t *attr, bias_mode_t bias_mode) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, in, k, b;
    uint32_t n, oc, oh, ow, kh, kw, ic;
    uint32_t h_ins, w_ins, kheff, kweff, oh_n, ow_n;
    int64_t oh_d, ow_d, ih_ins, iw_ins, ih, iw;
    int32_t ins1;

    TNKR_REF_CHECK("conv.fp", attr != nullptr, "attr pointer is NULL");
    TNKR_REF_RESOLVE(ctx, &out, output, 0, "conv.fp", "out");
    TNKR_REF_RESOLVE(ctx, &in, input, 0, "conv.fp", "in");
    TNKR_REF_RESOLVE(ctx, &k, kernel, 0, "conv.fp", "kernel");
    TNKR_REF_CHECK("conv.fp", bias_mode == BIAS_MODE_NONE || bias_mode == BIAS_MODE_PER_CHANNEL,
                   "bias mode must be NONE or PER_CHANNEL");
    /* bias exists only under PER_CHANNEL: with NONE the bias index is
     * ignored and need not name a configured TDR */
    if (bias_mode == BIAS_MODE_PER_CHANNEL)
        TNKR_REF_RESOLVE(ctx, &b, bias, 0, "conv.fp", "bias");

    /* kernel.h: layout rules */
    TNKR_REF_CHECK("conv.fp",
                   out.layout == TDR_LAYOUT_HWALIGN && in.layout == TDR_LAYOUT_HWALIGN &&
                       k.layout == TDR_LAYOUT_WALIGN,
                   "layouts: in/out HWAlign, kernel WAlign");

    /* family dtype rules */
    TNKR_REF_CHECK("conv.fp", in.dtype == k.dtype, "input/kernel dtype mismatch");
    TNKR_REF_CHECK("conv.fp", in.dtype == TN_DTYPE_BF16 || in.dtype == TN_DTYPE_FP8_E4M3,
                   "input dtype must be BF16 or FP8_E4M3");
    TNKR_REF_CHECK("conv.fp", out.dtype == TN_DTYPE_FP32 || out.dtype == TN_DTYPE_BF16,
                   "output dtype must be BF16/FP32");

    /* geometry limits (kernel.h): stride<=15, pad/insert<=3, dilation 1..3 */
    TNKR_REF_CHECK("conv.fp",
                   attr->stride_w != 0 && attr->stride_w <= 15 && attr->stride_h != 0 &&
                       attr->stride_h <= 15,
                   "stride must be 1..15");
    TNKR_REF_CHECK("conv.fp",
                   attr->pad_t <= 3 && attr->pad_b <= 3 && attr->pad_l <= 3 && attr->pad_r <= 3,
                   "pad must be 0..3");
    TNKR_REF_CHECK("conv.fp", attr->insert <= 3, "insert must be 0..3");
    TNKR_REF_CHECK("conv.fp", attr->dilation != 0 && attr->dilation <= 3, "dilation must be 1..3");

    /* operand shapes: kernel (1,OC,KH*KW,IC), output (N,OC,OH,OW);
     * kernel size limit (kernel.h): kernel_h/kernel_w <= 15 */
    TNKR_REF_CHECK("conv.fp",
                   attr->kernel_w != 0 && attr->kernel_w <= 15 && attr->kernel_h != 0 &&
                       attr->kernel_h <= 15,
                   "kernel_h/kernel_w must be 1..15");
    TNKR_REF_CHECK("conv.fp", (uint32_t)k.dh == (uint32_t)attr->kernel_w * (uint32_t)attr->kernel_h,
                   "kernel.dh must equal kernel_h*kernel_w");
    TNKR_REF_CHECK("conv.fp", k.dn == 1 && k.dw == in.dc && out.dn == in.dn && out.dc == k.dc,
                   "operand shapes: kernel (1,OC,KH*KW,IC), output (N,OC,OH,OW)");

    if (bias_mode == BIAS_MODE_PER_CHANNEL) {
        TNKR_REF_CHECK("conv.fp", b.layout == TDR_LAYOUT_HWALIGN, "bias must be HWAlign");
        TNKR_REF_CHECK("conv.fp", b.dtype == TN_DTYPE_BF16 || b.dtype == TN_DTYPE_FP32,
                       "bias dtype must be BF16/FP32");
        TNKR_REF_CHECK("conv.fp", b.dn == 1 && b.dh == 1 && b.dw == 1 && b.dc == out.dc,
                       "bias shape must be (1,OC,1,1)");
    }

    /* output shape derivation (floor) */
    ins1 = (int32_t)attr->insert + 1;
    h_ins = ((uint32_t)in.dh - 1u) * (uint32_t)ins1 + 1u;
    w_ins = ((uint32_t)in.dw - 1u) * (uint32_t)ins1 + 1u;
    kheff = ((uint32_t)attr->kernel_h - 1u) * (uint32_t)attr->dilation + 1u;
    kweff = ((uint32_t)attr->kernel_w - 1u) * (uint32_t)attr->dilation + 1u;
    oh_d = (int64_t)h_ins + attr->pad_t + attr->pad_b - (int64_t)kheff;
    ow_d = (int64_t)w_ins + attr->pad_l + attr->pad_r - (int64_t)kweff;
    TNKR_REF_CHECK("conv.fp", oh_d >= 0 && ow_d >= 0, "no complete window fits the input");
    oh_n = (uint32_t)oh_d / attr->stride_h + 1u;
    ow_n = (uint32_t)ow_d / attr->stride_w + 1u;
    TNKR_REF_CHECK("conv.fp", out.dh == oh_n && out.dw == ow_n,
                   "output shape must match the derived OH/OW");

    for (n = 0; n < out.dn; n++)
        for (oc = 0; oc < out.dc; oc++)
            for (oh = 0; oh < out.dh; oh++)
                for (ow = 0; ow < out.dw; ow++) {
                    double acc = 0.0;

                    for (kh = 0; kh < attr->kernel_h; kh++)
                        for (kw = 0; kw < attr->kernel_w; kw++)
                            for (ic = 0; ic < in.dc; ic++) {
                                double lv, rv;
                                ih_ins = (int64_t)oh * attr->stride_h - attr->pad_t +
                                         (int64_t)kh * attr->dilation;
                                iw_ins = (int64_t)ow * attr->stride_w - attr->pad_l +
                                         (int64_t)kw * attr->dilation;
                                if (ih_ins % ins1 != 0 || iw_ins % ins1 != 0)
                                    continue; /* insert hole contributes zero */
                                ih = ih_ins / ins1;
                                iw = iw_ins / ins1;
                                if (ih < 0 || ih >= (int64_t)in.dh || iw < 0 ||
                                    iw >= (int64_t)in.dw)
                                    continue; /* padding contributes zero */
                                lv = tnkr_decode_fp(
                                    tnkr_view_read(&in, n, ic, (uint32_t)ih, (uint32_t)iw),
                                    in.dtype);
                                rv = tnkr_decode_fp(
                                    tnkr_view_read(&k, 0, oc, kh * attr->kernel_w + kw, ic),
                                    k.dtype);
                                acc += lv * rv;
                            }
                    if (bias_mode == BIAS_MODE_PER_CHANNEL)
                        acc += tnkr_decode_fp(tnkr_view_read(&b, 0, oc, 0, 0), b.dtype);
                    tnkr_view_write(&out, n, oc, oh, ow, tnkr_encode_fp(acc, out.dtype));
                }
}
