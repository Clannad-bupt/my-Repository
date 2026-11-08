// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/common/tdr_util.h
 *
 * TDR construction/parsing helpers (private header, src/ only)
 *
 * kernel/common/tdr.h is the encoding contract (types, constants, field
 * positions); everything that BUILDS or PARSES a 128-bit value lives
 * here: raw bit primitives (tdr_rd/tdr_wr), field accessors, legality
 * check (tdr_check) and constructors (tdr_make_*). Consumers are the
 * kernel itself (context.c, regops.cpp, ref engines, common/tensor.c)
 * and the kernel tests — the op layer builds descriptors through
 * kernel/tensor.h instead.
 *
 * These are value helpers, not a register-access path: a descriptor is
 * assembled (tdr_make_*) before a tdr-class instruction programs it,
 * and the ref engines parse the register file through tdr_get_* exactly
 * the way hardware fetches operands from the TDR — engines do not
 * execute tdr_read instructions to reach their operands.
 * ========================================================================== */
#pragma once

#include "kernel/common/tdr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- raw field access primitives (explicit shift/mask, endian-safe) ----
 * lo: field LSB index (0..127); wmask: field-width mask.
 * Operates on raw directly, shared by main and stride TDRs.
 * Fields must not cross a 64-bit boundary (asserted above). */
static inline uint64_t tdr_rd(const uint64_t *raw, unsigned lo, uint64_t wmask) {
    unsigned word = (unsigned)(lo >> 6);
    unsigned bit = lo & 63U;
    return (raw[word] >> bit) & wmask;
}

static inline void tdr_wr(uint64_t *raw, unsigned lo, uint64_t wmask, uint64_t v) {
    unsigned word = (unsigned)(lo >> 6);
    unsigned bit = lo & 63U;
    raw[word] = (raw[word] & ~(wmask << bit)) | ((v & wmask) << bit);
}

/* ---- field accessors ---- */
static inline uint64_t tdr_get_addr(const tdr_t *t) {
    return tdr_rd(t->raw, TDR_ADDR_LO, TDR_ADDR_MASK);
}
static inline void tdr_set_addr(tdr_t *t, uint64_t v) {
    tdr_wr(t->raw, TDR_ADDR_LO, TDR_ADDR_MASK, v);
}

static inline tn_dtype_t tdr_get_dtype(const tdr_t *t) {
    return (tn_dtype_t)tdr_rd(t->raw, TDR_DTYPE_LO, TDR_DTYPE_MASK);
}
static inline void tdr_set_dtype(tdr_t *t, tn_dtype_t v) {
    tdr_wr(t->raw, TDR_DTYPE_LO, TDR_DTYPE_MASK, (uint64_t)v);
}

static inline tdr_layout_t tdr_get_layout(const tdr_t *t) {
    return (tdr_layout_t)tdr_rd(t->raw, TDR_LAYOUT_LO, TDR_LAYOUT_MASK);
}
static inline void tdr_set_layout(tdr_t *t, tdr_layout_t v) {
    tdr_wr(t->raw, TDR_LAYOUT_LO, TDR_LAYOUT_MASK, (uint64_t)v);
}

static inline uint8_t tdr_get_bcast(const tdr_t *t) {
    return (uint8_t)tdr_rd(t->raw, TDR_BCAST_LO, TDR_BCAST_MASK);
}

static inline tdr_loc_t tdr_get_loc(const tdr_t *t) {
    return (tdr_loc_t)tdr_rd(t->raw, TDR_LOC_LO, TDR_LOC_MASK);
}
static inline void tdr_set_loc(tdr_t *t, tdr_loc_t v) {
    tdr_wr(t->raw, TDR_LOC_LO, TDR_LOC_MASK, (uint64_t)v);
}

static inline uint16_t tdr_get_w(const tdr_t *t) {
    return (uint16_t)tdr_rd(t->raw, TDR_W_LO, TDR_W_MASK);
}
static inline void tdr_set_w(tdr_t *t, uint16_t v) { tdr_wr(t->raw, TDR_W_LO, TDR_W_MASK, v); }

static inline uint16_t tdr_get_h(const tdr_t *t) {
    return (uint16_t)tdr_rd(t->raw, TDR_H_LO, TDR_H_MASK);
}
static inline void tdr_set_h(tdr_t *t, uint16_t v) { tdr_wr(t->raw, TDR_H_LO, TDR_H_MASK, v); }

static inline uint16_t tdr_get_c(const tdr_t *t) {
    return (uint16_t)tdr_rd(t->raw, TDR_C_LO, TDR_C_MASK);
}
static inline void tdr_set_c(tdr_t *t, uint16_t v) { tdr_wr(t->raw, TDR_C_LO, TDR_C_MASK, v); }

static inline uint16_t tdr_get_n(const tdr_t *t) {
    return (uint16_t)tdr_rd(t->raw, TDR_N_LO, TDR_N_MASK);
}
static inline void tdr_set_n(tdr_t *t, uint16_t v) { tdr_wr(t->raw, TDR_N_LO, TDR_N_MASK, v); }

/* ---- TDR legality check ---- */
static inline tn_status_t tdr_check(const tdr_t *t) {
    /* Reserved dtype values (0x10..0x1F) are always illegal; "defined but
     * unsupported on this chip" is rejected by tdr_shadow_write via
     * ctx->info.dtype_mask, not here */
    if ((unsigned)tdr_get_dtype(t) >= 0x10u)
        return TN_ERR_DTYPE;

    switch (tdr_get_layout(t)) {
    case TDR_LAYOUT_HWALIGN:
    case TDR_LAYOUT_ZEROSTRIDE:
    case TDR_LAYOUT_WALIGN:
    case TDR_LAYOUT_CONSTANT:
        if (tdr_get_loc(t) != TDR_LOC_URF)
            return TN_ERR_LOC;
        break;
    case TDR_LAYOUT_CONTINUOUS:
        if (tdr_get_loc(t) != TDR_LOC_GLOBAL)
            return TN_ERR_LOC;
        break;
    case TDR_LAYOUT_FREE:
        break; /* legal on both sides */
    default:
        return TN_ERR_LAYOUT;
    }

    /* HWALIGN has no h*w size constraint: TDR_URF_WIDTH_BYTES is the
     * plane/row start-address alignment, padded by the address map — not
     * a configuration-legality rule */

    if (tdr_get_layout(t) != TDR_LAYOUT_ZEROSTRIDE && tdr_get_bcast(t) != 0)
        return TN_ERR_BCAST;
    if (tdr_get_bcast(t) == 0xF)
        return TN_ERR_BCAST_FULL; /* ambiguous with CONSTANT, banned */

    if (tdr_get_layout(t) == TDR_LAYOUT_ZEROSTRIDE) {
        /* Stored extent of broadcast dims must be 1 (see tdr_layout_t);
         * C broadcast requires dc == smc_num — device-dependent, rejected
         * at engine operand resolution (ref: tnkr_view_resolve), not here */
        uint8_t bc = tdr_get_bcast(t);
        if ((bc & TDR_BCAST_NB) && tdr_get_n(t) != 1)
            return TN_ERR_BCAST_EXT;
        if ((bc & TDR_BCAST_HB) && tdr_get_h(t) != 1)
            return TN_ERR_BCAST_EXT;
        if ((bc & TDR_BCAST_WB) && tdr_get_w(t) != 1)
            return TN_ERR_BCAST_EXT;
    }

    if (tdr_get_loc(t) == TDR_LOC_URF && (tdr_get_addr(t) >> 32) != 0)
        return TN_ERR_ADDR;

    return TN_SUCCESS;
}

/* ---- broadcast mask setter (ZEROSTRIDE only, enforced by tdr_make_urf) ---- */
static inline void tdr_set_bcast(tdr_t *t, uint8_t dims) {
    tdr_wr(t->raw, TDR_BCAST_LO, TDR_BCAST_MASK, (uint64_t)(dims & 0xF));
}

/* ---- constructors ----
 * URF side: addr is uint32_t, making [47:32] non-zero unrepresentable.
 * bcast takes effect only under ZEROSTRIDE; cleared otherwise. */
static inline tdr_t tdr_make_urf(tdr_layout_t ly, tn_dtype_t dt, uint32_t addr, uint16_t n,
                                 uint16_t c, uint16_t h, uint16_t w, uint8_t bcast) {
    tdr_t t;
    t.raw[0] = 0;
    t.raw[1] = 0;
    tdr_set_addr(&t, addr);
    tdr_set_dtype(&t, dt);
    tdr_set_layout(&t, ly);
    tdr_set_loc(&t, TDR_LOC_URF);
    if (ly == TDR_LAYOUT_ZEROSTRIDE)
        tdr_set_bcast(&t, bcast);
    tdr_set_w(&t, w);
    tdr_set_h(&t, h);
    tdr_set_c(&t, c);
    tdr_set_n(&t, n);
    return t;
}

/* GLOBAL side: legal layouts are FREE / CONTINUOUS; full 48-bit address. */
static inline tdr_t tdr_make_global(tdr_layout_t ly, tn_dtype_t dt, uint64_t addr, uint16_t n,
                                    uint16_t c, uint16_t h, uint16_t w) {
    tdr_t t;
    t.raw[0] = 0;
    t.raw[1] = 0;
    tdr_set_addr(&t, addr & 0xFFFFFFFFFFFFULL);
    tdr_set_dtype(&t, dt);
    tdr_set_layout(&t, ly);
    tdr_set_loc(&t, TDR_LOC_GLOBAL);
    tdr_set_w(&t, w);
    tdr_set_h(&t, h);
    tdr_set_c(&t, c);
    tdr_set_n(&t, n);
    return t;
}

/* Constant tensor (URF): low 64 bits reused, [31:0] = value, sizes zeroed. */
static inline tdr_t tdr_make_urf_constant(tn_dtype_t dt, uint32_t value) {
    tdr_t t;
    t.raw[0] = 0;
    t.raw[1] = 0;
    tdr_set_addr(&t, value);
    tdr_set_dtype(&t, dt);
    tdr_set_layout(&t, TDR_LAYOUT_CONSTANT);
    tdr_set_loc(&t, TDR_LOC_URF);
    return t;
}

/* Companion stride TDR for FREE layout. Strides are element counts (one
 * unit = one element of the operand's dtype); stride 0 broadcasts that
 * dimension. */
static inline tdr_stride_t tdr_make_stride(uint32_t w_str, uint32_t h_str, uint32_t c_str,
                                           uint32_t n_str) {
    tdr_stride_t s;
    s.raw[0] = 0;
    s.raw[1] = 0;
    tdr_wr(s.raw, TDR_WSTR_LO, TDR_WSTR_MASK, w_str);
    tdr_wr(s.raw, TDR_HSTR_LO, TDR_HSTR_MASK, h_str);
    tdr_wr(s.raw, TDR_CSTR_LO, TDR_CSTR_MASK, c_str);
    tdr_wr(s.raw, TDR_NSTR_LO, TDR_NSTR_MASK, n_str);
    return s;
}

#ifdef __cplusplus
}
#endif
