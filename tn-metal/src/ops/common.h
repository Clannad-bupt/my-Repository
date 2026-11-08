// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/ops/common.h
 *
 * Ops-module shared helpers (internal header — the public ABI is
 * include/ops/, this is op-implementation plumbing):
 *   dim4      - (w,h,c,n) shape, innermost first, the kernel's tensor
 *               geometry, uint16 per axis
 *   TNOP_MIN_CONTIG_BYTES
 *             - the per-lane contiguous-transfer floor: one lane's
 *               single load/store moves at least this many contiguous
 *               bytes; the split holds every full chunk at or above it
 *               (a multi-operand op binds on the smallest dtype), only
 *               a final fragment may fall below
 *   TNOP_TRY  - propagate a non-success tn_status_t; the op dispatch
 *               releases op-scoped TDR slots wholesale, so an early
 *               return never leaks
 *   gtensor / gtensor_free / ltensor / ltensor_walign / ltensor_free /
 *   lconstant,
 *   set_addr / set_shape / set_dtype / set_value / set_bcast,
 *   tensor_release
 *             - the complete kernel/tensor.h TDR create/rewrite set
 *               (the shaped entries take dim4), plus the op-scope
 *               release for host-side fit probes; ltensor_walign is the
 *               conv-kernel placement (top_mm_conv_fp's kernel operand)
 *   get_elem_num / get_max_common_div
 *             - element-count and lane-mapping arithmetic
 *
 * Elementwise-family split: every operand shares one element count, so
 * the whole launch is one contiguous element range and the work is
 * re-chunked linearly instead of per stored axis. Each core takes an
 * equal contiguous share (the last one clamped to what remains, cores
 * past the launch idle) and stripes it over the greatest common lane
 * count its size allows — get_max_common_div against the smc count —
 * so every chunk, tail included, is an even stripe. The host fits the
 * one number the scalar half cannot derive, the w block s->B0, ppl
 * host-wrapper style: run this core's own launch per candidate block,
 * opening with the whole per-lane run and halving on the real
 * allocator's TN_ERR_NOMEM (create_urf, kernel/common/tensor.c, answers
 * past the lane window) — every candidate releases the op scope, so the
 * next faces a fresh window — TN_ERR_NOMEM when a minimum block cannot
 * fit. The scalar side runs the same body with the fitted block,
 * walking its share as a sequence of contiguous (cur_w, 1, c_slice)
 * tiles, so chunks cover the share exactly with no tail.
 * ========================================================================== */
#pragma once

#include <algorithm>
#include <stdint.h>

#include "kernel/chip.h" /* tnkr_spec_info for the split arithmetic */
#include "kernel/tensor.h"
#include "ops/params.h"
#include "tndef.h"

/* per-lane contiguous-transfer floor (see the banner) */
#define TNOP_MIN_CONTIG_BYTES 256u

struct dim4 {
    uint16_t w, h, c, n;
};

inline bool operator==(const dim4 &a, const dim4 &b) {
    return a.n == b.n && a.c == b.c && a.h == b.h && a.w == b.w;
}

inline bool operator!=(const dim4 &a, const dim4 &b) { return !(a == b); }

inline uint64_t get_elem_num(const tn_tensor_t *tensor) {
    auto elems = 1ull;
    for (uint32_t i = 0; i < tensor->ndims; ++i) {
        elems *= tensor->dims[i];
    }
    return elems;
}

/* the largest divisor of v not exceeding max_v: the largest lane
 * count a core's share stripes over evenly — every chunk of the
 * walk, tail included, is a whole number of equal per-lane runs
 * (ppl's gcd lane mapping) */
inline int get_max_common_div(int v, int max_v) {
    for (int i = max_v; i > 0; --i) {
        if (v % i == 0) {
            return i;
        }
    }
    return 1;
}

/* status propagation for op bodies: any tensor-helper failure exits the
 * op; op-scoped TDR slots are released by the dispatch, not here */
#define TNOP_TRY(call)                                                                             \
    do {                                                                                           \
        const tn_status_t tnop_st_ = (call);                                                       \
        if (tnop_st_ != TN_SUCCESS)                                                                \
            return tnop_st_;                                                                       \
    } while (0)

/* op-facing forms of kernel/tensor.h's TDR creators and field
 * rewrites — the complete set, so op code never names a tnkr_* entry:
 * the shaped entries take dim4 (its fields are uint16 by construction,
 * the split caps every programmed axis); the rest pass through */
inline tn_status_t gtensor(uint64_t addr, dim4 s, tn_dtype_t dt, tdr_idx_t *out) {
    return tnkr_gtensor(addr, s.n, s.c, s.h, s.w, dt, out);
}

inline tn_status_t gtensor_free(uint64_t addr, dim4 s, tn_dtype_t dt, const uint32_t strides[4],
                                tdr_idx_t *out, tdr_idx_t *out_str) {
    return tnkr_gtensor_free(addr, s.n, s.c, s.h, s.w, dt, strides, out, out_str);
}

inline tn_status_t ltensor(dim4 s, tn_dtype_t dt, tdr_idx_t *out) {
    return tnkr_ltensor(s.n, s.c, s.h, s.w, dt, out);
}

inline tn_status_t ltensor_walign(dim4 s, tn_dtype_t dt, tdr_idx_t *out) {
    return tnkr_ltensor_walign(s.n, s.c, s.h, s.w, dt, out);
}

inline tn_status_t ltensor_free(dim4 s, tn_dtype_t dt, const uint32_t strides[4], tdr_idx_t *out,
                                tdr_idx_t *out_str) {
    return tnkr_ltensor_free(s.n, s.c, s.h, s.w, dt, strides, out, out_str);
}

inline tn_status_t lconstant(tn_dtype_t dt, tnkr_imm_t v, tdr_idx_t *out) {
    return tnkr_lconstant(dt, v, out);
}

inline tn_status_t set_addr(tdr_idx_t idx, uint64_t addr) { return tnkr_set_addr(idx, addr); }

inline tn_status_t set_shape(tdr_idx_t idx, dim4 s) {
    return tnkr_set_shape(idx, s.n, s.c, s.h, s.w);
}

inline tn_status_t set_dtype(tdr_idx_t idx, tn_dtype_t dt) { return tnkr_set_dtype(idx, dt); }

inline tn_status_t set_value(tdr_idx_t idx, tnkr_imm_t v) { return tnkr_set_value(idx, v); }

inline tn_status_t set_bcast(tdr_idx_t idx, uint8_t m) { return tnkr_set_bcast(idx, m); }

/* op-scope release of every TDR slot and the whole URF bump: the
 * elementwise split's host-side fit probe calls it after every
 * candidate (the op dispatch releases after every op regardless) */
inline void tensor_release(void) { tnkr_tensor_release(); }