// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/vec.cpp
 *
 * top/vec engine instructions (ref backend) — files are split by kernel.h
 * engine:
 *   data movement    : move
 *   elementwise arith: add / sub / mul / div / mac / max / min
 *   unary arith      : square / abs / floor
 *   logic            : and / or / xor / nxor / not
 *   shifts           : logic/arith/rotate left/right shift
 *   reductions       : max / min / sum / absmax, prefix sum (cumsum)
 *   compare          : cmp gt/le/eq/ge, cmp_select (csel)
 *   special functions: normal / getmant / rsqrt / taylor / reciprocal
 *   cast & quant     : cast, fp8/mxfp4/nxfp4 quant, fp8/int8 dequant
 *   conv             : FP32 convolution, unreorganized (IC,OC,KH,KW) kernel
 *   depthwise        : FP8/BF16 -> BF16/FP32, per-channel KH*KW chain
 *   pool             : max / min / sum
 *   matmul           : nnn (FP32; strict FP32-step accumulation)
 *
 * The arith family is eaten by one binop driver: walk the output shape,
 * read both sides' bit patterns via TensorView -> decode -> compute in
 * double (FP) / __int128 (INT) -> encode in the output dtype and write
 * back. Per-instruction differences collapse into vop_sem_t entries.
 *
 * Numeric semantics:
 *   FP : fixed saturate (clamp to ±max, never produces inf) + RNE — the
 *        inf from div-by-zero is likewise clamped to ±max_finite
 *   INT: ovf 0=saturate 1=truncate; max/min/div output the input dtype,
 *        add/sub/mul output width >= input (U subtraction may output S
 *        or U)
 * ========================================================================== */
#include "dtype.h"
#include "kernel/common/context_internal.h"
#include "kernel/kernel.h"
#include "kernel/ref/rcp_lut.h" /* rcp layout constants (single source of truth) */
#include "tensor.h"
#include "violate.h"

namespace {

typedef enum { VOP_ADD, VOP_SUB, VOP_MUL, VOP_DIV, VOP_MAX, VOP_MIN, VOP_MAC } vop_t;

typedef struct {
    vop_t op;
    bool fp_ok;       /* floating-point input allowed */
    bool int_ok;      /* integer input allowed */
    bool same_dtype;  /* output dtype must equal input (max/min/div) */
    bool fp_widen;    /* FP output restricted to BF16/FP32 (mul/mac) */
    bool rconst_only; /* Constant operand allowed on right (opd1) only */
} vop_sem_t;

const vop_sem_t SEM_ADD = {VOP_ADD, true, true, false, false, false};
const vop_sem_t SEM_SUB = {VOP_SUB, true, true, false, false, false};
const vop_sem_t SEM_MUL = {VOP_MUL, true, true, false, true, true};
const vop_sem_t SEM_DIV = {VOP_DIV, true, false, true, false, false};
const vop_sem_t SEM_MAX = {VOP_MAX, true, true, true, false, true};
const vop_sem_t SEM_MIN = {VOP_MIN, true, true, true, false, true};
const vop_sem_t SEM_MAC = {VOP_MAC, true, false, false, true, true}; /* FP only (kernel.h) */

bool in_layout_ok(tdr_layout_t ly) {
    return ly == TDR_LAYOUT_HWALIGN || ly == TDR_LAYOUT_ZEROSTRIDE || ly == TDR_LAYOUT_CONSTANT;
}

void vec_binop(const char *op, tdr_idx_t out_i, tdr_idx_t l_i, tdr_idx_t r_i, const vop_sem_t *sem,
               ovf_mode_t ovf) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, l, r;
    uint32_t n, c, h, w;
    bool fp;

    TNKR_REF_RESOLVE(ctx, &out, out_i, 0, op, "out");
    TNKR_REF_RESOLVE(ctx, &l, l_i, 0, op, "left");
    TNKR_REF_RESOLVE(ctx, &r, r_i, 0, op, "right");
    TNKR_REF_CHECK(op, out.layout == TDR_LAYOUT_HWALIGN, "output must be HWAlign");
    TNKR_REF_CHECK(op, in_layout_ok(l.layout) && in_layout_ok(r.layout),
                   "inputs must be ZeroStride/HWAlign/Constant");
    TNKR_REF_CHECK(op, !sem->rconst_only || l.layout != TDR_LAYOUT_CONSTANT,
                   "left may not be Constant for this op (kernel.h)");
    TNKR_REF_CHECK(op, !(l.layout == TDR_LAYOUT_CONSTANT && r.layout == TDR_LAYOUT_CONSTANT),
                   "inputs may not both be Constant");
    TNKR_REF_CHECK(op, sem->op != VOP_DIV || r.layout != TDR_LAYOUT_CONSTANT,
                   "div right operand may not be Constant (kernel.h)");
    TNKR_REF_CHECK(op, l.dtype == r.dtype, "left/right dtype mismatch");
    fp = tnkr_dt_is_fp(l.dtype);
    TNKR_REF_CHECK(op, fp ? sem->fp_ok : sem->int_ok, "dtype not allowed for this op");
    if (fp)
        TNKR_REF_CHECK(op,
                       !sem->fp_widen || (out.dtype == TN_DTYPE_BF16 || out.dtype == TN_DTYPE_FP32),
                       "FP output must be BF16/FP32 (width never narrows)");
    TNKR_REF_CHECK(op, !sem->same_dtype || out.dtype == l.dtype,
                   "output dtype must equal the input dtype");
    TNKR_REF_CHECK(op, out.dtype_bits >= l.dtype_bits, "output width never narrows");

    for (n = 0; n < out.dn; n++)
        for (c = 0; c < out.dc; c++)
            for (h = 0; h < out.dh; h++)
                for (w = 0; w < out.dw; w++) {
                    uint64_t lb = tnkr_view_read(&l, n, c, h, w);
                    uint64_t rb = tnkr_view_read(&r, n, c, h, w);
                    uint64_t ob;
                    if (fp) {
                        double a = tnkr_decode_fp(lb, l.dtype);
                        double b = tnkr_decode_fp(rb, r.dtype);
                        double v;
                        switch (sem->op) {
                        case VOP_ADD:
                            v = a + b;
                            break;
                        case VOP_SUB:
                            v = a - b;
                            break;
                        case VOP_MUL:
                            v = a * b;
                            break;
                        case VOP_DIV:
                            v = a / b;
                            break;
                        case VOP_MAX:
                            v = (a > b) ? a : b;
                            break;
                        case VOP_MIN:
                            v = (a < b) ? a : b;
                            break;
                        default: /* VOP_MAC: out gains the product */
                            v = a * b + tnkr_decode_fp(tnkr_view_read(&out, n, c, h, w), out.dtype);
                            break;
                        }
                        ob = tnkr_encode_fp(v, out.dtype);
                    } else {
                        __int128 a = tnkr_decode_int(lb, l.dtype);
                        __int128 b = tnkr_decode_int(rb, r.dtype);
                        __int128 v;
                        switch (sem->op) {
                        case VOP_ADD:
                            v = a + b;
                            break;
                        case VOP_SUB:
                            v = a - b;
                            break;
                        case VOP_MUL:
                            v = a * b;
                            break;
                        case VOP_MAX:
                            v = (a > b) ? a : b;
                            break;
                        case VOP_MIN:
                            v = (a < b) ? a : b;
                            break;
                        default: /* VOP_MAC: out gains the product */
                            v = a * b +
                                tnkr_decode_int(tnkr_view_read(&out, n, c, h, w), out.dtype);
                            break;
                        }
                        ob = tnkr_encode_int(v, out.dtype, ovf);
                    }
                    tnkr_view_write(&out, n, c, h, w, ob);
                }
}

/* ---- logic driver ----
 * Pure bit-pattern ops: no numeric decode — signedness is irrelevant to
 * bitwise results, and the write masks to the dtype width.
 * Integers only; output dtype == input dtype. */
typedef enum { BOP_AND, BOP_OR, BOP_XOR, BOP_NXOR } bop_t;

uint64_t bitop_u64(bop_t op, uint64_t a, uint64_t b) {
    switch (op) {
    case BOP_AND:
        return a & b;
    case BOP_OR:
        return a | b;
    case BOP_XOR:
        return a ^ b;
    default:
        return ~(a ^ b); /* NXOR */
    }
}

void vec_bitop(const char *op, tdr_idx_t out_i, tdr_idx_t l_i, tdr_idx_t r_i, bop_t bit) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, l, r;
    uint32_t n, c, h, w;

    TNKR_REF_RESOLVE(ctx, &out, out_i, 0, op, "out");
    TNKR_REF_RESOLVE(ctx, &l, l_i, 0, op, "left");
    TNKR_REF_RESOLVE(ctx, &r, r_i, 0, op, "right");
    TNKR_REF_CHECK(op, out.layout == TDR_LAYOUT_HWALIGN, "output must be HWAlign");
    TNKR_REF_CHECK(op, in_layout_ok(l.layout) && in_layout_ok(r.layout),
                   "inputs must be ZeroStride/HWAlign/Constant");
    TNKR_REF_CHECK(op, l.layout != TDR_LAYOUT_CONSTANT, "left may not be Constant (kernel.h)");
    TNKR_REF_CHECK(op, l.dtype == r.dtype && out.dtype == l.dtype,
                   "dtypes must match (output = input)");
    TNKR_REF_CHECK(op, !tnkr_dt_is_fp(l.dtype), "logic ops are integers only");

    for (n = 0; n < out.dn; n++)
        for (c = 0; c < out.dc; c++)
            for (h = 0; h < out.dh; h++)
                for (w = 0; w < out.dw; w++)
                    tnkr_view_write(&out, n, c, h, w,
                                    bitop_u64(bit, tnkr_view_read(&l, n, c, h, w),
                                              tnkr_view_read(&r, n, c, h, w)));
}

/* ---- shift driver ----
 * Bit-pattern shifts on the raw dtype-width value; the amount is an
 * unsigned count and must be < input width (kernel.h legality — the
 * whole instruction violates if any amount is out of range, so amounts
 * are validated in a first pass before any write).
 * mode==CONSTANT takes the uint5 immediate; mode==TENSOR reads per-element
 * amounts from the shift operand (any integer dtype, layout
 * ZeroStride/HWAlign — Constant is not supported on opd1). Logic right
 * zero-fills, arith right sign-fills, rotate is circular. */
typedef enum { SH_LSL, SH_LSR, SH_ASR, SH_ROL, SH_ROR } shop_t;

uint64_t shift_u64(shop_t shop, uint64_t a, uint32_t s, uint32_t bits) {
    uint64_t mask = (1ULL << bits) - 1ULL;

    a &= mask;
    if (s == 0)
        return a;
    switch (shop) {
    case SH_LSL:
        return (a << s) & mask;
    case SH_LSR:
        return a >> s;
    case SH_ASR:
        if (a & (1ULL << (bits - 1)))
            return (a >> s) | ((mask << (bits - s)) & mask);
        return a >> s;
    case SH_ROL:
        return ((a << s) | (a >> (bits - s))) & mask;
    default: /* SH_ROR */
        return ((a >> s) | (a << (bits - s))) & mask;
    }
}

void vec_shift(const char *op, tdr_idx_t out_i, tdr_idx_t in_i, tdr_idx_t sh_i, uint8_t imm,
               shift_mode_t mode, shop_t shop) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, in, sh;
    uint32_t n, c, h, w, bits;

    TNKR_REF_RESOLVE(ctx, &out, out_i, 0, op, "out");
    TNKR_REF_RESOLVE(ctx, &in, in_i, 0, op, "in");
    TNKR_REF_CHECK(op, out.layout == TDR_LAYOUT_HWALIGN, "output must be HWAlign");
    TNKR_REF_CHECK(op, in.layout == TDR_LAYOUT_HWALIGN || in.layout == TDR_LAYOUT_ZEROSTRIDE,
                   "input must be ZeroStride/HWAlign (not Constant)");
    TNKR_REF_CHECK(op, out.dtype == in.dtype && !tnkr_dt_is_fp(in.dtype),
                   "integers only; output dtype == input dtype");
    bits = in.dtype_bits;
    if (mode == CONSTANT) {
        TNKR_REF_CHECK(op, imm < bits, "shift amount immediate >= input width");
    } else {
        TNKR_REF_RESOLVE(ctx, &sh, sh_i, 0, op, "shift");
        TNKR_REF_CHECK(op, sh.layout == TDR_LAYOUT_HWALIGN || sh.layout == TDR_LAYOUT_ZEROSTRIDE,
                       "shift operand layout not allowed (ZeroStride/HWAlign only)");
        TNKR_REF_CHECK(op, !tnkr_dt_is_fp(sh.dtype), "shift amounts are integers");
        /* validate every amount before writing anything */
        for (n = 0; n < out.dn; n++)
            for (c = 0; c < out.dc; c++)
                for (h = 0; h < out.dh; h++)
                    for (w = 0; w < out.dw; w++)
                        TNKR_REF_CHECK(op, tnkr_view_read(&sh, n, c, h, w) < bits,
                                       "shift amount >= input width");
    }

    for (n = 0; n < out.dn; n++)
        for (c = 0; c < out.dc; c++)
            for (h = 0; h < out.dh; h++)
                for (w = 0; w < out.dw; w++) {
                    uint32_t s =
                        (mode == CONSTANT) ? imm : (uint32_t)tnkr_view_read(&sh, n, c, h, w);
                    tnkr_view_write(&out, n, c, h, w,
                                    shift_u64(shop, tnkr_view_read(&in, n, c, h, w), s, bits));
                }
}

/* ---- unary arith driver ----
 * Decode -> compute in double (FP) / __int128 (INT) -> encode in the
 * output dtype, same numeric semantics as the binop driver.
 * Per-op dtype rules (kernel.h):
 *   square: FP in, BF16/FP32 out (width never narrows)
 *   abs   : signed-int / FP in, out = in
 *   floor : FP in, out = in */
typedef enum { UOP_SQUARE, UOP_ABS, UOP_FLOOR } uop_t;

void vec_unop(const char *op, tdr_idx_t out_i, tdr_idx_t in_i, uop_t uop) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, in;
    uint32_t n, c, h, w;
    bool fp;

    TNKR_REF_RESOLVE(ctx, &out, out_i, 0, op, "out");
    TNKR_REF_RESOLVE(ctx, &in, in_i, 0, op, "in");
    TNKR_REF_CHECK(op, out.layout == TDR_LAYOUT_HWALIGN && in.layout == TDR_LAYOUT_HWALIGN,
                   "operands must be HWAlign");
    fp = tnkr_dt_is_fp(in.dtype);
    switch (uop) {
    case UOP_SQUARE:
        TNKR_REF_CHECK(op,
                       fp && (out.dtype == TN_DTYPE_BF16 || out.dtype == TN_DTYPE_FP32) &&
                           out.dtype_bits >= in.dtype_bits,
                       "square: FP input, BF16/FP32 output, width never narrows");
        break;
    case UOP_ABS:
        TNKR_REF_CHECK(op, (fp || tnkr_dt_is_signed(in.dtype)) && out.dtype == in.dtype,
                       "abs: signed-int/FP input, output = input dtype (unsigned rejected)");
        break;
    default: /* UOP_FLOOR */
        TNKR_REF_CHECK(op, fp && out.dtype == in.dtype, "floor: FP input, output = input dtype");
        break;
    }

    for (n = 0; n < out.dn; n++)
        for (c = 0; c < out.dc; c++)
            for (h = 0; h < out.dh; h++)
                for (w = 0; w < out.dw; w++) {
                    uint64_t ib = tnkr_view_read(&in, n, c, h, w), ob;
                    if (fp) {
                        double a = tnkr_decode_fp(ib, in.dtype), v;
                        if (uop == UOP_SQUARE)
                            v = a * a;
                        else if (uop == UOP_ABS)
                            v = fabs(a);
                        else
                            v = floor(a);
                        ob = tnkr_encode_fp(v, out.dtype);
                    } else {
                        __int128 a = tnkr_decode_int(ib, in.dtype);
                        ob = tnkr_encode_int(a < 0 ? -a : a, out.dtype, OVF_MODE_SATURATE);
                    }
                    tnkr_view_write(&out, n, c, h, w, ob);
                }
}

/* ---- reduction driver ----
 * Reduce one axis (W or H) to extent 1; the value set is decoded once and
 * folded in double (FP) / __int128 (INT, sum saturates at encode). */
typedef enum { ROP_MAX, ROP_MIN, ROP_SUM, ROP_ABSMAX } rop_t;

void vec_reduce(const char *op, tdr_idx_t out_i, tdr_idx_t in_i, axis_mode_t axis, rop_t rop) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, in;
    uint32_t n, c, h, w, k, len;
    bool fp;

    TNKR_REF_RESOLVE(ctx, &out, out_i, 0, op, "out");
    TNKR_REF_RESOLVE(ctx, &in, in_i, 0, op, "in");
    /* kernel_types.h reserves axis encodings 2.. for future axes */
    TNKR_REF_CHECK(op, axis == AXIS_MODE_W || axis == AXIS_MODE_H, "reduce axis must be W or H");
    TNKR_REF_CHECK(op, out.layout == TDR_LAYOUT_HWALIGN && in.layout == TDR_LAYOUT_HWALIGN,
                   "operands must be HWAlign");
    TNKR_REF_CHECK(op, out.dtype == in.dtype, "output dtype must equal the input dtype");
    TNKR_REF_CHECK(op, out.dn == in.dn && out.dc == in.dc, "output N/C must match the input");
    if (axis == AXIS_MODE_W) {
        TNKR_REF_CHECK(op, out.dh == in.dh && out.dw == 1, "W reduce: output must be (N,C,H,1)");
        len = in.dw;
    } else {
        TNKR_REF_CHECK(op, out.dh == 1 && out.dw == in.dw, "H reduce: output must be (N,C,1,W)");
        len = in.dh;
    }
    fp = tnkr_dt_is_fp(in.dtype);

    for (n = 0; n < out.dn; n++)
        for (c = 0; c < out.dc; c++)
            for (h = 0; h < out.dh; h++)
                for (w = 0; w < out.dw; w++) {
                    uint64_t ib;
                    if (fp) {
                        double acc = 0.0, x;
                        for (k = 0; k < len; k++) {
                            ib = (axis == AXIS_MODE_W) ? tnkr_view_read(&in, n, c, h, k)
                                                       : tnkr_view_read(&in, n, c, k, w);
                            x = tnkr_decode_fp(ib, in.dtype);
                            if (rop == ROP_ABSMAX)
                                x = fabs(x);
                            if (k == 0)
                                acc = x;
                            else if (rop == ROP_SUM)
                                acc += x;
                            else if (rop == ROP_MAX || rop == ROP_ABSMAX)
                                acc = (x > acc) ? x : acc;
                            else
                                acc = (x < acc) ? x : acc;
                        }
                        tnkr_view_write(&out, n, c, h, w, tnkr_encode_fp(acc, out.dtype));
                    } else {
                        __int128 acc = 0, x;
                        for (k = 0; k < len; k++) {
                            ib = (axis == AXIS_MODE_W) ? tnkr_view_read(&in, n, c, h, k)
                                                       : tnkr_view_read(&in, n, c, k, w);
                            x = tnkr_decode_int(ib, in.dtype);
                            if (rop == ROP_ABSMAX && x < 0)
                                x = -x;
                            if (k == 0)
                                acc = x;
                            else if (rop == ROP_SUM)
                                acc += x;
                            else if (rop == ROP_MAX || rop == ROP_ABSMAX)
                                acc = (x > acc) ? x : acc;
                            else
                                acc = (x < acc) ? x : acc;
                        }
                        tnkr_view_write(&out, n, c, h, w,
                                        tnkr_encode_int(acc, out.dtype, OVF_MODE_SATURATE));
                    }
                }
}

/* ---- compare helpers (shared by cmp and csel) ---- */
typedef enum { COP_GT, COP_LT, COP_LE, COP_EQ, COP_GE } cop_t;

bool cmp_eval_fp(cop_t op, double a, double b) {
    switch (op) {
    case COP_GT:
        return a > b;
    case COP_LT:
        return a < b;
    case COP_LE:
        return a <= b;
    case COP_EQ:
        return a == b;
    default:
        return a >= b;
    }
}

bool cmp_eval_int(cop_t op, __int128 a, __int128 b) {
    switch (op) {
    case COP_GT:
        return a > b;
    case COP_LT:
        return a < b;
    case COP_LE:
        return a <= b;
    case COP_EQ:
        return a == b;
    default:
        return a >= b;
    }
}

} // anonymous namespace

/* ---- data movement ----
 * top_vec_move (top.vec.mv): bit copy inside URF, never crosses SMC; the
 * only instruction that allows FREE layout on URF (kernel.h top/ldst
 * section); dtype must match, shapes pair per source layout
 * (tnkr_view_copy_shape_ok: exact equality, broadcast-read dims may
 * expand, CONSTANT accepts any shape) */
extern "C" void top_vec_move(tdr_idx_t output, tdr_idx_t out_str, tdr_idx_t input,
                             tdr_idx_t in_str) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, in;

    TNKR_REF_RESOLVE(ctx, &out, output, out_str, "vec_move", "out");
    TNKR_REF_RESOLVE(ctx, &in, input, in_str, "vec_move", "in");
    TNKR_REF_CHECK("vec_move",
                   tdr_get_loc(out.desc) == TDR_LOC_URF && tdr_get_loc(in.desc) == TDR_LOC_URF,
                   "both operands must live in URF");
    TNKR_REF_CHECK("vec_move", out.layout == TDR_LAYOUT_HWALIGN || out.layout == TDR_LAYOUT_FREE,
                   "output layout must be HWAlign or FREE");
    TNKR_REF_CHECK("vec_move",
                   in.layout == TDR_LAYOUT_HWALIGN || in.layout == TDR_LAYOUT_ZEROSTRIDE ||
                       in.layout == TDR_LAYOUT_FREE || in.layout == TDR_LAYOUT_CONSTANT,
                   "input layout must be HWAlign/ZeroStride/FREE/Constant");
    /* shape pairing per source layout: exact equality, except broadcast
     * reads (ZEROSTRIDE bcast dims / FREE stride-0 dims) which may
     * expand, and CONSTANT which accepts any shape — rules in
     * tensor.h tnkr_view_copy_shape_ok */
    TNKR_REF_CHECK("vec_move", out.dtype == in.dtype && tnkr_view_copy_shape_ok(&out, &in, false),
                   "dtype must match and dst/src shapes must pair legally");
    tnkr_view_copy(&out, &in, false);
}

/* ---- elementwise arithmetic ---- */

extern "C" void top_vec_add(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right, ovf_mode_t mode) {
    vec_binop("vec_add", output, left, right, &SEM_ADD, mode);
}

extern "C" void top_vec_sub(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right, ovf_mode_t mode) {
    vec_binop("vec_sub", output, left, right, &SEM_SUB, mode);
}

extern "C" void top_vec_mul(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right, ovf_mode_t mode) {
    vec_binop("vec_mul", output, left, right, &SEM_MUL, mode);
}

/* mac: out = out + left * right (the output doubles as the accumulator);
 * FP only, so there is no ovf mode (kernel.h) */
extern "C" void top_vec_mac(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right) {
    vec_binop("vec_mac", output, left, right, &SEM_MAC, OVF_MODE_SATURATE);
}

extern "C" void top_vec_div(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right) {
    vec_binop("vec_div", output, left, right, &SEM_DIV, OVF_MODE_SATURATE);
}

extern "C" void top_vec_max(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right) {
    vec_binop("vec_max", output, left, right, &SEM_MAX, OVF_MODE_SATURATE);
}

extern "C" void top_vec_min(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right) {
    vec_binop("vec_min", output, left, right, &SEM_MIN, OVF_MODE_SATURATE);
}

/* ---- logic ----
 * kernel.h: integers only; left/right ZeroStride/HWAlign/Constant
 * (Constant on right only), output HWAlign = input dtype */

extern "C" void top_vec_and(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right) {
    vec_bitop("vec_and", output, left, right, BOP_AND);
}

extern "C" void top_vec_or(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right) {
    vec_bitop("vec_or", output, left, right, BOP_OR);
}

extern "C" void top_vec_xor(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right) {
    vec_bitop("vec_xor", output, left, right, BOP_XOR);
}

extern "C" void top_vec_nxor(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right) {
    vec_bitop("vec_nxor", output, left, right, BOP_NXOR);
}

/* top_vec_not: unary; input/output both HWAlign (kernel.h) */
extern "C" void top_vec_not(tdr_idx_t output, tdr_idx_t input) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, in;
    uint32_t n, c, h, w;

    TNKR_REF_RESOLVE(ctx, &out, output, 0, "vec_not", "out");
    TNKR_REF_RESOLVE(ctx, &in, input, 0, "vec_not", "in");
    TNKR_REF_CHECK("vec_not", out.layout == TDR_LAYOUT_HWALIGN && in.layout == TDR_LAYOUT_HWALIGN,
                   "operands must be HWAlign");
    TNKR_REF_CHECK("vec_not", out.dtype == in.dtype && !tnkr_dt_is_fp(in.dtype),
                   "integers only; output dtype == input dtype");

    for (n = 0; n < out.dn; n++)
        for (c = 0; c < out.dc; c++)
            for (h = 0; h < out.dh; h++)
                for (w = 0; w < out.dw; w++)
                    tnkr_view_write(&out, n, c, h, w, ~tnkr_view_read(&in, n, c, h, w));
}

/* ---- shifts ----
 * kernel.h: input ZeroStride/HWAlign integers; shift tensor any integer
 * dtype (ZeroStride/HWAlign/Constant) or a uint5 immediate via
 * mode=CONSTANT; amount < input width; output HWAlign = input dtype */

extern "C" void top_vec_logic_left_shift(tdr_idx_t output, tdr_idx_t input, tdr_idx_t shift,
                                         uint8_t shift_imm, shift_mode_t mode) {
    vec_shift("vec_shift.lsl", output, input, shift, shift_imm, mode, SH_LSL);
}

extern "C" void top_vec_logic_right_shift(tdr_idx_t output, tdr_idx_t input, tdr_idx_t shift,
                                          uint8_t shift_imm, shift_mode_t mode) {
    vec_shift("vec_shift.lsr", output, input, shift, shift_imm, mode, SH_LSR);
}

extern "C" void top_vec_rotate_left_shift(tdr_idx_t output, tdr_idx_t input, tdr_idx_t shift,
                                          uint8_t shift_imm, shift_mode_t mode) {
    vec_shift("vec_shift.rol", output, input, shift, shift_imm, mode, SH_ROL);
}

extern "C" void top_vec_rotate_right_shift(tdr_idx_t output, tdr_idx_t input, tdr_idx_t shift,
                                           uint8_t shift_imm, shift_mode_t mode) {
    vec_shift("vec_shift.ror", output, input, shift, shift_imm, mode, SH_ROR);
}

extern "C" void top_vec_arith_right_shift(tdr_idx_t output, tdr_idx_t input, tdr_idx_t shift,
                                          uint8_t shift_imm, shift_mode_t mode) {
    vec_shift("vec_shift.asr", output, input, shift, shift_imm, mode, SH_ASR);
}

/* ---- unary arithmetic ----
 * kernel.h: input/output both HWAlign; square FP in / BF16+FP32 out,
 * abs signed-int+FP out=in, floor FP out=in */

extern "C" void top_vec_square(tdr_idx_t output, tdr_idx_t input) {
    vec_unop("vec_square", output, input, UOP_SQUARE);
}

extern "C" void top_vec_abs(tdr_idx_t output, tdr_idx_t input) {
    vec_unop("vec_abs", output, input, UOP_ABS);
}

extern "C" void top_vec_floor(tdr_idx_t output, tdr_idx_t input) {
    vec_unop("vec_floor", output, input, UOP_FLOOR);
}

/* ---- reductions ----
 * kernel.h: input HWAlign integers+FP; output HWAlign, input dtype,
 * reduced axis (W or H) collapses to extent 1 */

extern "C" void top_vec_reduce_max(tdr_idx_t output, tdr_idx_t input, axis_mode_t axis) {
    vec_reduce("vec_reduce.max", output, input, axis, ROP_MAX);
}

extern "C" void top_vec_reduce_min(tdr_idx_t output, tdr_idx_t input, axis_mode_t axis) {
    vec_reduce("vec_reduce.min", output, input, axis, ROP_MIN);
}

extern "C" void top_vec_reduce_sum(tdr_idx_t output, tdr_idx_t input, axis_mode_t axis) {
    vec_reduce("vec_reduce.sum", output, input, axis, ROP_SUM);
}

extern "C" void top_vec_reduce_absmax(tdr_idx_t output, tdr_idx_t input, axis_mode_t axis) {
    vec_reduce("vec_reduce.absmax", output, input, axis, ROP_ABSMAX);
}

/* ---- prefix sum ----
 * kernel.h: W axis only; input/output HWAlign, integers+FP, out = in
 * dtype. INT accumulates in __int128 and saturates per element at encode;
 * FP accumulates in double with per-element RNE encode. */
extern "C" void top_vec_cumsum(tdr_idx_t output, tdr_idx_t input, cumsum_include_t include) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, in;
    uint32_t n, c, h, w;
    bool fp;

    TNKR_REF_RESOLVE(ctx, &out, output, 0, "vec_cumsum", "out");
    TNKR_REF_RESOLVE(ctx, &in, input, 0, "vec_cumsum", "in");
    TNKR_REF_CHECK("vec_cumsum",
                   out.layout == TDR_LAYOUT_HWALIGN && in.layout == TDR_LAYOUT_HWALIGN,
                   "operands must be HWAlign");
    TNKR_REF_CHECK("vec_cumsum", out.dtype == in.dtype && tnkr_view_same_shape(&out, &in),
                   "dtypes must match and output shape must equal the input shape");
    fp = tnkr_dt_is_fp(in.dtype);

    for (n = 0; n < out.dn; n++)
        for (c = 0; c < out.dc; c++)
            for (h = 0; h < out.dh; h++) {
                if (fp) {
                    double acc = 0.0;
                    for (w = 0; w < out.dw; w++) {
                        double x = tnkr_decode_fp(tnkr_view_read(&in, n, c, h, w), in.dtype);
                        if (include == CUMSUM_INCLUSIVE)
                            acc += x;
                        tnkr_view_write(&out, n, c, h, w, tnkr_encode_fp(acc, out.dtype));
                        if (include == CUMSUM_EXCLUSIVE)
                            acc += x;
                    }
                } else {
                    __int128 acc = 0;
                    for (w = 0; w < out.dw; w++) {
                        __int128 x = tnkr_decode_int(tnkr_view_read(&in, n, c, h, w), in.dtype);
                        if (include == CUMSUM_INCLUSIVE)
                            acc += x;
                        tnkr_view_write(&out, n, c, h, w,
                                        tnkr_encode_int(acc, out.dtype, OVF_MODE_SATURATE));
                        if (include == CUMSUM_EXCLUSIVE)
                            acc += x;
                    }
                }
            }
}

/* ---- compare ----
 * kernel.h: left/right integers+FP, same dtype, HWAlign/ZeroStride/
 * Constant (Constant on right only); output HWAlign = input dtype, 0/1 (int)
 * or 0.0/1.0 (FP) */
static void vec_cmp(const char *op, tdr_idx_t out_i, tdr_idx_t l_i, tdr_idx_t r_i, cop_t cop) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, l, r;
    uint32_t n, c, h, w;
    bool fp;

    TNKR_REF_RESOLVE(ctx, &out, out_i, 0, op, "out");
    TNKR_REF_RESOLVE(ctx, &l, l_i, 0, op, "left");
    TNKR_REF_RESOLVE(ctx, &r, r_i, 0, op, "right");
    TNKR_REF_CHECK(op, out.layout == TDR_LAYOUT_HWALIGN, "output must be HWAlign");
    TNKR_REF_CHECK(op, in_layout_ok(l.layout) && in_layout_ok(r.layout),
                   "inputs must be ZeroStride/HWAlign/Constant");
    TNKR_REF_CHECK(op, l.layout != TDR_LAYOUT_CONSTANT, "left may not be Constant (kernel.h)");
    TNKR_REF_CHECK(op, l.dtype == r.dtype && out.dtype == l.dtype,
                   "dtypes must match (output = input)");
    fp = tnkr_dt_is_fp(l.dtype);

    for (n = 0; n < out.dn; n++)
        for (c = 0; c < out.dc; c++)
            for (h = 0; h < out.dh; h++)
                for (w = 0; w < out.dw; w++) {
                    uint64_t lb = tnkr_view_read(&l, n, c, h, w);
                    uint64_t rb = tnkr_view_read(&r, n, c, h, w);
                    bool v;
                    if (fp) {
                        v = cmp_eval_fp(cop, tnkr_decode_fp(lb, l.dtype),
                                        tnkr_decode_fp(rb, r.dtype));
                        tnkr_view_write(&out, n, c, h, w, tnkr_encode_fp(v ? 1.0 : 0.0, out.dtype));
                    } else {
                        v = cmp_eval_int(cop, tnkr_decode_int(lb, l.dtype),
                                         tnkr_decode_int(rb, r.dtype));
                        tnkr_view_write(&out, n, c, h, w, v ? 1u : 0u);
                    }
                }
}

extern "C" void top_vec_cmp_gt(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right) {
    vec_cmp("vec_cmp.gt", output, left, right, COP_GT);
}

extern "C" void top_vec_cmp_lt(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right) {
    vec_cmp("vec_cmp.lt", output, left, right, COP_LT);
}

extern "C" void top_vec_cmp_le(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right) {
    vec_cmp("vec_cmp.le", output, left, right, COP_LE);
}

extern "C" void top_vec_cmp_eq(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right) {
    vec_cmp("vec_cmp.eq", output, left, right, COP_EQ);
}

extern "C" void top_vec_cmp_ge(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right) {
    vec_cmp("vec_cmp.ge", output, left, right, COP_GE);
}

/* ---- compare-select ----
 * kernel.h: res0 = left OP right ? left_result : right_result;
 * left/right HWAlign/ZeroStride/Constant (Constant on right only),
 * left_result/right_result HWAlign/ZeroStride; output HWAlign, input
 * dtype. The selected operand's bit pattern is copied verbatim. */
static void vec_csel(const char *op, tdr_idx_t out_i, tdr_idx_t l_i, tdr_idx_t r_i, tdr_idx_t lr_i,
                     tdr_idx_t rr_i, cop_t cop) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, l, r, lr, rr;
    uint32_t n, c, h, w;
    bool fp;

    TNKR_REF_RESOLVE(ctx, &out, out_i, 0, op, "out");
    TNKR_REF_RESOLVE(ctx, &l, l_i, 0, op, "left");
    TNKR_REF_RESOLVE(ctx, &r, r_i, 0, op, "right");
    TNKR_REF_RESOLVE(ctx, &lr, lr_i, 0, op, "left_result");
    TNKR_REF_RESOLVE(ctx, &rr, rr_i, 0, op, "right_result");
    TNKR_REF_CHECK(op, out.layout == TDR_LAYOUT_HWALIGN, "output must be HWAlign");
    TNKR_REF_CHECK(op, in_layout_ok(l.layout) && in_layout_ok(r.layout),
                   "inputs must be ZeroStride/HWAlign/Constant");
    TNKR_REF_CHECK(op, l.layout != TDR_LAYOUT_CONSTANT, "left may not be Constant (kernel.h)");
    TNKR_REF_CHECK(op, lr.layout != TDR_LAYOUT_CONSTANT && rr.layout != TDR_LAYOUT_CONSTANT,
                   "results may not be Constant");
    TNKR_REF_CHECK(op, in_layout_ok(lr.layout) && in_layout_ok(rr.layout),
                   "results must be ZeroStride/HWAlign");
    TNKR_REF_CHECK(op,
                   l.dtype == r.dtype && lr.dtype == l.dtype && rr.dtype == l.dtype &&
                       out.dtype == l.dtype,
                   "all dtypes must match (output = input)");
    fp = tnkr_dt_is_fp(l.dtype);

    for (n = 0; n < out.dn; n++)
        for (c = 0; c < out.dc; c++)
            for (h = 0; h < out.dh; h++)
                for (w = 0; w < out.dw; w++) {
                    uint64_t lb = tnkr_view_read(&l, n, c, h, w);
                    uint64_t rb = tnkr_view_read(&r, n, c, h, w);
                    bool take_l;
                    if (fp)
                        take_l = cmp_eval_fp(cop, tnkr_decode_fp(lb, l.dtype),
                                             tnkr_decode_fp(rb, r.dtype));
                    else
                        take_l = cmp_eval_int(cop, tnkr_decode_int(lb, l.dtype),
                                              tnkr_decode_int(rb, r.dtype));
                    tnkr_view_write(&out, n, c, h, w,
                                    take_l ? tnkr_view_read(&lr, n, c, h, w)
                                           : tnkr_view_read(&rr, n, c, h, w));
                }
}

extern "C" void top_vec_cmp_select_gt(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right,
                                      tdr_idx_t left_result, tdr_idx_t right_result) {
    vec_csel("vec_csel.gt", output, left, right, left_result, right_result, COP_GT);
}

extern "C" void top_vec_cmp_select_lt(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right,
                                      tdr_idx_t left_result, tdr_idx_t right_result) {
    vec_csel("vec_csel.lt", output, left, right, left_result, right_result, COP_LT);
}

extern "C" void top_vec_cmp_select_le(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right,
                                      tdr_idx_t left_result, tdr_idx_t right_result) {
    vec_csel("vec_csel.le", output, left, right, left_result, right_result, COP_LE);
}

extern "C" void top_vec_cmp_select_eq(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right,
                                      tdr_idx_t left_result, tdr_idx_t right_result) {
    vec_csel("vec_csel.eq", output, left, right, left_result, right_result, COP_EQ);
}

extern "C" void top_vec_cmp_select_ge(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right,
                                      tdr_idx_t left_result, tdr_idx_t right_result) {
    vec_csel("vec_csel.ge", output, left, right, left_result, right_result, COP_GE);
}

/* ---- special functions ----
 * kernel.h: input/output HWAlign, BF16/FP32 (normal/getmant also FP8) */

/* normal/getmant: pure bit-field extraction, no numeric decode;
 * zero/subnormal/inf/nan -> 0 */
static void vec_fp_extract(const char *op, tdr_idx_t out_i, tdr_idx_t in_i, bool is_normal) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, in;
    const tnkr_fp_fmt_t *f;
    uint32_t n, c, h, w;

    TNKR_REF_RESOLVE(ctx, &out, out_i, 0, op, "out");
    TNKR_REF_RESOLVE(ctx, &in, in_i, 0, op, "in");
    TNKR_REF_CHECK(op, out.layout == TDR_LAYOUT_HWALIGN && in.layout == TDR_LAYOUT_HWALIGN,
                   "operands must be HWAlign");
    f = tnkr_fp_fmt(in.dtype);
    TNKR_REF_CHECK(op, f != NULL && in.dtype != TN_DTYPE_FP4,
                   "input dtype must be FP8_E4M3 / BF16 / FP32");
    if (is_normal) {
        TNKR_REF_CHECK(op, out.dtype == TN_DTYPE_S8,
                       "normal: output dtype is fixed S8 (exponent - bias)");
    } else {
        TNKR_REF_CHECK(op,
                       (in.dtype == TN_DTYPE_FP8_E4M3 && out.dtype == TN_DTYPE_U8) ||
                           (in.dtype == TN_DTYPE_BF16 && out.dtype == TN_DTYPE_U16) ||
                           (in.dtype == TN_DTYPE_FP32 && out.dtype == TN_DTYPE_U32),
                       "getmant: output dtype follows the input width (U8/U16/U32)");
    }

    for (n = 0; n < out.dn; n++)
        for (c = 0; c < out.dc; c++)
            for (h = 0; h < out.dh; h++)
                for (w = 0; w < out.dw; w++) {
                    uint64_t bits = tnkr_view_read(&in, n, c, h, w);
                    uint64_t mag = bits & (((uint64_t)1 << (f->e_bits + f->m_bits)) - 1u);
                    uint64_t ef = (bits >> f->m_bits) & ((1u << f->e_bits) - 1u);
                    uint64_t m = bits & ((1u << f->m_bits) - 1u);
                    uint64_t exp_max = (1u << f->e_bits) - 1u;
                    uint64_t res = 0;

                    if (ef == 0)
                        res = 0; /* zero / subnormal */
                    else if (!f->exp_ones_finite && ef == exp_max)
                        res = 0; /* inf / nan */
                    else if (f->exp_ones_finite && mag == f->nan_pat)
                        res = 0; /* FP8 NaN (no inf encoding) */
                    else if (is_normal)
                        res = tnkr_encode_int((int)ef - f->bias, TN_DTYPE_S8, OVF_MODE_SATURATE);
                    else
                        res = ((uint64_t)1 << f->m_bits) | m; /* restore hidden 1 */
                    tnkr_view_write(&out, n, c, h, w, res);
                }
}

extern "C" void top_vec_normal(tdr_idx_t output, tdr_idx_t input) {
    vec_fp_extract("vec_normal", output, input, true);
}

extern "C" void top_vec_getmant(tdr_idx_t output, tdr_idx_t input) {
    vec_fp_extract("vec_getmant", output, input, false);
}

/* rsqrt: the ref computes the mathematically exact value (same golden
 * model policy as div); kernel.h's magic-guess + Newton iteration counts
 * describe the chip's approximation strategy. Documented specials:
 * negative -> NaN, zero -> +-inf (written as a raw pattern: the encode
 * path saturates and never emits inf) */
extern "C" void top_vec_rsqrt(tdr_idx_t output, tdr_idx_t input) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, in;
    const tnkr_fp_fmt_t *f;
    uint32_t n, c, h, w;

    TNKR_REF_RESOLVE(ctx, &out, output, 0, "vec_rsqrt", "out");
    TNKR_REF_RESOLVE(ctx, &in, input, 0, "vec_rsqrt", "in");
    TNKR_REF_CHECK("vec_rsqrt", out.layout == TDR_LAYOUT_HWALIGN && in.layout == TDR_LAYOUT_HWALIGN,
                   "operands must be HWAlign");
    TNKR_REF_CHECK("vec_rsqrt", in.dtype == TN_DTYPE_BF16 || in.dtype == TN_DTYPE_FP32,
                   "input dtype must be BF16/FP32");
    TNKR_REF_CHECK("vec_rsqrt", out.dtype == in.dtype, "output dtype must equal the input dtype");
    f = tnkr_fp_fmt(in.dtype);

    for (n = 0; n < out.dn; n++)
        for (c = 0; c < out.dc; c++)
            for (h = 0; h < out.dh; h++)
                for (w = 0; w < out.dw; w++) {
                    double a = tnkr_decode_fp(tnkr_view_read(&in, n, c, h, w), in.dtype);
                    uint64_t ob;
                    if (a == 0.0) {
                        uint64_t sgn = signbit(a) ? (uint64_t)1 << (f->e_bits + f->m_bits) : 0;
                        ob = sgn | (((uint64_t)1 << f->e_bits) - 1u) << f->m_bits; /* +-inf */
                    } else if (isinf(a)) {
                        ob = 0; /* rsqrt(+inf) = +0 */
                    } else if (a < 0.0) {
                        ob = f->nan_pat; /* negative -> NaN */
                    } else {
                        ob = tnkr_encode_fp(1.0 / sqrt(a), out.dtype);
                    }
                    tnkr_view_write(&out, n, c, h, w, ob);
                }
}

/* taylor: y = sum_k coeff[lane(c), k] * x^k, Horner evaluation in double;
 * coeff is the table (1, SMC_NUM, 1, step), input dtype — one authored
 * (1,1,1,step) table C-broadcast at load time, so every lane holds an
 * identical copy and table reads wrap at c % SMC_NUM (same convention
 * as the reciprocal LUT; see kernel.h) */
extern "C" void top_vec_taylor(tdr_idx_t output, tdr_idx_t input, tdr_idx_t coeff) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, in, cf;
    uint32_t n, c, h, w;

    TNKR_REF_RESOLVE(ctx, &out, output, 0, "vec_taylor", "out");
    TNKR_REF_RESOLVE(ctx, &in, input, 0, "vec_taylor", "in");
    TNKR_REF_RESOLVE(ctx, &cf, coeff, 0, "vec_taylor", "coeff");
    TNKR_REF_CHECK("vec_taylor",
                   out.layout == TDR_LAYOUT_HWALIGN && in.layout == TDR_LAYOUT_HWALIGN &&
                       cf.layout == TDR_LAYOUT_HWALIGN,
                   "operands must be HWAlign");
    TNKR_REF_CHECK("vec_taylor", in.dtype == TN_DTYPE_BF16 || in.dtype == TN_DTYPE_FP32,
                   "input dtype must be BF16/FP32");
    TNKR_REF_CHECK("vec_taylor", out.dtype == in.dtype && cf.dtype == in.dtype,
                   "output/coeff dtype must equal the input dtype");
    TNKR_REF_CHECK("vec_taylor",
                   cf.dn == 1 && cf.dh == 1 && cf.dc == ctx->info.smc_num && cf.dw != 0,
                   "coeff shape must be (1, SMC_NUM, 1, step) with step >= 1");

    for (n = 0; n < out.dn; n++)
        for (c = 0; c < out.dc; c++)
            for (h = 0; h < out.dh; h++)
                for (w = 0; w < out.dw; w++) {
                    const uint32_t cf_c = c % ctx->info.smc_num;
                    double x = tnkr_decode_fp(tnkr_view_read(&in, n, c, h, w), in.dtype);
                    double y = 0.0;
                    int k;
                    for (k = (int)cf.dw - 1; k >= 0; k--)
                        y = y * x +
                            tnkr_decode_fp(tnkr_view_read(&cf, 0, cf_c, 0, (uint32_t)k), cf.dtype);
                    tnkr_view_write(&out, n, c, h, w, tnkr_encode_fp(y, out.dtype));
                }
}

/* ---- reciprocal ----
 * LUT + segment-local quadratic interpolation, run in int64 fixed point.
 * Unlike rsqrt there is no simpler exact form to substitute, so the ref
 * executes the algorithm itself (bit-exact against the table contents):
 *   x = +-2^(E-bias) * (1+t); the top SEG bits of the mantissa index three
 *   S32 Q2.30 sub-tables (A|B|C), the low bits xl give u = xl/2^(m-SEG) and
 *   F = A + B*u + C*u^2 ~ 1/(1+t). Then 1/x = +-2^(2*bias-1-E-bias) * 2F,
 *   the mantissa is cut TNKR_RCP_Q-bit -> m-bit with round-half-up and the
 *   rounding carry bumps the exponent. The chip has no subnormal
 *   encodings: subnormal inputs (E==0, mantissa != 0) are flushed to
 *   signed zero before the operation (-> +-inf), subnormal results
 *   (E_out <= 0) flush to signed zero. NaN -> canonical NaN.
 *
 * Layout constants (TNKR_RCP_SEG_BITS / TNKR_RCP_SEGS / TNKR_RCP_Q /
 * TNKR_RCP_TBL_WORDS) come from rcp_lut.h -- same definitions the generated
 * table and every table-loading caller use. */

/* floor-precise arithmetic right shift (int64; the C >> on negative values
 * is implementation-defined before C23) */
static int64_t rcp_sar(int64_t v, unsigned sh) {
    if (v >= 0)
        return v >> sh;
    return -(((-v) + ((int64_t)1 << sh) - 1) >> sh);
}

extern "C" void top_vec_reciprocal(tdr_idx_t output, tdr_idx_t input, tdr_idx_t table) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, in, tb;
    const tnkr_fp_fmt_t *f;
    uint64_t infb;
    uint32_t n, c, h, w;

    TNKR_REF_RESOLVE(ctx, &out, output, 0, "vec_rcp", "out");
    TNKR_REF_RESOLVE(ctx, &in, input, 0, "vec_rcp", "in");
    TNKR_REF_RESOLVE(ctx, &tb, table, 0, "vec_rcp", "table");
    TNKR_REF_CHECK("vec_rcp",
                   out.layout == TDR_LAYOUT_HWALIGN && in.layout == TDR_LAYOUT_HWALIGN &&
                       tb.layout == TDR_LAYOUT_HWALIGN,
                   "operands must be HWAlign");
    TNKR_REF_CHECK("vec_rcp", in.dtype == TN_DTYPE_BF16 || in.dtype == TN_DTYPE_FP32,
                   "input dtype must be BF16/FP32");
    TNKR_REF_CHECK("vec_rcp", out.dtype == in.dtype && tb.dtype == TN_DTYPE_S32,
                   "output dtype = input dtype, table dtype must be S32");
    TNKR_REF_CHECK("vec_rcp",
                   out.dn == in.dn && out.dc == in.dc && out.dh == in.dh && out.dw == in.dw,
                   "output shape must equal the input shape");
    TNKR_REF_CHECK("vec_rcp",
                   tb.dn == 1 && tb.dh == 1 && tb.dc == ctx->info.smc_num &&
                       tb.dw == TNKR_RCP_TBL_WORDS,
                   "table shape must be (1, SMC_NUM, 1, words)");
    f = tnkr_fp_fmt(in.dtype);
    infb = ((1ull << f->e_bits) - 1ull) << f->m_bits;

    for (n = 0; n < out.dn; n++)
        for (c = 0; c < out.dc; c++)
            for (h = 0; h < out.dh; h++)
                for (w = 0; w < out.dw; w++) {
                    const uint32_t tb_c = c % ctx->info.smc_num;
                    uint64_t ib = tnkr_view_read(&in, n, c, h, w);
                    uint64_t sign = (ib >> (f->e_bits + f->m_bits)) & 1;
                    uint64_t ef = (ib >> f->m_bits) & ((1u << f->e_bits) - 1u);
                    uint64_t m = ib & ((1u << f->m_bits) - 1u);
                    uint64_t ob;

                    if (ef == 0) {
                        /* +-0 and (flushed) subnormal input -> +-inf */
                        ob = (sign << (f->e_bits + f->m_bits)) | infb;
                    } else if (ef == (uint64_t)((1u << f->e_bits) - 1u)) {
                        ob = (m == 0) ? (sign << (f->e_bits + f->m_bits)) : f->nan_pat;
                    } else {
                        uint32_t idx = (uint32_t)(m >> ((int)f->m_bits - (int)TNKR_RCP_SEG_BITS));
                        int xsh = (int)f->m_bits - (int)TNKR_RCP_SEG_BITS;
                        int64_t A = (int32_t)tnkr_view_read(&tb, 0, tb_c, 0, idx);
                        int64_t B = (int32_t)tnkr_view_read(&tb, 0, tb_c, 0, idx + TNKR_RCP_SEGS);
                        int64_t C =
                            (int32_t)tnkr_view_read(&tb, 0, tb_c, 0, idx + 2u * TNKR_RCP_SEGS);
                        int64_t F = A, mant;
                        int eout;

                        if (xsh > 0) {
                            /* FP32: low mantissa bits carry the interpolant
                             * (BF16 has none -> pure table lookup) */
                            int64_t xl = (int64_t)(m & ((1u << xsh) - 1u));
                            F += rcp_sar(B * xl, (unsigned)xsh) +
                                 rcp_sar(C * xl * xl, (unsigned)(2 * xsh));
                        }
                        eout = 2 * (int)f->bias - 1 - (int)ef;
                        mant = rcp_sar((F << 1) - ((int64_t)1 << TNKR_RCP_Q) +
                                           ((int64_t)1 << ((int)TNKR_RCP_Q - (int)f->m_bits - 1)),
                                       (unsigned)((int)TNKR_RCP_Q - (int)f->m_bits));
                        if (mant >= ((int64_t)1 << f->m_bits)) {
                            mant = 0; /* rounding carry reaches the exponent */
                            eout++;
                        }
                        ob = (eout <= 0) ? (sign << (f->e_bits + f->m_bits))
                                         : (sign << (f->e_bits + f->m_bits)) |
                                               ((uint64_t)eout << f->m_bits) | (uint64_t)mant;
                    }
                    tnkr_view_write(&out, n, c, h, w, ob);
                }
}

/* ---- cast ----
 * kernel.h: plain dtype conversion, both HWAlign, same shape; INT <-> FP
 * goes through FP32 only; integer narrowing saturates (so do the
 * same-width sign changes); FP32->int rounds per `round` then saturates,
 * NaN converts to 0. rint() rounds to nearest even in the default FP
 * environment, which is the RNE arm. */
extern "C" void top_vec_cast(tdr_idx_t output, tdr_idx_t input, round_mode_t round) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, in;
    bool ifp, ofp;
    uint32_t n, c, h, w;

    TNKR_REF_RESOLVE(ctx, &out, output, 0, "vec_cast", "out");
    TNKR_REF_RESOLVE(ctx, &in, input, 0, "vec_cast", "in");
    TNKR_REF_CHECK("vec_cast", out.layout == TDR_LAYOUT_HWALIGN && in.layout == TDR_LAYOUT_HWALIGN,
                   "operands must be HWAlign");
    TNKR_REF_CHECK("vec_cast",
                   out.dn == in.dn && out.dc == in.dc && out.dh == in.dh && out.dw == in.dw,
                   "output shape must equal the input shape");
    ifp = tnkr_dt_is_fp(in.dtype);
    ofp = tnkr_dt_is_fp(out.dtype);
    TNKR_REF_CHECK("vec_cast",
                   !(ifp && tnkr_fp_fmt(in.dtype) == NULL) &&
                       !(ofp && tnkr_fp_fmt(out.dtype) == NULL),
                   "codec-less FP formats (FP16/E5M2) unsupported");
    TNKR_REF_CHECK("vec_cast", ifp == ofp || (ifp ? in.dtype : out.dtype) == TN_DTYPE_FP32,
                   "INT <-> FP converts through FP32 only");

    for (n = 0; n < out.dn; n++)
        for (c = 0; c < out.dc; c++)
            for (h = 0; h < out.dh; h++)
                for (w = 0; w < out.dw; w++) {
                    uint64_t ib = tnkr_view_read(&in, n, c, h, w), ob;
                    if (!ifp && !ofp) {
                        ob = tnkr_encode_int(tnkr_decode_int(ib, in.dtype), out.dtype,
                                             OVF_MODE_SATURATE);
                    } else if (!ifp) {
                        ob = tnkr_encode_fp((double)tnkr_decode_int(ib, in.dtype), out.dtype);
                    } else if (!ofp) {
                        double v = tnkr_decode_fp(ib, in.dtype);
                        double r = (round == ROUND_MODE_RTZ) ? trunc(v) : rint(v);
                        int64_t iv;
                        if (isnan(r))
                            iv = 0; /* NaN -> 0 */
                        else if (r >= 9.2233720368547758e18)
                            iv = INT64_MAX; /* saturate below */
                        else if (r <= -9.2233720368547758e18)
                            iv = INT64_MIN;
                        else
                            iv = (int64_t)r;
                        ob = tnkr_encode_int(iv, out.dtype, OVF_MODE_SATURATE);
                    } else {
                        ob = tnkr_encode_fp(tnkr_decode_fp(ib, in.dtype), out.dtype);
                    }
                    tnkr_view_write(&out, n, c, h, w, ob);
                }
}

/* ---- quant / dequant ----
 * Shapes (kernel.h): (1,M,1,N), HWAlign; scales (1,M,1,N//g), g | N.
 * Scale derivation (kernel.h): fp8 uses the OCP MX convention
 * 2^(floor(log2(amax)) - emax) — an amax element above the format max
 * clamps to +-max (elements encode saturate + RNE); mxfp4 uses
 * 2^ceil(log2(amax/6)) and nxfp4 uses round_up(amax/6) in UE5M3
 * (NVFP4 semantics) — the two round-up conventions never saturate the
 * amax element. A zero group stores scale pattern 0 (both scale
 * formats decode it as 0.0); a group containing NaN poisons the whole
 * group (scale = NaN). */
typedef enum { QK_FP8, QK_MXFP4, QK_NXFP4 } qkind_t;

/* UE5M3 round-up encode: the smallest representable pattern whose value
 * is >= v (v >= 0). Used ONLY by the nxfp4 scale derivation (NVFP4
 * semantics); every other encoding keeps tnkr_encode_fp's RNE. Unsigned
 * FP patterns are value-monotonic, so "the next representable value up"
 * is just pattern + 1 — subnormals and mantissa-carry boundaries are
 * covered automatically. The chip has no subnormals: a round-up landing
 * below the minimum normal flushes to it. Saturates at max finite
 * (never reaches the 0xFF NaN pattern). */
static uint64_t encode_ue5m3_ceil(double v) {
    uint64_t p = tnkr_encode_fp(v, TN_DTYPE_UE5M3);
    if (p != 0xFEu /* max finite; 0xFF is NaN */ && tnkr_decode_fp(p, TN_DTYPE_UE5M3) < v)
        p++;
    if (p != 0 && p < 0x08u) /* min normal: exponent field 1, mantissa 0 */
        p = 0x08u;
    return p;
}

/* NVFP4 scale reciprocal LUT (the chip's divider-free element path):
 * the index is the scale's UE5M3 mantissa field, the entry is
 * 1/(1 + m/8) as the hardware's 6-decimal constants; the full
 * reciprocal is LUT[m] * 2^-(e-15) for exponent field e. Only normal
 * patterns reach it (encode_ue5m3_ceil flushes subnormals). */
static const float UE5M3_RECIP_LUT[8] = {1.000000f, 0.888889f, 0.800000f, 0.727273f,
                                         0.666667f, 0.615385f, 0.571429f, 0.533333f};

static void vec_quant(const char *op, tdr_idx_t out_i, tdr_idx_t os_i, tdr_idx_t in_i,
                      group_size_t gs, qkind_t kind) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, os, in;
    tn_dtype_t qdt = (kind == QK_FP8) ? TN_DTYPE_FP8_E4M3 : TN_DTYPE_FP4;
    tn_dtype_t sdt = (kind == QK_NXFP4) ? TN_DTYPE_UE5M3 : TN_DTYPE_UE8M0;
    uint32_t m, n, g, mi, gi, i;

    TNKR_REF_RESOLVE(ctx, &out, out_i, 0, op, "out");
    TNKR_REF_RESOLVE(ctx, &os, os_i, 0, op, "out_scale");
    TNKR_REF_RESOLVE(ctx, &in, in_i, 0, op, "in");
    TNKR_REF_CHECK(op,
                   out.layout == TDR_LAYOUT_HWALIGN && os.layout == TDR_LAYOUT_HWALIGN &&
                       in.layout == TDR_LAYOUT_HWALIGN,
                   "operands must be HWAlign");
    TNKR_REF_CHECK(op, in.dtype == TN_DTYPE_BF16 || in.dtype == TN_DTYPE_FP32,
                   "input dtype must be BF16/FP32");
    TNKR_REF_CHECK(op, out.dtype == qdt && os.dtype == sdt, "output/scale dtype mismatch");
    TNKR_REF_CHECK(
        op, in.dn == 1 && in.dh == 1 && out.dn == 1 && out.dh == 1 && os.dn == 1 && os.dh == 1,
        "quant shapes are (1,M,1,N)");
    m = in.dc;
    n = in.dw;
    TNKR_REF_CHECK(op, out.dc == m && out.dw == n, "output shape must match the input");
    g = (gs == GROUP_SIZE_32) ? 32u : 16u;
    TNKR_REF_CHECK(op, n % g == 0, "group width must divide N");
    TNKR_REF_CHECK(op, os.dc == m && os.dw == n / g, "scale shape must be (1,M,1,N//g)");

    for (mi = 0; mi < m; mi++)
        for (gi = 0; gi < n / g; gi++) {
            double amax = 0.0, sval;
            uint64_t spat;
            float rinv = 0.0f; /* nxfp4 only: the scale's LUT reciprocal */
            bool nan = false;

            for (i = 0; i < g; i++) {
                double v = tnkr_decode_fp(tnkr_view_read(&in, 0, mi, 0, gi * g + i), in.dtype);
                if (isnan(v))
                    nan = true;
                else if (fabs(v) > amax)
                    amax = fabs(v);
            }
            if (nan) {
                spat = 0xFFu; /* scale NaN poisons the whole group */
                sval = NAN;
            } else if (amax == 0.0) {
                spat = 0; /* zero group: scale 0 decodes to 0.0 */
                sval = 0.0;
            } else if (kind == QK_NXFP4) {
                /* NVFP4: scale = round_up(amax/6); the elements multiply
                 * by the scale's LUT reciprocal (no divider on the chip) */
                spat = encode_ue5m3_ceil(amax / 6.0);
                sval = tnkr_decode_fp(spat, TN_DTYPE_UE5M3);
                rinv = ldexpf(UE5M3_RECIP_LUT[spat & 7u], 15 - (int)(spat >> 3));
            } else if (kind == QK_MXFP4) {
                /* MXFP4: scale = 2^ceil(log2(amax/6)) — the smallest
                 * power of two keeping the scaled amax <= 6, so the amax
                 * element never saturates */
                int se;
                if (isinf(amax)) {
                    se = 127;
                } else {
                    se = (int)ceil(log2(amax / 6.0));
                    if (se > 127)
                        se = 127;
                    if (se < -127)
                        se = -127;
                }
                spat = (uint64_t)(se + 127);
                sval = ldexp(1.0, se);
            } else {
                /* FP8, OCP MX: scale = 2^(floor(log2(amax)) - emax),
                 * emax = 8 for E4M3; an amax element above +max
                 * saturates */
                int e = 0, se;
                if (isinf(amax)) {
                    se = 127;
                } else {
                    (void)frexp(amax, &e); /* amax = f * 2^e, f in [0.5,1) */
                    se = e - 1 - 8;
                    if (se > 127)
                        se = 127;
                    if (se < -127)
                        se = -127;
                }
                spat = (uint64_t)(se + 127);
                sval = ldexp(1.0, se);
            }
            tnkr_view_write(&os, 0, mi, 0, gi, spat);
            for (i = 0; i < g; i++) {
                double v = tnkr_decode_fp(tnkr_view_read(&in, 0, mi, 0, gi * g + i), in.dtype);
                double q;
                if (sval == 0.0)
                    q = 0.0;
                else if (kind == QK_NXFP4 && !isnan(sval))
                    q = (double)((float)v * rinv); /* one FP32 multiply */
                else
                    q = v / sval;
                tnkr_view_write(&out, 0, mi, 0, gi * g + i, tnkr_encode_fp(q, qdt));
            }
        }
}

extern "C" void top_vec_quant_fp8(tdr_idx_t output, tdr_idx_t out_scale, tdr_idx_t input,
                                  group_size_t g_size) {
    vec_quant("vec_quant.fp8", output, out_scale, input, g_size, QK_FP8);
}

extern "C" void top_vec_quant_nxfp4(tdr_idx_t output, tdr_idx_t out_scale, tdr_idx_t input,
                                    group_size_t g_size) {
    vec_quant("vec_quant.nxfp4", output, out_scale, input, g_size, QK_NXFP4);
}

extern "C" void top_vec_quant_mxfp4(tdr_idx_t output, tdr_idx_t out_scale, tdr_idx_t input,
                                    group_size_t g_size) {
    vec_quant("vec_quant.mxfp4", output, out_scale, input, g_size, QK_MXFP4);
}

extern "C" void top_vec_dequant_fp8(tdr_idx_t output, tdr_idx_t input, tdr_idx_t in_scale,
                                    group_size_t g_size) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, in, is;
    uint32_t m, n, g, mi, i;

    TNKR_REF_RESOLVE(ctx, &out, output, 0, "vec_dequant.fp8", "out");
    TNKR_REF_RESOLVE(ctx, &in, input, 0, "vec_dequant.fp8", "in");
    TNKR_REF_RESOLVE(ctx, &is, in_scale, 0, "vec_dequant.fp8", "in_scale");
    TNKR_REF_CHECK("vec_dequant.fp8",
                   out.layout == TDR_LAYOUT_HWALIGN && in.layout == TDR_LAYOUT_HWALIGN &&
                       is.layout == TDR_LAYOUT_HWALIGN,
                   "operands must be HWAlign");
    TNKR_REF_CHECK("vec_dequant.fp8", in.dtype == TN_DTYPE_FP8_E4M3 && is.dtype == TN_DTYPE_UE8M0,
                   "input dtype must be FP8_E4M3 with UE8M0 scales");
    TNKR_REF_CHECK("vec_dequant.fp8", out.dtype == TN_DTYPE_BF16 || out.dtype == TN_DTYPE_FP32,
                   "output dtype must be BF16/FP32");
    TNKR_REF_CHECK("vec_dequant.fp8",
                   in.dn == 1 && in.dh == 1 && out.dn == 1 && out.dh == 1 && is.dn == 1 &&
                       is.dh == 1,
                   "dequant shapes are (1,M,1,N)");
    m = in.dc;
    n = in.dw;
    TNKR_REF_CHECK("vec_dequant.fp8", out.dc == m && out.dw == n,
                   "output shape must match the input");
    g = (g_size == GROUP_SIZE_32) ? 32u : 16u;
    TNKR_REF_CHECK("vec_dequant.fp8", n % g == 0, "group width must divide N");
    TNKR_REF_CHECK("vec_dequant.fp8", is.dc == m && is.dw == n / g,
                   "scale shape must be (1,M,1,N//g)");

    for (mi = 0; mi < m; mi++)
        for (i = 0; i < n; i++) {
            double q = tnkr_decode_fp(tnkr_view_read(&in, 0, mi, 0, i), in.dtype);
            double s = tnkr_decode_fp(tnkr_view_read(&is, 0, mi, 0, i / g), is.dtype);
            tnkr_view_write(&out, 0, mi, 0, i, tnkr_encode_fp(q * s, out.dtype));
        }
}

/* one BF16 rounding step (RNE + saturate) — the int8 dequant MAC's
 * intermediate precision */
static double bf16_step(double v) {
    return tnkr_decode_fp(tnkr_encode_fp(v, TN_DTYPE_BF16), TN_DTYPE_BF16);
}

/* int8 dequant: scale and min stacked as (1,M,N//g,2), BF16 only;
 * W = scale*q - min with the product and the subtract each rounded to
 * BF16 once (the chip MAC runs in BF16) */
extern "C" void top_vec_dequant_int8(tdr_idx_t output, tdr_idx_t input, tdr_idx_t scale_min,
                                     group_size_i8_t g_size) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, in, sm;
    uint32_t m, n, g, mi, i;

    TNKR_REF_RESOLVE(ctx, &out, output, 0, "vec_dequant.int8", "out");
    TNKR_REF_RESOLVE(ctx, &in, input, 0, "vec_dequant.int8", "in");
    TNKR_REF_RESOLVE(ctx, &sm, scale_min, 0, "vec_dequant.int8", "scale_min");
    TNKR_REF_CHECK("vec_dequant.int8",
                   out.layout == TDR_LAYOUT_HWALIGN && in.layout == TDR_LAYOUT_HWALIGN &&
                       sm.layout == TDR_LAYOUT_HWALIGN,
                   "operands must be HWAlign");
    TNKR_REF_CHECK("vec_dequant.int8", in.dtype == TN_DTYPE_S8, "input dtype must be S8");
    TNKR_REF_CHECK("vec_dequant.int8", sm.dtype == TN_DTYPE_BF16, "scale_min dtype must be BF16");
    TNKR_REF_CHECK("vec_dequant.int8", out.dtype == TN_DTYPE_BF16, "output dtype must be BF16");
    TNKR_REF_CHECK("vec_dequant.int8",
                   in.dn == 1 && in.dh == 1 && out.dn == 1 && out.dh == 1 && sm.dn == 1 &&
                       sm.dw == 2,
                   "shapes are (1,M,1,N) with scale_min (1,M,N//g,2)");
    m = in.dc;
    n = in.dw;
    TNKR_REF_CHECK("vec_dequant.int8", out.dc == m && out.dw == n && sm.dc == m,
                   "output shape must match the input, scale_min C must match");
    g = (g_size == GROUP_SIZE_128) ? 128u : 64u;
    TNKR_REF_CHECK("vec_dequant.int8", n % g == 0, "group width must divide N");
    TNKR_REF_CHECK("vec_dequant.int8", sm.dh == n / g, "scale_min H extent must be N//g");

    for (mi = 0; mi < m; mi++)
        for (i = 0; i < n; i++) {
            double sc = tnkr_decode_fp(tnkr_view_read(&sm, 0, mi, i / g, 0), sm.dtype);
            double mn = tnkr_decode_fp(tnkr_view_read(&sm, 0, mi, i / g, 1), sm.dtype);
            double q = (double)tnkr_decode_int(tnkr_view_read(&in, 0, mi, 0, i), in.dtype);
            tnkr_view_write(&out, 0, mi, 0, i,
                            tnkr_encode_fp(bf16_step(bf16_step(q * sc) - mn), out.dtype));
        }
}

/* ---- conv (FP32) ----
 * kernel.h: input (N,IC,H,W), kernel (IC,OC,KH,KW), output (N,OC,OH,OW),
 * all FP32 HWAlign — the kernel keeps its external (IC,OC,KH,KW) shape
 * (no compile-time reorganization into the mm engine's (1,OC,KH*KW,IC)
 * WAlign): lane oc owns its whole (IC,KH,KW) weight slice; kernel
 * dh==kernel_h, dw==kernel_w. Bias (1,OC,1,1)
 * FP32, PER_CHANNEL only (with NONE the bias index is ignored).
 * Geometry and the OH/OW derivation are the mm conv's (insert-expanded
 * input space; padding cells and insert holes contribute zero).
 * Numeric semantics: the KH*KW*IC reduction is the vec engine's FP32
 * MAC chain (tap order kh -> kw -> ic, one step per tap), the bias is
 * appended as one further FP32 add step — NOT the mm double tail. */

/* one step of the FP32 chain: the exact term is added to the
 * FP32-valued acc and rounded (RNE + saturate) to FP32 once — shared by
 * the conv / matmul reductions */
static double vec_mm_step(double acc, double term) {
    return tnkr_decode_fp(tnkr_encode_fp(acc + term, TN_DTYPE_FP32), TN_DTYPE_FP32);
}

extern "C" void top_vec_conv(tdr_idx_t output, tdr_idx_t input, tdr_idx_t kernel, tdr_idx_t bias,
                             const conv_attr_t *attr, bias_mode_t bias_mode) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, in, k, b;
    uint32_t n, oc, oh, ow, kh, kw, ic;
    uint32_t h_ins, w_ins, kheff, kweff, oh_n, ow_n;
    int64_t oh_d, ow_d, ih_ins, iw_ins, ih, iw;
    int32_t ins1;

    TNKR_REF_CHECK("vec_conv", attr != nullptr, "attr pointer is NULL");
    TNKR_REF_RESOLVE(ctx, &out, output, 0, "vec_conv", "out");
    TNKR_REF_RESOLVE(ctx, &in, input, 0, "vec_conv", "in");
    TNKR_REF_RESOLVE(ctx, &k, kernel, 0, "vec_conv", "kernel");
    TNKR_REF_CHECK("vec_conv", bias_mode == BIAS_MODE_NONE || bias_mode == BIAS_MODE_PER_CHANNEL,
                   "bias mode must be NONE or PER_CHANNEL");
    /* bias exists only under PER_CHANNEL: with NONE the bias index is
     * ignored and need not name a configured TDR */
    if (bias_mode == BIAS_MODE_PER_CHANNEL)
        TNKR_REF_RESOLVE(ctx, &b, bias, 0, "vec_conv", "bias");

    /* kernel.h: all operands HWAlign (unlike the mm conv's WAlign kernel) */
    TNKR_REF_CHECK("vec_conv",
                   out.layout == TDR_LAYOUT_HWALIGN && in.layout == TDR_LAYOUT_HWALIGN &&
                       k.layout == TDR_LAYOUT_HWALIGN,
                   "operands must be HWAlign");

    /* FP32 only (kernel.h) */
    TNKR_REF_CHECK("vec_conv",
                   in.dtype == TN_DTYPE_FP32 && k.dtype == TN_DTYPE_FP32 &&
                       out.dtype == TN_DTYPE_FP32,
                   "dtypes must be FP32");

    /* geometry limits (kernel.h): stride<=15, pad/insert<=3, dilation 1..3 */
    TNKR_REF_CHECK("vec_conv",
                   attr->stride_w != 0 && attr->stride_w <= 15 && attr->stride_h != 0 &&
                       attr->stride_h <= 15,
                   "stride must be 1..15");
    TNKR_REF_CHECK("vec_conv",
                   attr->pad_t <= 3 && attr->pad_b <= 3 && attr->pad_l <= 3 && attr->pad_r <= 3,
                   "pad must be 0..3");
    TNKR_REF_CHECK("vec_conv", attr->insert <= 3, "insert must be 0..3");
    TNKR_REF_CHECK("vec_conv", attr->dilation != 0 && attr->dilation <= 3, "dilation must be 1..3");

    /* operand shapes: kernel (IC,OC,KH,KW) carries KH/KW directly in
     * dh/dw; output (N,OC,OH,OW) */
    TNKR_REF_CHECK("vec_conv",
                   attr->kernel_w != 0 && attr->kernel_w <= 15 && attr->kernel_h != 0 &&
                       attr->kernel_h <= 15,
                   "kernel_h/kernel_w must be 1..15");
    TNKR_REF_CHECK("vec_conv", (uint32_t)k.dh == attr->kernel_h && (uint32_t)k.dw == attr->kernel_w,
                   "kernel dh/dw must equal kernel_h/kernel_w");
    TNKR_REF_CHECK("vec_conv", k.dn == in.dc && k.dc == out.dc && out.dn == in.dn,
                   "operand shapes: kernel (IC,OC,KH,KW), output (N,OC,OH,OW)");

    if (bias_mode == BIAS_MODE_PER_CHANNEL) {
        TNKR_REF_CHECK("vec_conv", b.layout == TDR_LAYOUT_HWALIGN, "bias must be HWAlign");
        TNKR_REF_CHECK("vec_conv", b.dtype == TN_DTYPE_FP32, "bias dtype must be FP32");
        TNKR_REF_CHECK("vec_conv", b.dn == 1 && b.dh == 1 && b.dw == 1 && b.dc == out.dc,
                       "bias shape must be (1,OC,1,1)");
    }

    /* output shape derivation (floor) — same as the mm conv */
    ins1 = (int32_t)attr->insert + 1;
    h_ins = ((uint32_t)in.dh - 1u) * (uint32_t)ins1 + 1u;
    w_ins = ((uint32_t)in.dw - 1u) * (uint32_t)ins1 + 1u;
    kheff = ((uint32_t)attr->kernel_h - 1u) * (uint32_t)attr->dilation + 1u;
    kweff = ((uint32_t)attr->kernel_w - 1u) * (uint32_t)attr->dilation + 1u;
    oh_d = (int64_t)h_ins + attr->pad_t + attr->pad_b - (int64_t)kheff;
    ow_d = (int64_t)w_ins + attr->pad_l + attr->pad_r - (int64_t)kweff;
    TNKR_REF_CHECK("vec_conv", oh_d >= 0 && ow_d >= 0, "no complete window fits the input");
    oh_n = (uint32_t)oh_d / attr->stride_h + 1u;
    ow_n = (uint32_t)ow_d / attr->stride_w + 1u;
    TNKR_REF_CHECK("vec_conv", out.dh == oh_n && out.dw == ow_n,
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
                                rv = tnkr_decode_fp(tnkr_view_read(&k, ic, oc, kh, kw), k.dtype);
                                acc = vec_mm_step(acc, lv * rv);
                            }
                    if (bias_mode == BIAS_MODE_PER_CHANNEL)
                        acc = vec_mm_step(acc,
                                          tnkr_decode_fp(tnkr_view_read(&b, 0, oc, 0, 0), b.dtype));
                    tnkr_view_write(&out, n, oc, oh, ow, tnkr_encode_fp(acc, out.dtype));
                }
}

/* ---- depthwise (FP8/BF16 -> BF16/FP32) ----
 * kernel.h: input (N,IC,H,W) FP8/BF16, kernel (1,IC,KH,KW) same dtype,
 * output (N,IC,OH,OW) BF16/FP32, all HWAlign; bias (1,IC,1,1) BF16/FP32,
 * PER_CHANNEL only. Geometry and the OH/OW derivation are the mm
 * conv's (insert-expanded input space; padding cells and insert holes
 * contribute zero); each output channel reduces only its own input
 * channel (tap order kh -> kw, one vec_mm_step per tap), the bias is
 * appended as one further FP32 add step, and the result is encoded to
 * the output dtype (RNE + saturate). */
extern "C" void top_vec_depthwise(tdr_idx_t output, tdr_idx_t input, tdr_idx_t kernel,
                                  tdr_idx_t bias, const conv_attr_t *attr, bias_mode_t bias_mode) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, in, k, b;
    uint32_t n, c, oh, ow, kh, kw;
    uint32_t h_ins, w_ins, kheff, kweff, oh_n, ow_n;
    int64_t oh_d, ow_d, ih_ins, iw_ins, ih, iw;
    int32_t ins1;

    TNKR_REF_CHECK("vec_depthwise", attr != nullptr, "attr pointer is NULL");
    TNKR_REF_RESOLVE(ctx, &out, output, 0, "vec_depthwise", "out");
    TNKR_REF_RESOLVE(ctx, &in, input, 0, "vec_depthwise", "in");
    TNKR_REF_RESOLVE(ctx, &k, kernel, 0, "vec_depthwise", "kernel");
    TNKR_REF_CHECK("vec_depthwise",
                   bias_mode == BIAS_MODE_NONE || bias_mode == BIAS_MODE_PER_CHANNEL,
                   "bias mode must be NONE or PER_CHANNEL");
    /* bias exists only under PER_CHANNEL: with NONE the bias index is
     * ignored and need not name a configured TDR */
    if (bias_mode == BIAS_MODE_PER_CHANNEL)
        TNKR_REF_RESOLVE(ctx, &b, bias, 0, "vec_depthwise", "bias");

    /* kernel.h: all operands HWAlign */
    TNKR_REF_CHECK("vec_depthwise",
                   out.layout == TDR_LAYOUT_HWALIGN && in.layout == TDR_LAYOUT_HWALIGN &&
                       k.layout == TDR_LAYOUT_HWALIGN,
                   "operands must be HWAlign");

    /* dtype rules (kernel.h): input FP8/BF16, kernel = input,
     * output BF16/FP32 */
    TNKR_REF_CHECK("vec_depthwise", in.dtype == TN_DTYPE_FP8_E4M3 || in.dtype == TN_DTYPE_BF16,
                   "input dtype must be FP8_E4M3 or BF16");
    TNKR_REF_CHECK("vec_depthwise", k.dtype == in.dtype, "kernel dtype must equal the input dtype");
    TNKR_REF_CHECK("vec_depthwise", out.dtype == TN_DTYPE_BF16 || out.dtype == TN_DTYPE_FP32,
                   "output dtype must be BF16/FP32");

    /* geometry limits (kernel.h): stride<=15, pad/insert<=3, dilation 1..3 */
    TNKR_REF_CHECK("vec_depthwise",
                   attr->stride_w != 0 && attr->stride_w <= 15 && attr->stride_h != 0 &&
                       attr->stride_h <= 15,
                   "stride must be 1..15");
    TNKR_REF_CHECK("vec_depthwise",
                   attr->pad_t <= 3 && attr->pad_b <= 3 && attr->pad_l <= 3 && attr->pad_r <= 3,
                   "pad must be 0..3");
    TNKR_REF_CHECK("vec_depthwise", attr->insert <= 3, "insert must be 0..3");
    TNKR_REF_CHECK("vec_depthwise", attr->dilation != 0 && attr->dilation <= 3,
                   "dilation must be 1..3");

    /* operand shapes: kernel (1,IC,KH,KW) carries KH/KW directly in
     * dh/dw; output (N,IC,OH,OW) */
    TNKR_REF_CHECK("vec_depthwise",
                   attr->kernel_w != 0 && attr->kernel_w <= 15 && attr->kernel_h != 0 &&
                       attr->kernel_h <= 15,
                   "kernel_h/kernel_w must be 1..15");
    TNKR_REF_CHECK("vec_depthwise",
                   (uint32_t)k.dh == attr->kernel_h && (uint32_t)k.dw == attr->kernel_w,
                   "kernel dh/dw must equal kernel_h/kernel_w");
    TNKR_REF_CHECK("vec_depthwise",
                   k.dn == 1 && k.dc == in.dc && out.dn == in.dn && out.dc == in.dc,
                   "operand shapes: kernel (1,IC,KH,KW), output (N,IC,OH,OW)");

    if (bias_mode == BIAS_MODE_PER_CHANNEL) {
        TNKR_REF_CHECK("vec_depthwise", b.layout == TDR_LAYOUT_HWALIGN, "bias must be HWAlign");
        TNKR_REF_CHECK("vec_depthwise", b.dtype == TN_DTYPE_BF16 || b.dtype == TN_DTYPE_FP32,
                       "bias dtype must be BF16/FP32");
        TNKR_REF_CHECK("vec_depthwise", b.dn == 1 && b.dh == 1 && b.dw == 1 && b.dc == in.dc,
                       "bias shape must be (1,IC,1,1)");
    }

    /* output shape derivation (floor) — same as the mm conv */
    ins1 = (int32_t)attr->insert + 1;
    h_ins = ((uint32_t)in.dh - 1u) * (uint32_t)ins1 + 1u;
    w_ins = ((uint32_t)in.dw - 1u) * (uint32_t)ins1 + 1u;
    kheff = ((uint32_t)attr->kernel_h - 1u) * (uint32_t)attr->dilation + 1u;
    kweff = ((uint32_t)attr->kernel_w - 1u) * (uint32_t)attr->dilation + 1u;
    oh_d = (int64_t)h_ins + attr->pad_t + attr->pad_b - (int64_t)kheff;
    ow_d = (int64_t)w_ins + attr->pad_l + attr->pad_r - (int64_t)kweff;
    TNKR_REF_CHECK("vec_depthwise", oh_d >= 0 && ow_d >= 0, "no complete window fits the input");
    oh_n = (uint32_t)oh_d / attr->stride_h + 1u;
    ow_n = (uint32_t)ow_d / attr->stride_w + 1u;
    TNKR_REF_CHECK("vec_depthwise", out.dh == oh_n && out.dw == ow_n,
                   "output shape must match the derived OH/OW");

    for (n = 0; n < out.dn; n++)
        for (c = 0; c < out.dc; c++)
            for (oh = 0; oh < out.dh; oh++)
                for (ow = 0; ow < out.dw; ow++) {
                    double acc = 0.0;

                    for (kh = 0; kh < attr->kernel_h; kh++)
                        for (kw = 0; kw < attr->kernel_w; kw++) {
                            double lv, rv;

                            ih_ins = (int64_t)oh * attr->stride_h - attr->pad_t +
                                     (int64_t)kh * attr->dilation;
                            iw_ins = (int64_t)ow * attr->stride_w - attr->pad_l +
                                     (int64_t)kw * attr->dilation;
                            if (ih_ins % ins1 != 0 || iw_ins % ins1 != 0)
                                continue; /* insert hole contributes zero */
                            ih = ih_ins / ins1;
                            iw = iw_ins / ins1;
                            if (ih < 0 || ih >= (int64_t)in.dh || iw < 0 || iw >= (int64_t)in.dw)
                                continue; /* padding contributes zero */
                            lv = tnkr_decode_fp(
                                tnkr_view_read(&in, n, c, (uint32_t)ih, (uint32_t)iw), in.dtype);
                            rv = tnkr_decode_fp(tnkr_view_read(&k, 0, c, kh, kw), k.dtype);
                            acc = vec_mm_step(acc, lv * rv);
                        }
                    if (bias_mode == BIAS_MODE_PER_CHANNEL)
                        acc = vec_mm_step(acc,
                                          tnkr_decode_fp(tnkr_view_read(&b, 0, c, 0, 0), b.dtype));
                    tnkr_view_write(&out, n, c, oh, ow, tnkr_encode_fp(acc, out.dtype));
                }
}

/* ---- pool ----
 * kernel.h: input (N,C,H,W) FP8/BF16/FP32, output (N,C,OH,OW) same dtype,
 * both HWAlign; OH = (H + pad_t + pad_b - KH) / stride_h + 1 (floor, the
 * tail windows are dropped), OW likewise; padding cells contribute the
 * operation identity (max: -inf, min: +inf, sum: 0), so an all-padding
 * window writes the encoded identity (max -> -max_finite). */
typedef enum { P_MAX, P_MIN, P_SUM } pop_t;

static void vec_pool(const char *op, tdr_idx_t out_i, tdr_idx_t in_i, const pool_attr_t *attr,
                     pop_t pop) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, in;
    uint32_t ph, pw, oh_n, ow_n, n, c, oh, ow;

    TNKR_REF_CHECK(op, attr != NULL, "attr pointer is NULL");
    TNKR_REF_RESOLVE(ctx, &out, out_i, 0, op, "out");
    TNKR_REF_RESOLVE(ctx, &in, in_i, 0, op, "in");
    TNKR_REF_CHECK(op, out.layout == TDR_LAYOUT_HWALIGN && in.layout == TDR_LAYOUT_HWALIGN,
                   "operands must be HWAlign");
    TNKR_REF_CHECK(op, tnkr_dt_is_fp(in.dtype) && tnkr_fp_fmt(in.dtype) != NULL,
                   "input dtype must be FP8/BF16/FP32");
    TNKR_REF_CHECK(op, out.dtype == in.dtype, "output dtype must equal the input dtype");
    TNKR_REF_CHECK(op, out.dn == in.dn && out.dc == in.dc, "output N/C must match the input");
    TNKR_REF_CHECK(op,
                   attr->stride_h != 0 && attr->stride_w != 0 && attr->stride_h <= 15 &&
                       attr->stride_w <= 15,
                   "stride must be 1..15");
    TNKR_REF_CHECK(op, attr->pad_t <= 3 && attr->pad_b <= 3 && attr->pad_l <= 3 && attr->pad_r <= 3,
                   "pad must be 0..3");
    TNKR_REF_CHECK(op,
                   attr->kernel_h != 0 && attr->kernel_w != 0 && attr->kernel_h <= 16383 &&
                       attr->kernel_w <= 16383,
                   "kernel size must be 1..16383");
    ph = in.dh + attr->pad_t + attr->pad_b;
    pw = in.dw + attr->pad_l + attr->pad_r;
    TNKR_REF_CHECK(op, attr->kernel_h <= ph && attr->kernel_w <= pw,
                   "kernel larger than the padded input");
    oh_n = (ph - attr->kernel_h) / attr->stride_h + 1;
    ow_n = (pw - attr->kernel_w) / attr->stride_w + 1;
    TNKR_REF_CHECK(op, out.dh == oh_n && out.dw == ow_n,
                   "output shape must match the derived OH/OW");

    for (n = 0; n < out.dn; n++)
        for (c = 0; c < out.dc; c++)
            for (oh = 0; oh < oh_n; oh++)
                for (ow = 0; ow < ow_n; ow++) {
                    double acc = (pop == P_MAX) ? -INFINITY : (pop == P_MIN) ? INFINITY : 0.0;
                    uint32_t kh, kw;
                    for (kh = 0; kh < attr->kernel_h; kh++)
                        for (kw = 0; kw < attr->kernel_w; kw++) {
                            int64_t r = (int64_t)oh * attr->stride_h + kh - attr->pad_t;
                            int64_t q = (int64_t)ow * attr->stride_w + kw - attr->pad_l;
                            double v;
                            if (r < 0 || r >= in.dh || q < 0 || q >= in.dw)
                                continue; /* padding: identity */
                            v = tnkr_decode_fp(tnkr_view_read(&in, n, c, (uint32_t)r, (uint32_t)q),
                                               in.dtype);
                            if (pop == P_MAX) {
                                if (v > acc)
                                    acc = v;
                            } else if (pop == P_MIN) {
                                if (v < acc)
                                    acc = v;
                            } else {
                                acc += v;
                            }
                        }
                    tnkr_view_write(&out, n, c, oh, ow, tnkr_encode_fp(acc, out.dtype));
                }
}

extern "C" void top_vec_pool_max(tdr_idx_t output, tdr_idx_t input, const pool_attr_t *attr) {
    vec_pool("vec_pool.max", output, input, attr, P_MAX);
}

extern "C" void top_vec_pool_min(tdr_idx_t output, tdr_idx_t input, const pool_attr_t *attr) {
    vec_pool("vec_pool.min", output, input, attr, P_MIN);
}

extern "C" void top_vec_pool_sum(tdr_idx_t output, tdr_idx_t input, const pool_attr_t *attr) {
    vec_pool("vec_pool.sum", output, input, attr, P_SUM);
}

/* ---- matmul (nnn, FP32) ----
 * kernel.h: left (1,M,1,K), right (1,K,1,N), output (1,M,1,N), all
 * HWAlign, FP32 only; bias (1,SMC_NUM,1,N) FP32 — a C-broadcast add
 * against the product (channel c adds bias row c % SMC_NUM), present
 * only under PER_CHANNEL (with NONE the bias index is ignored).
 * Unlike the mm engine's double-tail golden, the K-reduction here is
 * the vec engine's fused FP32 MAC chain: every step rounds the exact
 * sum once to FP32 (RNE + saturate), so the double accumulator below
 * only ever holds values exactly representable in FP32. */
static void vec_matmul_nnn(const char *op, tdr_idx_t out_i, tdr_idx_t l_i, tdr_idx_t r_i,
                           tdr_idx_t b_i, acc_mode_t acc, bias_mode_t bias_mode) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, l, r, b;
    uint32_t m, k, n, mm, kk, nn;

    TNKR_REF_RESOLVE(ctx, &out, out_i, 0, op, "out");
    TNKR_REF_RESOLVE(ctx, &l, l_i, 0, op, "left");
    TNKR_REF_RESOLVE(ctx, &r, r_i, 0, op, "right");
    TNKR_REF_CHECK(op, bias_mode == BIAS_MODE_NONE || bias_mode == BIAS_MODE_PER_CHANNEL,
                   "bias mode must be NONE or PER_CHANNEL");
    TNKR_REF_CHECK(op, acc == ACC_MODE_OVERWRITE || acc == ACC_MODE_ACCUMULATE,
                   "acc mode must be OVERWRITE or ACCUMULATE");
    /* bias exists only under PER_CHANNEL: with NONE the bias index is
     * ignored and need not name a configured TDR */
    if (bias_mode == BIAS_MODE_PER_CHANNEL)
        TNKR_REF_RESOLVE(ctx, &b, b_i, 0, op, "bias");
    /* kernel.h: all operands HWAlign */
    TNKR_REF_CHECK(op,
                   out.layout == TDR_LAYOUT_HWALIGN && l.layout == TDR_LAYOUT_HWALIGN &&
                       r.layout == TDR_LAYOUT_HWALIGN,
                   "operands must be HWAlign");
    /* kernel.h: FP32 only */
    TNKR_REF_CHECK(
        op, l.dtype == TN_DTYPE_FP32 && r.dtype == TN_DTYPE_FP32 && out.dtype == TN_DTYPE_FP32,
        "FP32 only");
    /* nnn shapes: left (1,M,1,K), right (1,K,1,N), output (1,M,1,N) */
    TNKR_REF_CHECK(op,
                   out.dn == 1 && out.dh == 1 && l.dn == 1 && l.dh == 1 && r.dn == 1 && r.dh == 1,
                   "operands must be (1,X,1,Y)");
    m = l.dc;
    k = l.dw;
    n = r.dw;
    TNKR_REF_CHECK(op, r.dc == k && out.dc == m && out.dw == n, "shape mismatch (nnn)");
    if (bias_mode == BIAS_MODE_PER_CHANNEL) {
        TNKR_REF_CHECK(op, b.layout == TDR_LAYOUT_HWALIGN && b.dn == 1 && b.dh == 1,
                       "bias must be HWAlign (1,C,1,W)");
        TNKR_REF_CHECK(op, b.dtype == TN_DTYPE_FP32,
                       "bias is FP32 only (stricter than the mm engine)");
        TNKR_REF_CHECK(op, b.dc == ctx->info.smc_num && b.dw == n,
                       "bias shape must be (1,SMC_NUM,1,N)");
    }
    TNKR_REF_CHECK(op, acc != ACC_MODE_ACCUMULATE || bias_mode != BIAS_MODE_PER_CHANNEL,
                   "ACCUMULATE and PER_CHANNEL bias are mutually exclusive");

    for (mm = 0; mm < m; mm++)
        for (nn = 0; nn < n; nn++) {
            double acc_d = 0.0;

            for (kk = 0; kk < k; kk++) {
                double lv = tnkr_decode_fp(tnkr_view_read(&l, 0, mm, 0, kk), l.dtype);
                double rv = tnkr_decode_fp(tnkr_view_read(&r, 0, kk, 0, nn), r.dtype);
                acc_d = vec_mm_step(acc_d, lv * rv);
            }
            if (bias_mode == BIAS_MODE_PER_CHANNEL)
                acc_d = vec_mm_step(
                    acc_d,
                    tnkr_decode_fp(tnkr_view_read(&b, 0, mm % ctx->info.smc_num, 0, nn), b.dtype));
            if (acc == ACC_MODE_ACCUMULATE)
                acc_d = vec_mm_step(acc_d,
                                    tnkr_decode_fp(tnkr_view_read(&out, 0, mm, 0, nn), out.dtype));
            tnkr_view_write(&out, 0, mm, 0, nn, tnkr_encode_fp(acc_d, out.dtype));
        }
}

extern "C" void top_vec_matmul_nnn(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right,
                                   tdr_idx_t bias, acc_mode_t acc, bias_mode_t bias_mode) {
    vec_matmul_nnn("vec_matmul.nnn", output, left, right, bias, acc, bias_mode);
}
