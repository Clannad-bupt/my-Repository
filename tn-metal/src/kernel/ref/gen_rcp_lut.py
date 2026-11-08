#!/usr/bin/env python3
"""Generate rcp_lut.h, the coefficient tables for top_vec_reciprocal.

For each segment k the mantissa interval [k/SEGS, (k+1)/SEGS) is fit with
a quadratic in the segment-local coordinate u = SEGS * (t - k/SEGS):

    1/(1+t) ~= A + B*u + C*u^2          (u in [0,1],  t = 1+x mantissa frac)

Coefficients are stored Q2.30 as S32: round(poly * 2**Q). A and C are
positive, B is always negative (1/(1+t) is convex decreasing). The fit is
a Lawson iteratively-reweighted least squares run on Chebyshev nodes,
which converges towards the minimax polynomial; the iteration count is
fixed, so re-running reproduces this header bit for bit (no external
dependencies, stdlib only).

Layout: SEGS entries of A, then SEGS of B, then SEGS of C.

Usage: python3 src/kernel/ref/gen_rcp_lut.py   (writes rcp_lut.h next to
this script; the header is the single shared table asset for the ref
kernel and the tests)
"""
import math
import os

SEGS = 128         # one segment per top-mantissa-bit combination
SEGS_BITS = 7      # SEGS == 1 << SEGS_BITS (mantissa bits forming the index)
Q = 30             # storage format Q2.30
NODES = 33         # Chebyshev fit nodes per segment
LAWSON_ITERS = 8   # fixed iteration count -> deterministic output


def solve3(mat, vec):
    """3x3 Gaussian elimination with partial pivoting."""
    aug = [row[:] + [vec[i]] for i, row in enumerate(mat)]
    for col in range(3):
        piv = max(range(col, 3), key=lambda r: abs(aug[r][col]))
        aug[col], aug[piv] = aug[piv], aug[col]
        for r in range(col + 1, 3):
            f = aug[r][col] / aug[col][col]
            for j in range(col, 4):
                aug[r][j] -= f * aug[col][j]
    out = [0.0] * 3
    for r in (2, 1, 0):
        out[r] = (aug[r][3] - sum(aug[r][j] * out[j] for j in range(r + 1, 3))) / aug[r][r]
    return out


def fit_segment(k):
    """Lawson minimax of g(u) = 1/(1 + k/SEGS + u/SEGS) on u in [0,1]."""
    t0 = k / SEGS
    xs = [(math.cos(math.pi * (2 * i + 1) / (2 * NODES)) + 1) / 2 for i in range(NODES)]
    gs = [1.0 / (1.0 + t0 + x / SEGS) for x in xs]
    w = [1.0] * NODES
    poly = None
    for _ in range(LAWSON_ITERS):
        mat = [[0.0] * 3 for _ in range(3)]
        vec = [0.0] * 3
        for i in range(NODES):
            basis = [1.0, xs[i], xs[i] * xs[i]]
            for a in range(3):
                for b in range(3):
                    mat[a][b] += w[i] * basis[a] * basis[b]
                vec[a] += w[i] * basis[a] * gs[i]
        poly = solve3(mat, vec)
        err = [abs(poly[0] + poly[1] * x + poly[2] * x * x - g) for x, g in zip(xs, gs)]
        peak = max(err)
        w = [wi * (1.0 + e / peak) for wi, e in zip(w, err)]
    return poly


def quant(v):
    q = int(round(v * (1 << Q)))
    if not (-(1 << 31) <= q < (1 << 31)):
        raise ValueError("coefficient out of S32 range: %r" % q)
    return q


def main():
    tab_a, tab_b, tab_c = [], [], []
    for k in range(SEGS):
        a, b, c = fit_segment(k)
        tab_a.append(quant(a))
        tab_b.append(quant(b))
        tab_c.append(quant(c))
    if not all(b < 0 for b in tab_b):
        raise ValueError("1/(1+t) is decreasing on [0,1): every B must be negative")
    if not all(a > 0 for a in tab_a) or not all(c > 0 for c in tab_c):
        raise ValueError("A > 0 and C > 0 expected on [0,1)")

    # post-quantization dense error scan (64 points per segment)
    worst = 0.0
    for k in range(SEGS):
        for j in range(64):
            u = j / 64.0
            f = (tab_a[k] + tab_b[k] * u + tab_c[k] * u * u) / (1 << Q)
            g = 1.0 / (1.0 + k / SEGS + u / SEGS)
            worst = max(worst, abs(f - g) / g)
    print("segments %d, Q2.%d, post-quant max rel err %.3e" % (SEGS, Q, worst))

    words = tab_a + tab_b + tab_c
    lines = []
    for i in range(0, len(words), 6):
        lines.append("    " + ", ".join("%12d" % w for w in words[i : i + 6]) + ",")
    body = "\n".join(lines)

    header = """// Copyright (c) 2026 ThinkNative. All rights reserved.
// clang-format off
/* ==========================================================================
 * tn-metal: src/kernel/ref/rcp_lut.h
 * GENERATED FILE -- do not edit; regenerate with:
 *     python3 src/kernel/ref/gen_rcp_lut.py
 *
 * Coefficient tables for top_vec_reciprocal: SEGS segments of a quadratic
 * minimax fit of 1/(1+t) in the segment-local coordinate u in [0,1),
 * stored Q2.30 as S32. Layout: A[SEGS] | B[SEGS] | C[SEGS]; the kernel
 * reads it through a (1,smc,1,TNKR_RCP_TBL_WORDS) S32 descriptor with the
 * same row broadcast to every lane. Shared by the ref kernel (layout
 * constants only) and any caller that loads the table as an operand.
 * ========================================================================== */
#ifndef TN_REF_RCP_LUT_H
#define TN_REF_RCP_LUT_H

#include <stdint.h>

#define TNKR_RCP_SEG_BITS %du
#define TNKR_RCP_SEGS (1u << TNKR_RCP_SEG_BITS)
#define TNKR_RCP_Q %d
#define TNKR_RCP_TBL_WORDS (3u * TNKR_RCP_SEGS)

static const int32_t TNKR_RCP_LUT[TNKR_RCP_TBL_WORDS] = {
%s
};

#endif /* TN_REF_RCP_LUT_H */
// clang-format on
""" % (SEGS_BITS, Q, body)

    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "rcp_lut.h")
    with open(out, "w") as f:
        f.write(header)
    print("wrote", out)


if __name__ == "__main__":
    main()
