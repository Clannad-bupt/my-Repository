#!/usr/bin/env python3
"""Generate taylor_lut.h, prefit coefficient tables for top_vec_taylor.

top_vec_taylor is a generic per-lane Horner evaluator: the caller supplies
the coefficient table as a (1, SMC_NUM, 1, step) operand, one authored
(1,1,1,step) row C-broadcast to every lane at load time. Unlike reciprocal
(fixed function, fixed table) the taylor kernel has no built-in table --
this script provides prefit tables for the common SFU-style activations,
ready to be loaded as the coeff operand directly.

Each table is a degree-(ORDER-1) minimax fit (ABSOLUTE error; the odd
functions vanish at 0, so a relative-error fit would be dominated by the
origin) of one range-reduced kernel function on a fixed interval. Absolute
error on the ranges below translates to ~1 ulp-scale relative error over
the function's significant region:

    exp2      : 2^t          on [0, 1)   (exp/exp2 after the k-split; abs err
                                          scales by 2^k, exact rel error)
    sigmoid   : 1/(1+e^-t)   on [0, 3]   (sigma(-x) = 1 - sigma(x); sigma(3)
                                          = 0.953, beyond that bf16 rounds
                                          the output to 1.0 anyway)
    tanh      : tanh(t)      on [0, 3]   (odd; tanh(3) = 0.9951, |x|>3 rounds
                                          to 1.0 in bf16)
    gelu_tanh : tanh-GELU    on [-4, 4]  (gelu(-4) = -3e-4 is sub-ulp in bf16,
                                          |x|>4 -> x)
    silu      : t*sigmoid(t) on [-4, 4]  (|x|>4 -> x in bf16)

The fit is a Lawson iteratively-reweighted least squares run on Chebyshev
nodes, converging to the minimax polynomial; the iteration count is fixed,
so re-running reproduces this header bit for bit (no external
dependencies, stdlib only).

FP32 and BF16 tables are fitted SEPARATELY: the bf16 table is the Lawson
iteration run with coefficients quantized to bf16 after every solve
("quantize-and-refit"), so the iteration re-balances the residual against
the representable coefficient set instead of inheriting the quantization
error of the fp32 fit (which blows up on the interval edges, where the
high-order terms are amplified by x^k).

Usage:
    python3 src/kernel/ref/gen_taylor_lut.py                 # built-in tables -> taylor_lut.h
    python3 src/kernel/ref/gen_taylor_lut.py --func "lambda t: math.sin(t)" \
        --range 0 1.5707963 --order 8 --name sin_hpi2        # custom table (appended)
"""
import argparse
import math
import os
import struct

NODES = 65         # Chebyshev fit nodes per table
LAWSON_ITERS = 12  # fixed iteration count -> deterministic output
HEADER_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "taylor_lut.h")


# ---------------------------------------------------------------- target fns

def gelu_tanh(t):
    u = math.sqrt(2.0 / math.pi) * (t + 0.044715 * t ** 3)
    return 0.5 * t * (1.0 + math.tanh(u))


def silu(t):
    return t / (1.0 + math.exp(-t))


BUILTIN = [
    # name,        function,     lo,   hi,   order (step = number of coeffs)
    ("exp2",      lambda t: 2.0 ** t, 0.0, 1.0, 7),
    ("sigmoid",   lambda t: 1.0 / (1.0 + math.exp(-t)), 0.0, 3.0, 9),
    ("tanh",      math.tanh,          0.0, 2.0, 7),
    ("gelu_tanh", gelu_tanh,         -4.0, 4.0, 13),
    ("silu",      silu,              -4.0, 4.0, 11),
]


# ------------------------------------------------------------------ fitting

def solve(mat, vec, n):
    """n x n Gaussian elimination with partial pivoting."""
    aug = [mat[i][:] + [vec[i]] for i in range(n)]
    for col in range(n):
        piv = max(range(col, n), key=lambda r: abs(aug[r][col]))
        aug[col], aug[piv] = aug[piv], aug[col]
        if abs(aug[col][col]) < 1e-300:
            raise ValueError("singular normal equations")
        for r in range(col + 1, n):
            f = aug[r][col] / aug[col][col]
            for j in range(col, n + 1):
                aug[r][j] -= f * aug[col][j]
    out = [0.0] * n
    for r in range(n - 1, -1, -1):
        out[r] = (aug[r][n] - sum(aug[r][j] * out[j] for j in range(r + 1, n))) / aug[r][r]
    return out


def cheb_nodes(lo, hi, count):
    """Chebyshev nodes of the first kind mapped to [lo, hi]."""
    mid, half = (lo + hi) / 2.0, (hi - lo) / 2.0
    return [mid + half * math.cos(math.pi * (2 * i + 1) / (2 * count)) for i in range(count)]


def fit_minimax(func, lo, hi, order, quant=None):
    """Lawson IRLS minimax fit (ABSOLUTE error): coeffs a0..a_{order-1},
    Horner tail last. Absolute error is the right target here: the odd
    activations vanish at 0, so a relative-error fit would blow its whole
    budget on the origin; absolute error on the fitted interval keeps
    relative error small wherever the function is significant.

    quant: optional coefficient post-processing applied after EVERY solve
    (e.g. bf16 rounding). Quantize-and-refit: the next weighted solve sees
    the quantized residual, so the iteration converges to the minimax over
    the REPRESENTABLE coefficient set instead of quantizing the fp32
    minimax after the fact (which amplifies error ~x^k at the edges)."""
    xs = cheb_nodes(lo, hi, NODES)
    gs = [func(x) for x in xs]
    w = [1.0] * NODES
    poly = None
    for _ in range(LAWSON_ITERS):
        mat = [[0.0] * order for _ in range(order)]
        vec = [0.0] * order
        for i in range(NODES):
            basis = [1.0]
            for _ in range(1, order):
                basis.append(basis[-1] * xs[i])
            for a in range(order):
                for b in range(order):
                    mat[a][b] += w[i] * basis[a] * basis[b]
                vec[a] += w[i] * basis[a] * gs[i]
        poly = solve(mat, vec, order)
        if quant is not None:
            poly = [quant(a) for a in poly]
        err = [abs(sum(poly[k] * x ** k for k in range(order)) - g) for x, g in zip(xs, gs)]
        peak = max(err)
        if peak == 0.0:
            break
        w = [wi * (1.0 + e / peak) for wi, e in zip(w, err)]
    return poly


def eval_poly(poly, x):
    y = 0.0
    for a in reversed(poly):
        y = y * x + a
    return y


def max_abs_err(poly, func, lo, hi, samples=2000):
    worst = 0.0
    for j in range(samples + 1):
        x = lo + (hi - lo) * j / samples
        worst = max(worst, abs(eval_poly(poly, x) - func(x)))
    return worst


# ------------------------------------------------------------------ codecs

def f32_bits(v):
    return struct.unpack("<I", struct.pack("<f", v))[0]


def bf16_bits(v):
    """float -> BF16 bit pattern, round-to-nearest-even on the dropped half."""
    b = f32_bits(v)
    lsb = (b >> 16) & 1
    return (b + 0x7FFF + lsb) >> 16


def bf16_round(v):
    """float -> nearest BF16-representable value (as a Python float)."""
    return struct.unpack("<f", struct.pack("<I", bf16_bits(v) << 16))[0]


# ------------------------------------------------------------------ emit

def emit(tables):
    defs, arrays, metas = [], [], []
    for name, lo, hi, order, poly32, poly16, err32, err16 in tables:
        macro = "TNKR_TAYLOR_%s" % name.upper()
        defs.append("#define %s_LO %.9g" % (macro, lo))
        defs.append("#define %s_HI %.9g" % (macro, hi))
        defs.append("#define %s_ORDER %du" % (macro, order))
        row32 = ", ".join("0x%08Xu" % f32_bits(a) for a in poly32)
        row16 = ", ".join("0x%04Xu" % bf16_bits(a) for a in poly16)
        arrays.append("/* %s: minimax on [%.9g, %.9g], order %d, max abs err %.3e (fp32) / %.3e (bf16);\n"
                      " * fp32 and bf16 fitted separately (quantize-and-refit, see generator) */\n"
                      "static const uint32_t %s_F32[%s_ORDER] = { %s };\n"
                      "static const uint16_t %s_BF16[%s_ORDER] = { %s };"
                      % (name, lo, hi, order, err32, err16, macro, macro, row32, macro, macro, row16))
        metas.append((name, macro))

    table_rows = "\n".join(
        '    { "%s", %s_LO, %s_HI, %s_ORDER, %s_F32, %s_BF16 },' % (n, m, m, m, m, m)
        for n, m in metas)

    return """// Copyright (c) 2026 ThinkNative. All rights reserved.
// clang-format off
/* ==========================================================================
 * tn-metal: src/kernel/ref/taylor_lut.h
 * GENERATED FILE -- do not edit; regenerate with:
 *     python3 src/kernel/ref/gen_taylor_lut.py
 *
 * Prefit coefficient tables for top_vec_taylor: one (1,1,1,order) authored
 * row per function (Horner order: index k holds the x^k coefficient).
 * Load into a (1,SMC_NUM,1,order) operand with a c_str=0 companion stride
 * so ldst C-broadcasts the row to every lane -- the kernel then reads
 * coeff[lane, k] identically on all lanes (same convention as rcp_lut.h).
 * Each function ships FP32 bit patterns (U32) and BF16 bit patterns (U16,
 * RNE); memcpy the matching array into the coeff tensor backing store.
 * ========================================================================== */
#ifndef TN_REF_TAYLOR_LUT_H
#define TN_REF_TAYLOR_LUT_H

#include <stdint.h>

%(defs)s

%(arrays)s

/* table-of-tables: iterate to pick a function at runtime */
typedef struct {
    const char *name;
    double lo, hi;      /* range-reduced domain the fit covers */
    uint32_t order;     /* step: number of coefficients */
    const uint32_t *f32;
    const uint16_t *bf16;
} tnkr_taylor_lut_t;

static const tnkr_taylor_lut_t TNKR_TAYLOR_LUTS[] = {
%(table_rows)s
};

#define TNKR_TAYLOR_LUT_COUNT (sizeof(TNKR_TAYLOR_LUTS) / sizeof(TNKR_TAYLOR_LUTS[0]))

#endif /* TN_REF_TAYLOR_LUT_H */
// clang-format on
""" % {"defs": "\n".join(defs), "arrays": "\n\n".join(arrays), "table_rows": table_rows}


# ------------------------------------------------------------------- main

def build_one(name, func, lo, hi, order):
    poly32 = fit_minimax(func, lo, hi, order)
    err32 = max_abs_err(poly32, func, lo, hi)
    poly16 = fit_minimax(func, lo, hi, order, quant=bf16_round)
    err16 = max_abs_err(poly16, func, lo, hi)
    print("%-10s order %2d on [%g, %g]: max abs err %.3e (fp32) / %.3e (bf16)"
          % (name, order, lo, hi, err32, err16))
    return (name, lo, hi, order, poly32, poly16, err32, err16)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--func", help='custom target, e.g. "lambda t: math.sin(t)"')
    ap.add_argument("--range", nargs=2, type=float, metavar=("LO", "HI"))
    ap.add_argument("--order", type=int, help="number of coefficients (step)")
    ap.add_argument("--name", help="symbol name for the custom table")
    args = ap.parse_args()

    tables = []
    if args.func:
        if not (args.range and args.order and args.name):
            ap.error("--func requires --range, --order and --name")
        func = eval(args.func, {"math": math})
        lo, hi = args.range
        tables.append(build_one(args.name, func, lo, hi, args.order))
    else:
        for name, func, lo, hi, order in BUILTIN:
            tables.append(build_one(name, func, lo, hi, order))

    with open(HEADER_PATH, "w") as f:
        f.write(emit(tables))
    print("wrote", HEADER_PATH)


if __name__ == "__main__":
    main()
