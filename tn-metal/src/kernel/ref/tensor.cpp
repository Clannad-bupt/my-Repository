// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/tensor.cpp
 *
 * TensorView implementation: address mapping + element access +
 * traversal helpers (tnkr_view_copy)
 *
 * All formulas live in bit_addr().
 *
 * URF is a flat byte space split into smc_num equal-capacity lane windows:
 *   lane_bytes = urf_bytes / smc_num        // per-lane capacity
 *   lane l window base = l * lane_bytes     // bases follow capacity:
 *                                           // smc1 base = smc0 window end + 1
 * Each window is further split into bank_num banks
 * (bank_size = lane_bytes / bank_num, see kernel/chip.h tnkr_urf_bank_size).
 * The descriptor addr is an offset within a window (smc0 window base = 0).
 *
 * HWALIGN / ZEROSTRIDE (URF, lane placement):
 *   lane  = c % smc_num                 // channel -> lane direct map
 *   cb    = c / smc_num                 // c-block
 *   plane = align_up(h*w*dtype_bits, TDR_URF_WIDTH_BYTES*8)
 *         // plane start addresses align to TDR_URF_WIDTH_BYTES; h*w itself
 *         // need not be a multiple — the shortfall is padding
 *   byte  = lane*lane_bytes + addr + (n*cblocks + cb)*plane
 *         + (h*W + w) * dtype_bits          // bit granularity; sub-byte
 *                                           // element 0 sits in low bits
 *   Planes within one lane stack in [n][cb] order.
 *
 * ZEROSTRIDE (broadcast): in the mapping above, coordinates of
 * bcast-flagged dims contribute zero (stride 0).
 *   Broadcast dims store a fixed extent (kernel/common/tdr.h, enforced by resolve):
 *   C == smc_num, N/H/W == 1.
 *   C-dim lane locality: channel c is pinned to lane c % smc_num; beyond
 *   smc_num it wraps to the next c-block of the same lane — there is no
 *   data path between SMCs. CB reads wrap to this lane's c-block 0, so a
 *   broadcast view (2,smc_num,3,4) -> (2,n*smc_num,3,4) reuses data by
 *   lane wraparound. A C broadcast with dc=1 would need cross-SMC
 *   transfer and is illegal (rejected by resolve).
 *
 * CONSTANT (URF): addr[31:0] IS the bit pattern, no memory access.
 *
 * CONTINUOUS (GLOBAL): packed NCHW row-major,
 *   bit_addr = addr*8 + (((n*C+c)*H+h)*W+w) * dtype_bits.
 * FREE (both sides): companion stride TDR, 32-bit strides counted in
 * ELEMENTS (dtype-sized units; the tensor layer scales by dtype size).
 *   GLOBAL: flat byte space, freely configurable except W — the spec
 *   pins wstride to 1 on the external side (W is the contiguous dim;
 *   resolve rejects ws != 1 with TNKR_RESOLVE_FREE_WSTRIDE). A stride of 0
 *   broadcasts that dim (e.g. c_str=0 materializes the per-lane C
 *   replication at load time):
 *     byte = addr + (n*nstr + c*cstr + h*hstr + w*wstr) * esz,  esz = dtype
 *     size in bytes.
 *   URF: the lane law of HWALIGN applies unchanged — lane = c % smc_num,
 *   addr is a window-internal offset, and every access stays inside
 *   lane(c)'s window; data never crosses SMCs. cs strides c-blocks
 *   within the lane:
 *     byte = lane*lane_bytes + addr + (n*nstr + (c/smc_num)*cstr
 *          + h*hstr + w*wstr) * esz
 *   cs=0 wraps c-blocks to 0 — lane-local C broadcast reads, the same
 *   semantics as ZEROSTRIDE CB (broadcast sources are physically
 *   replicated per lane at load time). FREE with sub-byte dtypes is
 *   undefined — treated as unsupported.
 *   Lane-window bound: a URF tensor's whole footprint must fit its lane
 *   window (addr + max offset <= lane_bytes, enforced once by resolve) —
 *   otherwise one lane's tensor would spill into the next lane's
 *   storage, breaking lane independence.
 *
 * WALIGN (URF, conv kernel): lane law identical to HWALIGN (lane =
 * c % smc_num, planes stack [n][cb]); inside a plane each h row starts
 * at an urf-width boundary instead of rows packing into one aligned
 * plane:
 *   row   = align_up(w*dtype_bits, TDR_URF_WIDTH_BYTES*8)
 *           // per-row alignment: the global->URF load materializes the
 *           // row gap; the gap is dead space no element maps into
 *   byte  = lane*lane_bytes + addr + (n*cblocks + cb)*h*row
 *         + h*row + w*dtype_bits                // bit granularity
 *
 * Resolve failure modes map to TNKR_RESOLVE_* statuses (see tensor.h);
 * engines turn them into violations (violate.h). The per-element
 * mem_cap checks below are a defensive last line: once the resolve
 * footprint checks pass they are unreachable, so firing one is itself
 * a violation, never silent data corruption.
 * ========================================================================== */
#include "tensor.h"

#include "dtype.h"
#include "violate.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- violation sink (ref backend; see violate.h) ---- */
static tnkr_ref_violation_fn g_violation_fn = NULL;

void tnkr_ref_set_violation_handler(tnkr_ref_violation_fn fn) { g_violation_fn = fn; }

void tnkr_ref_violation(const char *op, const char *file, int line, const char *fmt, ...) {
    char msg[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    if (g_violation_fn != NULL) {
        g_violation_fn(op, file, line, msg);
        return; /* handler returned instead of aborting/longjmp'ing */
    }
    fprintf(stderr, "tn-metal ref VIOLATION %s:%d (%s): %s\n", file, line, op, msg);
    abort();
}

const char *tnkr_resolve_status_name(tnkr_resolve_status_t st) {
    switch (st) {
    case TNKR_RESOLVE_OK:
        return "ok";
    case TNKR_RESOLVE_NO_CTX:
        return "context not initialized";
    case TNKR_RESOLVE_BAD_IDX:
        return "tdr index out of range";
    case TNKR_RESOLVE_UNCONFIGURED:
        return "tdr slot unconfigured or holds a stride";
    case TNKR_RESOLVE_RESERVED_DT:
        return "reserved dtype encoding";
    case TNKR_RESOLVE_LOC:
        return "illegal storage location";
    case TNKR_RESOLVE_LAYOUT:
        return "illegal layout value";
    case TNKR_RESOLVE_NO_STRIDE:
        return "FREE layout lacks its companion stride";
    case TNKR_RESOLVE_ZS_EXTENT:
        return "ZEROSTRIDE stored-extent violation";
    case TNKR_RESOLVE_FREE_WSTRIDE:
        return "GLOBAL FREE wstride != 1";
    case TNKR_RESOLVE_FOOTPRINT:
        return "footprint over lane window / global capacity";
    default:
        return "unknown resolve status";
    }
}

namespace {

/* Bit address of element (n,c,h,w); false = unsupported layout (caller
 * treats as out of bounds) */
bool bit_addr(const tnkr_view_t *v, uint32_t n, uint32_t c, uint32_t h, uint32_t w, uint64_t *out) {
    const uint64_t bits = v->dtype_bits;

    switch (v->layout) {
    case TDR_LAYOUT_HWALIGN:
    case TDR_LAYOUT_ZEROSTRIDE: {
        const bool zs = (v->layout == TDR_LAYOUT_ZEROSTRIDE);
        const uint32_t nn = (zs && (v->bcast & TDR_BCAST_NB)) ? 0 : n;
        const uint32_t hh = (zs && (v->bcast & TDR_BCAST_HB)) ? 0 : h;
        const uint32_t ww = (zs && (v->bcast & TDR_BCAST_WB)) ? 0 : w;
        const uint64_t lane = c % v->smc_num;
        /* CB: cb is always 0 — resolve guarantees dc == smc_num, so
         * broadcast reads wrap to this lane's c-block 0; no cross-SMC
         * access */
        const uint64_t cb = (zs && (v->bcast & TDR_BCAST_CB)) ? 0 : (c / v->smc_num);
        const uint64_t cblocks = ((uint64_t)v->dc + v->smc_num - 1) / v->smc_num;
        const uint64_t plane_bytes =
            (((uint64_t)v->dh * v->dw * bits + TDR_URF_WIDTH_BYTES * 8 - 1) /
             (TDR_URF_WIDTH_BYTES * 8)) *
            TDR_URF_WIDTH_BYTES;

        *out = (lane * v->lane_bytes + v->addr + (nn * cblocks + cb) * plane_bytes) * 8 +
               ((uint64_t)hh * v->dw + ww) * bits;
        return true;
    }
    case TDR_LAYOUT_WALIGN: {
        /* Conv kernel placement: per-row urf-width alignment. Same lane
         * law as HWALIGN; rows are separated by a dead gap the load
         * materializes — no element maps into it (w < dw only). */
        const uint64_t lane = c % v->smc_num;
        const uint64_t cb = c / v->smc_num;
        const uint64_t cblocks = ((uint64_t)v->dc + v->smc_num - 1) / v->smc_num;
        const uint64_t row_bytes =
            (((uint64_t)v->dw * bits + TDR_URF_WIDTH_BYTES * 8 - 1) / (TDR_URF_WIDTH_BYTES * 8)) *
            TDR_URF_WIDTH_BYTES;
        const uint64_t plane_bytes = (uint64_t)v->dh * row_bytes;

        *out = (lane * v->lane_bytes + v->addr + ((uint64_t)n * cblocks + cb) * plane_bytes) * 8 +
               (uint64_t)h * row_bytes * 8 + (uint64_t)w * bits;
        return true;
    }
    case TDR_LAYOUT_CONTINUOUS: {
        const uint64_t idx = (((uint64_t)n * v->dc + c) * v->dh + h) * v->dw + w;
        *out = v->addr * 8 + idx * bits;
        return true;
    }
    case TDR_LAYOUT_FREE: {
        uint64_t ns, cs, hs, ws, esz, base;
        if (bits < 8 || v->stride == nullptr)
            return false; /* sub-byte FREE is undefined */
        ns = tdr_rd(v->stride->raw, TDR_NSTR_LO, TDR_NSTR_MASK);
        cs = tdr_rd(v->stride->raw, TDR_CSTR_LO, TDR_CSTR_MASK);
        hs = tdr_rd(v->stride->raw, TDR_HSTR_LO, TDR_HSTR_MASK);
        ws = tdr_rd(v->stride->raw, TDR_WSTR_LO, TDR_WSTR_MASK);
        esz = bits / 8; /* strides count ELEMENTS; scale by the dtype size */
        if (v->loc == TDR_LOC_URF) {
            /* Lane law: same C->lane mapping as HWALIGN; strides act only
             * inside lane(c)'s window, cs strides c-blocks within the lane.
             * cs=0 wraps c-blocks to 0 — lane-local broadcast read, no
             * cross-SMC access. */
            base = (uint64_t)(c % v->smc_num) * v->lane_bytes + v->addr +
                   (n * ns + ((uint64_t)c / v->smc_num) * cs + h * hs + w * ws) * esz;
        } else {
            base = v->addr + (n * ns + c * cs + h * hs + w * ws) * esz;
        }
        *out = base * 8;
        return true;
    }
    default:
        return false; /* illegal layout value */
    }
}

} // anonymous namespace

tnkr_resolve_status_t tnkr_view_resolve(tnkr_ctx_t *ctx, tdr_idx_t di, tdr_idx_t si,
                                        tnkr_view_t *out) {
    const tdr_t *d;

    if (ctx == nullptr || ctx->info.smc_num == 0 || di >= ctx->info.tdr_num)
        return (ctx == nullptr) ? TNKR_RESOLVE_NO_CTX : TNKR_RESOLVE_BAD_IDX;
    if (!TNKR_BIT_TEST(ctx->valid, di) || TNKR_BIT_TEST(ctx->is_stride, di))
        return TNKR_RESOLVE_UNCONFIGURED; /* unconfigured / holds a stride */

    d = &ctx->shadow[di].desc;
    out->ctx = ctx;
    out->desc = d;
    out->stride = nullptr;
    out->dtype = tdr_get_dtype(d);
    out->layout = tdr_get_layout(d);
    out->loc = tdr_get_loc(d);
    out->bcast = tdr_get_bcast(d);
    out->dn = tdr_get_n(d);
    out->dc = tdr_get_c(d);
    out->dh = tdr_get_h(d);
    out->dw = tdr_get_w(d);
    out->addr = tdr_get_addr(d);
    out->smc_num = ctx->info.smc_num;
    out->lane_bytes = ctx->info.urf_bytes / ctx->info.smc_num;
    out->dtype_bits = tn_dtype_bits(out->dtype);
    if (out->dtype_bits == 0)
        return TNKR_RESOLVE_RESERVED_DT; /* reserved dtype */

    /* layout x loc legality (mirrors tdr_check; engines never run
     * tdr_check, so resolve is the single enforcement point here) */
    switch (out->layout) {
    case TDR_LAYOUT_HWALIGN:
    case TDR_LAYOUT_ZEROSTRIDE:
    case TDR_LAYOUT_WALIGN:
    case TDR_LAYOUT_CONSTANT:
        if (out->loc != TDR_LOC_URF)
            return TNKR_RESOLVE_LOC;
        break;
    case TDR_LAYOUT_CONTINUOUS:
        if (out->loc != TDR_LOC_GLOBAL)
            return TNKR_RESOLVE_LOC;
        break;
    case TDR_LAYOUT_FREE:
        break; /* legal on both sides */
    default:
        return TNKR_RESOLVE_LAYOUT; /* layout value not defined */
    }

    if (out->layout == TDR_LAYOUT_CONSTANT) {
        out->mem = nullptr; /* constant lives in addr[31:0], no memory */
        out->mem_cap = 0;
        return TNKR_RESOLVE_OK;
    }
    switch (out->loc) {
    case TDR_LOC_URF:
        out->mem = ctx->urf;
        out->mem_cap = ctx->info.urf_bytes;
        break;
    case TDR_LOC_GLOBAL:
        out->mem = ctx->global;
        out->mem_cap = TNKR_SIM_GLOBAL_BYTES;
        break;
    default:
        return TNKR_RESOLVE_LOC;
    }
    if (out->layout == TDR_LAYOUT_FREE) {
        if (si >= ctx->info.tdr_num || !TNKR_BIT_TEST(ctx->valid, si) ||
            !TNKR_BIT_TEST(ctx->is_stride, si))
            return TNKR_RESOLVE_NO_STRIDE; /* FREE lacks its companion stride */
        out->stride = &ctx->shadow[si].stride;
        if (out->loc == TDR_LOC_GLOBAL && tdr_rd(out->stride->raw, TDR_WSTR_LO, TDR_WSTR_MASK) != 1)
            return TNKR_RESOLVE_FREE_WSTRIDE;
    }
    /* ZEROSTRIDE broadcast dims store a fixed extent (kernel/common/tdr.h): C == smc_num
     * (lane locality), N/H/W == 1. Engines never run tdr_check, and raw
     * tdr_write can bypass host validation — illegal operands are rejected
     * here, uniformly */
    if (out->layout == TDR_LAYOUT_ZEROSTRIDE) {
        if ((out->bcast & TDR_BCAST_CB) && out->dc != (uint16_t)out->smc_num)
            return TNKR_RESOLVE_ZS_EXTENT; /* C broadcast requires dc == smc_num */
        if ((out->bcast & TDR_BCAST_NB) && out->dn != 1)
            return TNKR_RESOLVE_ZS_EXTENT;
        if ((out->bcast & TDR_BCAST_HB) && out->dh != 1)
            return TNKR_RESOLVE_ZS_EXTENT;
        if ((out->bcast & TDR_BCAST_WB) && out->dw != 1)
            return TNKR_RESOLVE_ZS_EXTENT;
    }
    /* Footprint bound, checked once here (dims and strides are static at
     * resolve time):
     *   URF: the whole footprint must fit the window dictated by the
     *        C->lane mapping — otherwise one lane's tensor spills into
     *        the next lane's storage, breaking lane independence;
     *   GLOBAL: flat capacity bound (symmetric to the URF check; without
     *        it an oversized descriptor would fail mid-run with silent
     *        per-element truncation). */
    {
        const uint64_t cblocks = ((uint64_t)out->dc + out->smc_num - 1) / out->smc_num;
        uint64_t span;

        if (out->layout == TDR_LAYOUT_FREE) {
            const uint64_t ns = tdr_rd(out->stride->raw, TDR_NSTR_LO, TDR_NSTR_MASK);
            const uint64_t cs = tdr_rd(out->stride->raw, TDR_CSTR_LO, TDR_CSTR_MASK);
            const uint64_t hs = tdr_rd(out->stride->raw, TDR_HSTR_LO, TDR_HSTR_MASK);
            const uint64_t ws = tdr_rd(out->stride->raw, TDR_WSTR_LO, TDR_WSTR_MASK);
            /* strides count elements: element span scaled by the dtype size */
            span = (((uint64_t)out->dn - 1) * ns + (cblocks - 1) * cs +
                    ((uint64_t)out->dh - 1) * hs + ((uint64_t)out->dw - 1) * ws + 1) *
                   (out->dtype_bits / 8);
            if (out->loc == TDR_LOC_GLOBAL)
                span = (((uint64_t)out->dn - 1) * ns + ((uint64_t)out->dc - 1) * cs +
                        ((uint64_t)out->dh - 1) * hs + ((uint64_t)out->dw - 1) * ws + 1) *
                       (out->dtype_bits / 8);
        } else if (out->layout == TDR_LAYOUT_WALIGN) {
            /* per-row aligned planes stacked [n][cb]; footprint counts
             * the row gaps the load materializes */
            const uint64_t row_bytes =
                (((uint64_t)out->dw * out->dtype_bits + TDR_URF_WIDTH_BYTES * 8 - 1) /
                 (TDR_URF_WIDTH_BYTES * 8)) *
                TDR_URF_WIDTH_BYTES;
            span = (uint64_t)out->dn * cblocks * (uint64_t)out->dh * row_bytes;
        } else if (out->layout == TDR_LAYOUT_CONTINUOUS) {
            span = ((uint64_t)out->dn * out->dc * out->dh * out->dw * out->dtype_bits + 7) / 8;
        } else { /* HWALIGN / ZEROSTRIDE: aligned planes stacked [n][cb] */
            const uint64_t plane_bytes =
                (((uint64_t)out->dh * out->dw * out->dtype_bits + TDR_URF_WIDTH_BYTES * 8 - 1) /
                 (TDR_URF_WIDTH_BYTES * 8)) *
                TDR_URF_WIDTH_BYTES;
            span = (uint64_t)out->dn * cblocks * plane_bytes;
        }
        /* URF: bound is the lane window; GLOBAL: the flat capacity */
        {
            const uint64_t cap = (out->loc == TDR_LOC_URF) ? out->lane_bytes : out->mem_cap;
            if (out->addr + span > cap)
                return TNKR_RESOLVE_FOOTPRINT;
        }
    }
    return TNKR_RESOLVE_OK;
}

uint64_t tnkr_view_read(const tnkr_view_t *v, uint32_t n, uint32_t c, uint32_t h, uint32_t w) {
    uint64_t ba, byte, raw = 0;
    unsigned off, nbytes, i;

    if (v->layout == TDR_LAYOUT_CONSTANT)
        return v->addr & ((1ull << v->dtype_bits) - 1ull);

    if (!bit_addr(v, n, c, h, w, &ba)) {
        /* unreachable once resolve rejects undefined layouts — firing it
         * means an addressing bug, never silent zeros */
        tnkr_ref_violation("tensor", __FILE__, __LINE__,
                           "bit_addr: unsupported layout (resolve should have rejected)");
        return 0;
    }
    byte = ba / 8;
    off = (unsigned)(ba % 8);
    nbytes = (off + v->dtype_bits + 7) / 8;
    if (byte + nbytes > v->mem_cap) {
        /* defensive last line: resolve footprint checks make this
         * unreachable; firing it is an addressing bug */
        tnkr_ref_violation("tensor", __FILE__, __LINE__,
                           "element read OOB: byte %llu + %u over cap %llu",
                           (unsigned long long)byte, nbytes, (unsigned long long)v->mem_cap);
        return 0;
    }

    for (i = 0; i < nbytes; i++) /* little-endian assembly */
        raw |= (uint64_t)v->mem[byte + i] << (8 * i);
    return (raw >> off) & ((1ull << v->dtype_bits) - 1ull);
}

void tnkr_view_write(const tnkr_view_t *v, uint32_t n, uint32_t c, uint32_t h, uint32_t w,
                     uint64_t bits) {
    uint64_t ba, byte, raw = 0, mask;
    unsigned off, nbytes, i;

    if (v->layout == TDR_LAYOUT_CONSTANT)
        return; /* constants are not writable */

    if (!bit_addr(v, n, c, h, w, &ba)) {
        tnkr_ref_violation("tensor", __FILE__, __LINE__,
                           "bit_addr: unsupported layout (resolve should have rejected)");
        return;
    }
    byte = ba / 8;
    off = (unsigned)(ba % 8);
    nbytes = (off + v->dtype_bits + 7) / 8;
    if (byte + nbytes > v->mem_cap) {
        tnkr_ref_violation("tensor", __FILE__, __LINE__,
                           "element write OOB: byte %llu + %u over cap %llu",
                           (unsigned long long)byte, nbytes, (unsigned long long)v->mem_cap);
        return; /* fail-safe after the violation */
    }

    for (i = 0; i < nbytes; i++) /* read-modify-write, neighbors preserved */
        raw |= (uint64_t)v->mem[byte + i] << (8 * i);
    mask = ((1ull << v->dtype_bits) - 1ull) << off;
    raw = (raw & ~mask) | ((bits << off) & mask);
    for (i = 0; i < nbytes; i++)
        v->mem[byte + i] = (uint8_t)(raw >> (8 * i));
}

/* ---- shape pairing (rules documented in tensor.h) ----
 * Single definition point; vec_move reports an illegal pairing under its
 * own op name, tnkr_view_copy enforces it defensively as "view_copy" */
bool tnkr_view_copy_shape_ok(const tnkr_view_t *dst, const tnkr_view_t *src, bool nc_trans) {
    if (nc_trans)
        return dst->dn == src->dc && dst->dc == src->dn && dst->dh == src->dh && dst->dw == src->dw;
    if (src->layout == TDR_LAYOUT_CONSTANT)
        return true; /* any dst shape: reads return the constant pattern */
    if (src->layout == TDR_LAYOUT_ZEROSTRIDE) {
        /* broadcast dims read with stride 0 — any dst extent (stored
         * extents are already pinned by resolve: 1, C == smc_num);
         * non-broadcast dims must match */
        return ((src->bcast & TDR_BCAST_NB) || src->dn == dst->dn) &&
               ((src->bcast & TDR_BCAST_CB) || src->dc == dst->dc) &&
               ((src->bcast & TDR_BCAST_HB) || src->dh == dst->dh) &&
               ((src->bcast & TDR_BCAST_WB) || src->dw == dst->dw);
    }
    if (src->layout == TDR_LAYOUT_FREE && src->stride != NULL) {
        /* strides are arbitrary 32-bit element counts: a nonzero stride
         * requires the extent to match dst, stride 0 broadcasts the dim
         * to any dst extent. Lane-law exception on URF: the C lane is
         * fixed (c % smc_num) and cs strides only c-blocks, so a cs=0
         * C broadcast is sound only when the source's c-block 0 covers
         * every lane the dst touches — a narrower source would read
         * lanes holding no src data. GLOBAL sources are flat: stride 0
         * is a true broadcast there. */
        const uint64_t ns = tdr_rd(src->stride->raw, TDR_NSTR_LO, TDR_NSTR_MASK);
        const uint64_t cs = tdr_rd(src->stride->raw, TDR_CSTR_LO, TDR_CSTR_MASK);
        const uint64_t hs = tdr_rd(src->stride->raw, TDR_HSTR_LO, TDR_HSTR_MASK);
        const uint64_t ws = tdr_rd(src->stride->raw, TDR_WSTR_LO, TDR_WSTR_MASK);
        bool c_ok;

        if (cs != 0)
            c_ok = src->dc == dst->dc;
        else if (src->loc != TDR_LOC_URF)
            c_ok = true;
        else {
            const uint32_t s_cov = src->dc < src->smc_num ? src->dc : src->smc_num;
            const uint32_t d_cov = dst->dc < dst->smc_num ? dst->dc : dst->smc_num;
            c_ok = s_cov >= d_cov;
        }
        return (ns == 0 || src->dn == dst->dn) && c_ok && (hs == 0 || src->dh == dst->dh) &&
               (ws == 0 || src->dw == dst->dw);
    }
    return tnkr_view_same_shape(dst, src);
}

void tnkr_view_copy(tnkr_view_t *dst, const tnkr_view_t *src, bool nc_trans) {
    uint32_t n, c, h, w;

    if (!tnkr_view_copy_shape_ok(dst, src, nc_trans))
        TNKR_REF_VIOLATE("view_copy", "dst/src shape pairing illegal for this layout/mode");

    for (n = 0; n < dst->dn; n++)
        for (c = 0; c < dst->dc; c++)
            for (h = 0; h < dst->dh; h++)
                for (w = 0; w < dst->dw; w++) {
                    uint32_t sn = nc_trans ? c : n;
                    uint32_t sc = nc_trans ? n : c;
                    if (nc_trans && (sn >= src->dn || sc >= src->dc))
                        continue; /* outside source shape: skip */
                    tnkr_view_write(dst, n, c, h, w, tnkr_view_read(src, sn, sc, h, w));
                }
}

/* ---- gather / scatter (shared core of the vec/ldst twins and dmr) ---- */

/* dtype set of the value-comparing dmr families (kernel.h): shared by the
 * indexed-pick core here and the sort family in dmr.cpp */
bool tnkr_pick_dtype_ok(tn_dtype_t dt) {
    switch (dt) {
    case TN_DTYPE_FP8_E4M3:
    case TN_DTYPE_BF16:
    case TN_DTYPE_FP32:
    case TN_DTYPE_S8:
    case TN_DTYPE_S16:
    case TN_DTYPE_S32:
    case TN_DTYPE_U8:
    case TN_DTYPE_U16:
    case TN_DTYPE_U32:
        return true;
    default:
        return false;
    }
}

namespace {

/* per-operand placement: URF -> HWAlign; Global (dmr variants only) ->
 * Continuous, plus FREE for the in/index operands (companion strides ride
 * the call); the output stays Continuous on Global per spec */
void gs_check_place(const char *op, const tnkr_view_t *v, bool allow_global, bool allow_free) {
    if (v->loc == TDR_LOC_URF) {
        TNKR_REF_CHECK(op, v->layout == TDR_LAYOUT_HWALIGN, "operands must be HWAlign on URF");
        return;
    }
    TNKR_REF_CHECK(
        op,
        allow_global && v->loc == TDR_LOC_GLOBAL &&
            (v->layout == TDR_LAYOUT_CONTINUOUS || (allow_free && v->layout == TDR_LAYOUT_FREE)),
        allow_global ? "operand placement not allowed on Global" : "operands must live in URF");
}

/* operand rules identical for gather and scatter: placement per
 * gs_check_place (out never FREE); in/out the same supported dtype;
 * index U8/U16/U32 */
void gs_common_check(const char *op, const tnkr_view_t *out, const tnkr_view_t *in,
                     const tnkr_view_t *idx, const gs_opts_t *opts) {
    gs_check_place(op, out, opts->allow_global, false);
    gs_check_place(op, in, opts->allow_global, opts->allow_free);
    gs_check_place(op, idx, opts->allow_global, opts->allow_free);
    TNKR_REF_CHECK(op, in->dtype == out->dtype && tnkr_pick_dtype_ok(in->dtype),
                   "in/out dtype not in the supported set or mismatched");
    TNKR_REF_CHECK(
        op, idx->dtype == TN_DTYPE_U8 || idx->dtype == TN_DTYPE_U16 || idx->dtype == TN_DTYPE_U32,
        "index dtype not U8/U16/U32");
}

/* OOB fill pattern: zero / one / -1 — for unsigned dtypes -1 is the
 * width-truncated all-ones pattern, its natural encoding; KEEP writes
 * nothing and is handled by the caller */
uint64_t gs_fill(tn_dtype_t dt, pad_val_t value) {
    if (tnkr_dt_is_fp(dt)) {
        if (value == PAD_VAL_ONE)
            return tnkr_encode_fp(1.0, dt);
        if (value == PAD_VAL_MINUS_ONE)
            return tnkr_encode_fp(-1.0, dt);
        return 0;
    }
    if (value == PAD_VAL_ONE)
        return 1;
    if (value == PAD_VAL_MINUS_ONE)
        return (1ull << tn_dtype_bits(dt)) - 1ull;
    return 0;
}

} // anonymous namespace

void tnkr_gather_ex(tnkr_view_t *out, const tnkr_view_t *in, const tnkr_view_t *idx,
                    axis_mode_t mode, pad_val_t value, const gs_opts_t *opts) {
    uint32_t n, c, k, x, free_len, span;
    uint64_t fill, coord;

    /* unsupported mode / pad-value encodings are violations */
    TNKR_REF_CHECK("gather", mode == AXIS_MODE_W || mode == AXIS_MODE_H, "mode must be W or H");
    TNKR_REF_CHECK("gather",
                   value == PAD_VAL_ZERO || value == PAD_VAL_ONE || value == PAD_VAL_MINUS_ONE ||
                       value == PAD_VAL_KEEP,
                   "pad value encoding unsupported");
    gs_common_check("gather", out, in, idx, opts);
    /* output shape: the picked axis becomes K, the rest stays input's */
    TNKR_REF_CHECK("gather", out->dn == in->dn && out->dc == in->dc,
                   "output N/C must match the input");
    if (mode == AXIS_MODE_W) {
        TNKR_REF_CHECK("gather", out->dh == in->dh && out->dw == idx->dw,
                       "output H must match input H, W must match index K");
    } else {
        TNKR_REF_CHECK("gather", out->dh == idx->dw && out->dw == in->dw,
                       "output H must match index K, W must match input W");
    }
    TNKR_REF_CHECK("gather", idx->dn == 1 && idx->dh == 1 && idx->dc == in->dc,
                   "index shape must be (1,C,1,K) with the input's channel count");

    fill = gs_fill(out->dtype, value);
    free_len = (mode == AXIS_MODE_W) ? out->dh : out->dw;
    span = (mode == AXIS_MODE_W) ? in->dw : in->dh;
    for (n = 0; n < out->dn; n++)
        for (c = 0; c < out->dc; c++)
            for (k = 0; k < idx->dw; k++) {
                coord = tnkr_view_read(idx, 0, c, 0, k);
                if (coord >= span) {
                    if (value == PAD_VAL_KEEP)
                        continue; /* keep: the output elements stay */
                    for (x = 0; x < free_len; x++)
                        if (mode == AXIS_MODE_W)
                            tnkr_view_write(out, n, c, x, k, fill);
                        else
                            tnkr_view_write(out, n, c, k, x, fill);
                    continue;
                }
                for (x = 0; x < free_len; x++)
                    if (mode == AXIS_MODE_W)
                        tnkr_view_write(out, n, c, x, k,
                                        tnkr_view_read(in, n, c, x, (uint32_t)coord));
                    else
                        tnkr_view_write(out, n, c, k, x,
                                        tnkr_view_read(in, n, c, (uint32_t)coord, x));
            }
}

void tnkr_gather(tnkr_view_t *out, const tnkr_view_t *in, const tnkr_view_t *idx, axis_mode_t mode,
                 pad_val_t value) {
    const gs_opts_t twin = {false, false, false, false};

    tnkr_gather_ex(out, in, idx, mode, value, &twin);
}

void tnkr_scatter_ex(tnkr_view_t *out, const tnkr_view_t *in, const tnkr_view_t *idx,
                     axis_mode_t mode, const gs_opts_t *opts) {
    uint32_t n, c, k, x, free_len, span, idx_len;
    uint64_t coord;

    /* unsupported mode encodings are violations */
    TNKR_REF_CHECK("scatter", mode == AXIS_MODE_W || mode == AXIS_MODE_H, "mode must be W or H");
    gs_common_check("scatter", out, in, idx, opts);
    /* input is the compacted side: its picked extent equals K, the
     * other axes match the expanded output; the index vector rides the
     * W axis — (1,C,1,K) — except the dmr row-scatter (mode H, row_idx),
     * which carries it on the H axis: (1,C,K,1) */
    TNKR_REF_CHECK("scatter", out->dn == in->dn && out->dc == in->dc,
                   "output N/C must match the input");
    if (mode == AXIS_MODE_W) {
        TNKR_REF_CHECK("scatter", in->dw == idx->dw && out->dh == in->dh,
                       "input W must match index K, output H must match input H");
    } else if (opts->row_idx) {
        TNKR_REF_CHECK("scatter", in->dh == idx->dh && out->dw == in->dw,
                       "input H must match index K, output W must match input W");
    } else {
        TNKR_REF_CHECK("scatter", in->dh == idx->dw && out->dw == in->dw,
                       "input H must match index K, output W must match input W");
    }
    if (mode == AXIS_MODE_H && opts->row_idx)
        TNKR_REF_CHECK("scatter", idx->dn == 1 && idx->dw == 1 && idx->dc == in->dc,
                       "index shape must be (1,C,K,1) with the input's channel count");
    else
        TNKR_REF_CHECK("scatter", idx->dn == 1 && idx->dh == 1 && idx->dc == in->dc,
                       "index shape must be (1,C,1,K) with the input's channel count");

    idx_len = (mode == AXIS_MODE_H && opts->row_idx) ? idx->dh : idx->dw;
    free_len = (mode == AXIS_MODE_W) ? in->dh : in->dw;
    span = (mode == AXIS_MODE_W) ? out->dw : out->dh;
    for (n = 0; n < in->dn; n++)
        for (c = 0; c < in->dc; c++)
            for (k = 0; k < idx_len; k++) {
                coord = (mode == AXIS_MODE_H && opts->row_idx) ? tnkr_view_read(idx, 0, c, k, 0)
                                                               : tnkr_view_read(idx, 0, c, 0, k);
                if (coord >= span)
                    continue; /* OOB coordinate: dropped */
                /* ascending k: duplicate coordinates resolve
                 * last-write-wins (reduce_add: every k accumulates) */
                for (x = 0; x < free_len; x++)
                    if (mode == AXIS_MODE_W) {
                        uint64_t v = tnkr_view_read(in, n, c, x, k);
                        if (opts->reduce_add)
                            v = tnkr_acc_add(tnkr_view_read(out, n, c, x, (uint32_t)coord), v,
                                             out->dtype);
                        tnkr_view_write(out, n, c, x, (uint32_t)coord, v);
                    } else {
                        uint64_t v = tnkr_view_read(in, n, c, k, x);
                        if (opts->reduce_add)
                            v = tnkr_acc_add(tnkr_view_read(out, n, c, (uint32_t)coord, x), v,
                                             out->dtype);
                        tnkr_view_write(out, n, c, (uint32_t)coord, x, v);
                    }
            }
}

void tnkr_scatter(tnkr_view_t *out, const tnkr_view_t *in, const tnkr_view_t *idx,
                  axis_mode_t mode) {
    const gs_opts_t twin = {false, false, false, false};

    tnkr_scatter_ex(out, in, idx, mode, &twin);
}
