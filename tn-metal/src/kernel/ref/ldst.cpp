// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/ldst.cpp
 *
 * top/ldst engine instructions (ref backend): load / store /
 * gather / scatter. The gather/scatter entries below share their
 * traversal core with the vec twins (tensor.cpp — identical semantics,
 * see kernel.h); top_vec_move belongs to the vec engine, in vec.cpp —
 * files are split by kernel.h engine
 *
 * Semantics:
 *   - load: external memory -> URF; store: URF -> external memory;
 *     dtype unchanged (bit copy)
 *   - URF side is HWALIGN; load also accepts WALIGN for the conv kernel
 *     (per-row urf-width placement, see tensor.cpp; the load
 *     materializes the row gaps); external side is CONTINUOUS / FREE
 *   - shapes must match (N/C swapped for NC transpose); mismatch is a
 *     violation (see violate.h)
 *   - mode: 0=normal, 1=NC transpose (out(n,c,h,w)=in(c,n,h,w), elements
 *     whose source coordinate leaves the source shape are skipped);
 *     every other encoding is unsupported — a violation (CW_TRANS
 *     arrives with dmr, 3 is undefined)
 * ========================================================================== */
#include "kernel/common/context_internal.h"
#include "kernel/kernel.h"
#include "tensor.h"
#include "violate.h"

namespace {

/* ld/st pairing check: same dtype, shapes consistent per mode */
bool ldst_pair_ok(const tnkr_view_t *urf, const tnkr_view_t *ext, ldst_mode_t mode) {
    if (urf->dtype != ext->dtype)
        return false;
    if (mode == LDST_MODE_NC_TRANS)
        return ext->dn == urf->dc && ext->dc == urf->dn && ext->dh == urf->dh && ext->dw == urf->dw;
    return tnkr_view_same_shape(urf, ext);
}

} // anonymous namespace

extern "C" void top_ldst_load(tdr_idx_t output, tdr_idx_t input, tdr_idx_t in_str,
                              ldst_mode_t mode) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, in;

    TNKR_REF_RESOLVE(ctx, &out, output, 0, "ldst.load", "out");
    TNKR_REF_RESOLVE(ctx, &in, input, in_str, "ldst.load", "in");
    if (tdr_get_loc(out.desc) != TDR_LOC_URF || tdr_get_loc(in.desc) != TDR_LOC_GLOBAL)
        TNKR_REF_VIOLATE("ldst.load", "load requires URF out / GLOBAL in");
    if (out.layout != TDR_LAYOUT_HWALIGN && out.layout != TDR_LAYOUT_WALIGN)
        TNKR_REF_VIOLATE("ldst.load", "URF side layout must be HWALIGN or WALIGN (conv kernel)");
    if (in.layout != TDR_LAYOUT_CONTINUOUS && in.layout != TDR_LAYOUT_FREE)
        TNKR_REF_VIOLATE("ldst.load", "external side layout must be CONTINUOUS or FREE");
    TNKR_REF_CHECK("ldst.load", mode == LDST_MODE_NORMAL || mode == LDST_MODE_NC_TRANS,
                   "mode must be NORMAL or NC_TRANS (CW_TRANS arrives with dmr)");
    if (!ldst_pair_ok(&out, &in, mode))
        TNKR_REF_VIOLATE("ldst.load", "operand shapes/dtypes do not pair for mode %d", (int)mode);
    tnkr_view_copy(&out, &in, mode == LDST_MODE_NC_TRANS);
}

extern "C" void top_ldst_store(tdr_idx_t output, tdr_idx_t out_str, tdr_idx_t input,
                               ldst_mode_t mode) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, in;

    TNKR_REF_RESOLVE(ctx, &out, output, out_str, "ldst.store", "out");
    TNKR_REF_RESOLVE(ctx, &in, input, 0, "ldst.store", "in");
    if (tdr_get_loc(out.desc) != TDR_LOC_GLOBAL || tdr_get_loc(in.desc) != TDR_LOC_URF)
        TNKR_REF_VIOLATE("ldst.store", "store requires GLOBAL out / URF in");
    if (out.layout != TDR_LAYOUT_CONTINUOUS && out.layout != TDR_LAYOUT_FREE)
        TNKR_REF_VIOLATE("ldst.store", "external side layout must be CONTINUOUS or FREE");
    if (in.layout != TDR_LAYOUT_HWALIGN)
        TNKR_REF_VIOLATE("ldst.store", "URF side layout must be HWALIGN");
    TNKR_REF_CHECK("ldst.store", mode == LDST_MODE_NORMAL || mode == LDST_MODE_NC_TRANS,
                   "mode must be NORMAL or NC_TRANS (CW_TRANS arrives with dmr)");
    if (!ldst_pair_ok(&in, &out, mode))
        TNKR_REF_VIOLATE("ldst.store", "operand shapes/dtypes do not pair for mode %d", (int)mode);
    tnkr_view_copy(&out, &in, mode == LDST_MODE_NC_TRANS);
}

/* gather / scatter: ldst twins of the vec instructions (kernel.h) —
 * legality and traversal live in the shared tensor.cpp core */
extern "C" void top_ldst_gather(tdr_idx_t output, tdr_idx_t input, tdr_idx_t index,
                                axis_mode_t mode, pad_val_t value) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, in, idx;

    TNKR_REF_RESOLVE(ctx, &out, output, 0, "ldst.gather", "out");
    TNKR_REF_RESOLVE(ctx, &in, input, 0, "ldst.gather", "in");
    TNKR_REF_RESOLVE(ctx, &idx, index, 0, "ldst.gather", "index");
    tnkr_gather(&out, &in, &idx, mode, value);
}

extern "C" void top_ldst_scatter(tdr_idx_t output, tdr_idx_t input, tdr_idx_t index,
                                 axis_mode_t mode) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, in, idx;

    TNKR_REF_RESOLVE(ctx, &out, output, 0, "ldst.scatter", "out");
    TNKR_REF_RESOLVE(ctx, &in, input, 0, "ldst.scatter", "in");
    TNKR_REF_RESOLVE(ctx, &idx, index, 0, "ldst.scatter", "index");
    tnkr_scatter(&out, &in, &idx, mode);
}
