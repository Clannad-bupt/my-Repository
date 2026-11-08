// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/tensor.h
 *
 * TensorView — ref backend operand view
 *
 * Resolves a tdr_idx into an element-addressable view: descriptor +
 * companion stride + target memory; address mapping and element access
 * are implemented once in tensor.cpp. Engines only interpret descriptor
 * bits, never run tdr_check (same as hardware, see kernel.h top); the
 * host simulator protects itself loudly: every resolve failure maps to
 * a TNKR_RESOLVE_* status that engines report as a violation (see
 * violate.h), and an out-of-bounds element access — unreachable once
 * resolve footprint checks pass — is likewise a violation, not silent
 * data corruption.
 * ========================================================================== */
#pragma once

#include "kernel/common/context_internal.h"
#include "kernel/common/tdr_util.h"

#include <stdbool.h>
#include <stdint.h>

/* Resolved operand view; descriptor fields are cached at resolve time to
 * avoid re-extracting bit fields per element */
typedef struct {
    tnkr_ctx_t *ctx;
    const tdr_t *desc;
    const tdr_stride_t *stride; /* non-NULL only for FREE layout */
    uint8_t *mem;               /* target memory base (NULL for CONSTANT) */
    uint64_t mem_cap;           /* target memory capacity (OOB check) */
    tn_dtype_t dtype;
    tdr_layout_t layout;
    tdr_loc_t loc; /* URF / GLOBAL; URF-FREE addressing is lane-relative */
    uint8_t bcast;
    uint16_t dn, dc, dh, dw;
    uint64_t addr;
    uint32_t smc_num;
    uint64_t lane_bytes; /* equal-capacity window per lane: urf_bytes / smc_num */
    uint32_t dtype_bits;
} tnkr_view_t;

/* Resolve outcome; a non-OK status is an environment/addressing
 * violation and engines report it through TNKR_REF_VIOLATE */
typedef enum {
    TNKR_RESOLVE_OK = 0,
    TNKR_RESOLVE_NO_CTX,       /* not initialized */
    TNKR_RESOLVE_BAD_IDX,      /* di out of range */
    TNKR_RESOLVE_UNCONFIGURED, /* slot invalid / holds a stride */
    TNKR_RESOLVE_RESERVED_DT,  /* reserved dtype encoding */
    TNKR_RESOLVE_LOC,          /* loc not URF/GLOBAL */
    TNKR_RESOLVE_LAYOUT,       /* layout value not defined */
    TNKR_RESOLVE_NO_STRIDE,    /* FREE lacks its companion stride */
    TNKR_RESOLVE_ZS_EXTENT,    /* ZEROSTRIDE stored-extent violations */
    TNKR_RESOLVE_FREE_WSTRIDE, /* GLOBAL FREE wstride != 1 (spec: W is the
                                * contiguous dim on the external side) */
    TNKR_RESOLVE_FOOTPRINT,    /* URF lane-window / GLOBAL capacity overflow */
} tnkr_resolve_status_t;

const char *tnkr_resolve_status_name(tnkr_resolve_status_t st);

/* Resolve tdr_idx -> view.
 * di: descriptor slot; si: companion stride slot (consumed only for FREE,
 * ignored otherwise).
 * Every failure mode maps to a TNKR_RESOLVE_* status — the caller reports
 * it as a violation (ref backend policy, see violate.h); resolve never
 * fails silently.
 * Note: the alloc bit is NOT checked — slot allocation is a pure software
 * convention, invisible to hardware (context.c); slots written raw via
 * tdr_write are equally visible to engines. */
tnkr_resolve_status_t tnkr_view_resolve(tnkr_ctx_t *ctx, tdr_idx_t di, tdr_idx_t si,
                                        tnkr_view_t *out);

/* Element access: bit granularity, returns/accepts the raw dtype-width bit
 * pattern (no numeric interpretation).
 * CONSTANT: read returns the constant pattern, write is ignored. */
uint64_t tnkr_view_read(const tnkr_view_t *v, uint32_t n, uint32_t c, uint32_t h, uint32_t w);
void tnkr_view_write(const tnkr_view_t *v, uint32_t n, uint32_t c, uint32_t h, uint32_t w,
                     uint64_t bits);

/* ---- Traversal helpers shared across engines (engine files carry only
 * instruction semantics, no duplicated plumbing) ---- */

/* dtype set of the value-comparing dmr families (gather/scatter/sort):
 * FP8_E4M3/BF16/FP32 + all 8/16/32-bit integers */
bool tnkr_pick_dtype_ok(tn_dtype_t dt);

static inline bool tnkr_view_same_shape(const tnkr_view_t *a, const tnkr_view_t *b) {
    return a->dn == b->dn && a->dc == b->dc && a->dh == b->dh && a->dw == b->dw;
}

/* Element-wise bit copy: walk the dst shape, read raw patterns from src
 * and write to dst (dtype unchanged).
 * nc_trans: when true, read the source N-C transposed, i.e.
 * dst(n,c,h,w) <- src(c,n,h,w); elements whose source coordinate leaves
 * the source shape are skipped (ldst NC_TRANS mode only).
 * Shape pairing is validated inside (tnkr_view_copy_shape_ok); an illegal
 * pairing is a violation, never a silent partial copy. */
void tnkr_view_copy(tnkr_view_t *dst, const tnkr_view_t *src, bool nc_trans);

/* Shape-pairing legality of a tnkr_view_copy(dst, src, nc_trans) call
 * (single definition point; vec_move reports it under its own op name):
 *   nc_trans          -> swapped-shape equality (N<->C, H/W kept)
 *   src CONSTANT      -> any dst shape (reads return the pattern)
 *   src ZEROSTRIDE    -> broadcast dims (bcast bit set) read with stride
 *                        0, any dst extent (stored extents already
 *                        pinned by resolve); other dims must match dst
 *   src FREE          -> companion strides are arbitrary 32-bit element
 *                        counts; a nonzero stride requires the extent to
 *                        match dst, stride 0 broadcasts the dim to any
 *                        dst extent — on URF a cs=0 C broadcast is sound
 *                        only when the source's c-block 0 covers every
 *                        lane the dst touches (lane = c % smc_num is
 *                        fixed, cs strides c-blocks only)
 *   otherwise         -> exact shape equality
 * The dst side carries no extra legality of its own here: a dst stride
 * of 0 (FREE) folds writes and is the caller's intent, not a pairing
 * error. */
bool tnkr_view_copy_shape_ok(const tnkr_view_t *dst, const tnkr_view_t *src, bool nc_trans);

/* Indexed pick / drop shared by the vec/ldst gather/scatter twins and the
 * dmr family (identical semantics, see kernel.h): legality is checked here
 * once; an illegal call is a violation, unsupported mode / pad-value
 * encodings included.
 * tnkr_gather: out(n,c,h,k) = in(n,c,h,idx(0,c,0,k))         (mode W)
 *            out(n,c,k,w) = in(n,c,idx(0,c,0,k),w)         (mode H)
 * OOB coordinates fill per value (zero/one/-1/keep = leave the output
 * element untouched). tnkr_scatter is the inverse: in is the compacted
 * (N,C,H,K)/(N,C,K,W) side, OOB coordinates are dropped, and duplicate
 * coordinates resolve last-write-wins in ascending k order (reduce_add:
 * every k accumulates instead). The plain entries carry the vec/ldst
 * twin defaults; the _ex entries select the dmr variants via gs_opts_t. */
typedef struct {
    bool allow_global; /* operands may sit on Global (the dmr variants);
                          the top twins pin everything to URF */
    bool allow_free;   /* in/index operands may sit on Global FREE (their
                          companion strides ride the call); the output
                          stays Continuous on Global per spec */
    bool row_idx;      /* scatter mode H: index vector on the H axis —
                          shape (1,C,K,1), element read at idx(0,c,k,0) */
    bool reduce_add;   /* scatter: accumulate into the existing output at
                          the output dtype (dtype.h tnkr_acc_add) */
} gs_opts_t;

void tnkr_gather(tnkr_view_t *out, const tnkr_view_t *in, const tnkr_view_t *idx, axis_mode_t mode,
                 pad_val_t value);

void tnkr_gather_ex(tnkr_view_t *out, const tnkr_view_t *in, const tnkr_view_t *idx,
                    axis_mode_t mode, pad_val_t value, const gs_opts_t *opts);

void tnkr_scatter(tnkr_view_t *out, const tnkr_view_t *in, const tnkr_view_t *idx,
                  axis_mode_t mode);

void tnkr_scatter_ex(tnkr_view_t *out, const tnkr_view_t *in, const tnkr_view_t *idx,
                     axis_mode_t mode, const gs_opts_t *opts);
