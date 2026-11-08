// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/dtype.h
 *
 * Numeric codec — TDR bit pattern <-> compute value
 *
 * The ref backend's numeric-semantics center:
 *   - decode: bit pattern -> double (FP family) / int64 (INT family),
 *     exact, no rounding
 *   - encode: compute result -> target dtype bit pattern
 *       FP : fixed saturate (clamp to ±max_finite, never produces inf)
 *            + RNE. NaN input: formats with a real NaN pattern (FP8
 *            E4M3, the scale formats) encode to it; NaN-free formats
 *            (FP4) saturate to +max finite
 *       INT: ovf_mode_t, 0=saturate 1=truncate (mod 2^w)
 *   - integer intermediates use __int128 (U32*U32 overflows int64;
 *     regops.cpp sets the precedent)
 *
 * Coverage: S8..S32 / U8..U32 / FP8_E4M3 / BF16 / FP32 (needed by the
 * current instructions); FP4 (E2M1) rides the same generic path;
 * UE8M0 / UE5M3 (block-quant scale formats) also ride the generic path
 * (see FMT_UE8M0 / FMT_UE5M3 below).
 *
 * Scale formats (ref-model interpretation):
 *   UE8M0 - OCP MX convention: v = 2^(e-127), 0xFF = NaN (m_bits = 0, so
 *           the generic decode yields exactly 2^(ef-bias) and ef = 0 yields
 *           0).
 *   UE5M3 - unsigned E5M3, bias 15, E4M3-style all-ones-exponent-still-
 *           finite: max finite = (1+6/8)*2^16 = 114688 (pattern 0xFE),
 *           0xFF = NaN, subnormal step 2^-17. Matches the public NVFP4
 *           reference implementations (fp4-all-the-way experimental/fp4.py
 *           uses UE5M3_MAX = 114688 with a 2^-17 quantum; LLVM NVPTX names
 *           the same format in cvt.rn.f16x2.ue5m3x2).
 * ========================================================================== */
#pragma once

#include <tndef.h>

#include "kernel/kernel_types.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- dtype family predicates ---- */
static inline bool tnkr_dt_is_fp(tn_dtype_t dt) {
    switch (dt) {
    case TN_DTYPE_FP4:
    case TN_DTYPE_FP8_E4M3:
    case TN_DTYPE_FP8_E5M2:
    case TN_DTYPE_BF16:
    case TN_DTYPE_FP16:
    case TN_DTYPE_FP32:
    case TN_DTYPE_UE8M0:
    case TN_DTYPE_UE5M3:
        return true;
    default:
        return false;
    }
}

static inline bool tnkr_dt_is_signed(tn_dtype_t dt) {
    switch (dt) {
    case TN_DTYPE_S4:
    case TN_DTYPE_S8:
    case TN_DTYPE_S16:
    case TN_DTYPE_S32:
        return true;
    default:
        return false;
    }
}

/* ==========================================================================
 * Generic binary floating point: (sign, E exponent bits, M mantissa bits,
 * bias)
 * ========================================================================== */
typedef struct {
    unsigned e_bits;
    unsigned m_bits;
    int bias;
    uint32_t max_pat;    /* max finite bit pattern (unsigned) — saturate target */
    uint32_t nan_pat;    /* NaN bit pattern */
    double max_val;      /* max finite value */
    int exp_ones_finite; /* all-ones exp still finite (FP8_E4M3/FP4: no inf) */
} tnkr_fp_fmt_t;

static inline const tnkr_fp_fmt_t *tnkr_fp_fmt(tn_dtype_t dt) {
    static const tnkr_fp_fmt_t FMT_FP8 = {4, 3, 7, 0x7E, 0x7F, 448.0, 1};
    static const tnkr_fp_fmt_t FMT_BF16 = {8, 7, 127, 0x7F7F, 0x7FC0, 3.3895313892515355e38, 0};
    static const tnkr_fp_fmt_t FMT_FP32 = {
        8, 23, 127, 0x7F7FFFFFu, 0x7FC00000u, 3.4028234663852886e38, 0};
    /* OCP FP4 E2M1: no inf/nan, max = 6 (all-ones exp is finite). nan_pat
     * sits OUTSIDE the (e+m)-bit pattern space so the decode NaN check
     * can never fire — 0x7/0xF are +-6.0, not NaN. encode maps NaN input
     * to +max (see tnkr_encode_fp) */
    static const tnkr_fp_fmt_t FMT_FP4 = {2, 1, 1, 0x7, 0x8, 6.0, 1};
    /* OCP MX scale: v = 2^(e-127); 0xFF = NaN (exp_ones_finite, m_bits 0) */
    static const tnkr_fp_fmt_t FMT_UE8M0 = {
        8, 0, 127, 0xFE, 0xFF, 170141183460469231731687303715884105728.0, 1};
    /* unsigned E5M3 scale (nxfp4): bias 15, E4M3-style all-ones-exp finite;
     * max finite = (1+6/8)*2^16 = 114688 (0xFE), 0xFF = NaN */
    static const tnkr_fp_fmt_t FMT_UE5M3 = {5, 3, 15, 0xFE, 0xFF, 114688.0, 1};
    switch (dt) {
    case TN_DTYPE_FP8_E4M3:
        return &FMT_FP8;
    case TN_DTYPE_BF16:
        return &FMT_BF16;
    case TN_DTYPE_FP32:
        return &FMT_FP32;
    case TN_DTYPE_FP4:
        return &FMT_FP4;
    case TN_DTYPE_UE8M0:
        return &FMT_UE8M0;
    case TN_DTYPE_UE5M3:
        return &FMT_UE5M3;
    default:
        return NULL;
    }
}

static inline double tnkr_decode_fp(uint64_t bits, tn_dtype_t dt) {
    const tnkr_fp_fmt_t *f = tnkr_fp_fmt(dt);
    uint64_t sgn, ef, m, exp_max;
    double v;

    if (f == NULL)
        return 0.0;
    sgn = (bits >> (f->e_bits + f->m_bits)) & 1u;
    ef = (bits >> f->m_bits) & ((1u << f->e_bits) - 1u);
    m = bits & ((1u << f->m_bits) - 1u);
    exp_max = (1u << f->e_bits) - 1u;

    if (!f->exp_ones_finite && ef == exp_max) {
        v = (m == 0) ? INFINITY : NAN; /* IEEE: inf / nan */
        return sgn ? -v : v;
    }
    if (f->exp_ones_finite &&
        (bits & (((uint64_t)1 << (f->e_bits + f->m_bits)) - 1u)) == f->nan_pat)
        return NAN; /* FP8: only 0x7F is NaN */
    if (ef == 0)
        v = ldexp((double)m, 1 - f->bias - (int)f->m_bits); /* subnormal */
    else
        v = ldexp(1.0 + ldexp((double)m, -(int)f->m_bits), (int)ef - f->bias);
    return sgn ? -v : v;
}

/* Exact double-precision RNE (x >= 0 and well below 2^53) */
static inline uint64_t tnkr__rne_u64(double x) {
    double fl = floor(x);
    double d = x - fl;
    uint64_t r = (uint64_t)fl;

    if (d > 0.5 || (d == 0.5 && (r & 1u)))
        r += 1;
    return r;
}

/* encode: double -> dtype bit pattern; saturate (clamp to ±max_finite,
 * never inf) + RNE */
static inline uint64_t tnkr_encode_fp(double v, tn_dtype_t dt) {
    const tnkr_fp_fmt_t *f = tnkr_fp_fmt(dt);
    uint64_t sgn, m;
    int e, eu, ef;
    double frac;

    if (f == NULL)
        return 0;
    if (isnan(v)) {
        /* NaN input: formats with a real NaN pattern (FP8 E4M3 = 0x7F,
         * scale formats = 0xFF) keep it; NaN-free formats (FP4, nan_pat
         * outside the pattern space) saturate to +max finite */
        if (f->nan_pat < (1u << (f->e_bits + f->m_bits)))
            return f->nan_pat;
        return f->max_pat;
    }
    sgn = 0;
    if (signbit(v)) {
        sgn = (uint64_t)1 << (f->e_bits + f->m_bits);
        v = -v;
    }
    /* saturate: inf input (div by 0 etc.) also clamps to max finite */
    if (v >= f->max_val)
        return sgn | f->max_pat;
    if (v == 0.0)
        return sgn;

    if (v < ldexp(1.0, 1 - f->bias)) {
        /* subnormal region: step = 2^(1-bias-M); when RNE carries m to
         * 2^M the pattern naturally becomes the smallest normal
         * (exp=1, mant=0) */
        m = tnkr__rne_u64(ldexp(v, f->bias - 1 + (int)f->m_bits));
        return sgn | m;
    }

    frac = frexp(v, &e); /* v = frac * 2^e, frac in [0.5,1) */
    m = tnkr__rne_u64((2.0 * frac - 1.0) * ldexp(1.0, (int)f->m_bits));
    eu = e - 1;
    if (m == ((uint64_t)1 << f->m_bits)) { /* rounding carry: mantissa overflow -> exponent+1 */
        m = 0;
        eu += 1;
    }
    ef = eu + f->bias;
    if (ef > (int)((1u << f->e_bits) - 1u) ||
        (!f->exp_ones_finite && ef == (int)((1u << f->e_bits) - 1u)))
        return sgn | f->max_pat; /* overflow -> saturate */
    return sgn | ((uint64_t)ef << f->m_bits) | m;
}

/* ==========================================================================
 * Integer codec
 * ========================================================================== */
static inline int64_t tnkr_decode_int(uint64_t bits, tn_dtype_t dt) {
    unsigned w = tn_dtype_bits(dt);
    uint64_t mask = (1ull << w) - 1ull;
    uint64_t sign_b = 1ull << (w - 1u);

    bits &= mask;
    if (tnkr_dt_is_signed(dt) && (bits & sign_b))
        return (int64_t)(bits | ~mask); /* sign extend */
    return (int64_t)bits;
}

/* encode: __int128 -> dtype bit pattern
 * ovf = SATURATE: clamp to [min,max]; TRUNCATE: mod 2^w (keep low w bits) */
static inline uint64_t tnkr_encode_int(__int128 v, tn_dtype_t dt, ovf_mode_t ovf) {
    unsigned w = tn_dtype_bits(dt);
    uint64_t mask = (1ull << w) - 1ull;

    if (ovf == OVF_MODE_TRUNCATE)
        return (uint64_t)(unsigned __int128)v & mask;

    {
        __int128 lo, hi;
        if (tnkr_dt_is_signed(dt)) {
            lo = -((__int128)1 << (w - 1u));
            hi = ((__int128)1 << (w - 1u)) - 1;
        } else {
            lo = 0;
            hi = ((__int128)1 << w) - 1;
        }
        if (v < lo)
            v = lo;
        if (v > hi)
            v = hi;
    }
    return (uint64_t)(unsigned __int128)v & mask;
}

/* in-place accumulate at the target dtype (dmr reduce_add): FP decodes
 * both sides, adds in double, re-encodes RNE + saturate; INT saturates
 * (no ovf mode — the fixed store-reduce convention) */
static inline uint64_t tnkr_acc_add(uint64_t acc, uint64_t add, tn_dtype_t dt) {
    if (tnkr_dt_is_fp(dt))
        return tnkr_encode_fp(tnkr_decode_fp(acc, dt) + tnkr_decode_fp(add, dt), dt);
    return tnkr_encode_int((__int128)tnkr_decode_int(acc, dt) + (__int128)tnkr_decode_int(add, dt),
                           dt, OVF_MODE_SATURATE);
}
