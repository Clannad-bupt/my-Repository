// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/dmr.cpp
 *
 * dmr engine instructions (ref backend) — data rearrangement & movement.
 * Families land here per kernel.h section:
 *   movement : ld / st (reduce_add) / mv (this file)
 *   rearrange: gather / scatter (tensor.cpp shared core, gs_opts_t variants)
 *   sort&stat: sort / sort_mrg (stable, NaN above every number) /
 *              histogram_count (bucket tally, E = the res0 C extent)
 *
 * The movement family is a plain bit copy through TensorView: walk the
 * output shape, read the permuted source element, write it back — dtype
 * identical on both sides, no numeric interpretation (reduce_add excepted:
 * the accumulate runs at the output dtype, see dtype.h tnkr_acc_add).
 * The sort family decodes keys exactly to double (all dtypes <= 32 bits)
 * and works on per-channel scratch vectors — host-side heap use is fine,
 * the ref backend has no real-time constraints. Placement: URF HWAlign /
 * Global Continuous everywhere; dmr_sort also takes Global FREE, where a
 * coordinate tensor resolves with the accompanying values tensor's
 * companion stride — one c_str register in element units, each side
 * scaled by its own dtype size (kernel.h "Shared c_stride"); the
 * histogram input takes Global FREE with its own companion stride.
 * ========================================================================== */
#include "dtype.h"
#include "kernel/common/context_internal.h"
#include "kernel/kernel.h"
#include "tensor.h"
#include "violate.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

/* mode legality: 0/1/2 defined, 3 reserved (C-class silent, README §4) */
bool dmr_mode_ok(dmr_mode_t mode) { return (unsigned)mode <= (unsigned)DMR_MODE_NC_TRANS; }

/* shapes must pair as the exact permutation the mode names (kernel.h);
 * dtype identical both sides (bit copy) */
bool dmr_pair_ok(const tnkr_view_t *dst, const tnkr_view_t *src, dmr_mode_t mode) {
    if (dst->dtype != src->dtype)
        return false;
    switch (mode) {
    case DMR_MODE_NORMAL:
        return tnkr_view_same_shape(dst, src);
    case DMR_MODE_CW_TRANS: /* dst (N,W,H,C) <- src (N,C,H,W) */
        return dst->dn == src->dn && dst->dc == src->dw && dst->dh == src->dh && dst->dw == src->dc;
    case DMR_MODE_NC_TRANS: /* dst (C,N,H,W) <- src (N,C,H,W) */
        return dst->dn == src->dc && dst->dc == src->dn && dst->dh == src->dh && dst->dw == src->dw;
    default:
        return false; /* reserved mode (checked before) */
    }
}

/* dst <- src under the mode; shapes already validated; reduce accumulates
 * into the existing dst elements at the dst dtype. Positional loop vars
 * (p,q,r,s) walk dst axes 0..3; the source coordinate per mode:
 *   normal: src(p,q,r,s)
 *   cw: out(n,w,h,c)=in(n,c,h,w) -> src(p,s,r,q)
 *   nc: out(c,n,h,w)=in(n,c,h,w) -> src(q,p,r,s)
 */
void dmr_copy(tnkr_view_t *dst, const tnkr_view_t *src, dmr_mode_t mode, bool reduce) {
    uint32_t p, q, r, s;

    for (p = 0; p < dst->dn; p++)
        for (q = 0; q < dst->dc; q++)
            for (r = 0; r < dst->dh; r++)
                for (s = 0; s < dst->dw; s++) {
                    uint64_t v = (mode == DMR_MODE_CW_TRANS)   ? tnkr_view_read(src, p, s, r, q)
                                 : (mode == DMR_MODE_NC_TRANS) ? tnkr_view_read(src, q, p, r, s)
                                                               : tnkr_view_read(src, p, q, r, s);
                    if (reduce)
                        v = tnkr_acc_add(tnkr_view_read(dst, p, q, r, s), v, dst->dtype);
                    tnkr_view_write(dst, p, q, r, s, v);
                }
}

} // anonymous namespace

extern "C" void dmr_ld(tdr_idx_t output, tdr_idx_t input, tdr_idx_t in_str, dmr_mode_t mode) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, in;

    TNKR_REF_RESOLVE(ctx, &out, output, 0, "dmr.ld", "out");
    TNKR_REF_RESOLVE(ctx, &in, input, in_str, "dmr.ld", "in");
    if (tdr_get_loc(out.desc) != TDR_LOC_URF || tdr_get_loc(in.desc) != TDR_LOC_GLOBAL)
        TNKR_REF_VIOLATE("dmr.ld", "load requires URF out / GLOBAL in");
    if (out.layout != TDR_LAYOUT_HWALIGN && out.layout != TDR_LAYOUT_WALIGN)
        TNKR_REF_VIOLATE("dmr.ld", "URF side layout must be HWAlign or WALIGN (conv kernel)");
    if (in.layout != TDR_LAYOUT_CONTINUOUS && in.layout != TDR_LAYOUT_FREE)
        TNKR_REF_VIOLATE("dmr.ld", "external side layout must be CONTINUOUS or FREE");
    if (!dmr_mode_ok(mode))
        return; /* C-class: reserved mode, silent */
    if (out.layout == TDR_LAYOUT_WALIGN && mode != DMR_MODE_NORMAL)
        TNKR_REF_VIOLATE("dmr.ld", "WALIGN URF output is only legal for the normal load");
    if (!dmr_pair_ok(&out, &in, mode))
        TNKR_REF_VIOLATE("dmr.ld", "operand shapes/dtypes do not pair for mode %d", (int)mode);
    dmr_copy(&out, &in, mode, false);
}

extern "C" void dmr_st(tdr_idx_t output, tdr_idx_t out_str, tdr_idx_t input, reduce_mode_t reduce,
                       dmr_mode_t mode) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, in;

    TNKR_REF_RESOLVE(ctx, &out, output, out_str, "dmr.st", "out");
    TNKR_REF_RESOLVE(ctx, &in, input, 0, "dmr.st", "in");
    if (tdr_get_loc(out.desc) != TDR_LOC_GLOBAL || tdr_get_loc(in.desc) != TDR_LOC_URF)
        TNKR_REF_VIOLATE("dmr.st", "store requires GLOBAL out / URF in");
    if (out.layout != TDR_LAYOUT_CONTINUOUS && out.layout != TDR_LAYOUT_FREE)
        TNKR_REF_VIOLATE("dmr.st", "external side layout must be CONTINUOUS or FREE");
    if (in.layout != TDR_LAYOUT_HWALIGN)
        TNKR_REF_VIOLATE("dmr.st", "URF side layout must be HWAlign");
    if (!dmr_mode_ok(mode) || (unsigned)reduce > (unsigned)REDUCE_MODE_ADD)
        return; /* C-class: reserved mode/reduce encoding, silent */
    if (!dmr_pair_ok(&in, &out, mode))
        TNKR_REF_VIOLATE("dmr.st", "operand shapes/dtypes do not pair for mode %d", (int)mode);
    dmr_copy(&out, &in, mode, reduce == REDUCE_MODE_ADD);
}

extern "C" void dmr_mv(tdr_idx_t output, tdr_idx_t out_str, tdr_idx_t input, tdr_idx_t in_str,
                       dmr_mode_t mode) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, in;
    bool global;

    TNKR_REF_RESOLVE(ctx, &out, output, out_str, "dmr.mv", "out");
    TNKR_REF_RESOLVE(ctx, &in, input, in_str, "dmr.mv", "in");
    if (tdr_get_loc(out.desc) != tdr_get_loc(in.desc))
        TNKR_REF_VIOLATE("dmr.mv", "both operands must share the location (URF or Global)");
    global = tdr_get_loc(out.desc) == TDR_LOC_GLOBAL;
    if (global) {
        if ((out.layout != TDR_LAYOUT_CONTINUOUS && out.layout != TDR_LAYOUT_FREE) ||
            (in.layout != TDR_LAYOUT_CONTINUOUS && in.layout != TDR_LAYOUT_FREE))
            TNKR_REF_VIOLATE("dmr.mv", "Global side layout must be CONTINUOUS or FREE");
    } else {
        if (out.layout != TDR_LAYOUT_HWALIGN || in.layout != TDR_LAYOUT_HWALIGN)
            TNKR_REF_VIOLATE("dmr.mv", "URF side layout must be HWAlign");
    }
    if (!dmr_mode_ok(mode))
        return; /* C-class: reserved mode, silent */
    if (!dmr_pair_ok(&out, &in, mode))
        TNKR_REF_VIOLATE("dmr.mv", "operand shapes/dtypes do not pair for mode %d", (int)mode);
    dmr_copy(&out, &in, mode, false);
}

/* ---- gather / scatter: per-channel indexed pick / drop ----
 * Placement relaxed vs the top twins: each operand independently URF
 * (HWAlign) or Global; the in/index operands take Global FREE with their
 * companion strides riding the call, the output stays Continuous on
 * Global per spec; shapes pin N to 1. The traversal and legality live in
 * the tensor.cpp core shared with the vec/ldst twins, so the engines can
 * never drift apart. */
extern "C" void dmr_gather(tdr_idx_t output, tdr_idx_t input, tdr_idx_t in_str, tdr_idx_t index,
                           tdr_idx_t idx_str, axis_mode_t mode, pad_val_t value) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, in, idx;
    const gs_opts_t opts = {true, true, false, false};

    TNKR_REF_RESOLVE(ctx, &out, output, 0, "dmr.gather", "out");
    TNKR_REF_RESOLVE(ctx, &in, input, in_str, "dmr.gather", "in");
    TNKR_REF_RESOLVE(ctx, &idx, index, idx_str, "dmr.gather", "index");
    TNKR_REF_CHECK("dmr.gather", out.dn == 1 && in.dn == 1,
                   "rearrange shapes pin N to 1 (kernel.h dmr section)");
    tnkr_gather_ex(&out, &in, &idx, mode, value, &opts);
}

extern "C" void dmr_scatter(tdr_idx_t output, tdr_idx_t input, tdr_idx_t in_str, tdr_idx_t index,
                            tdr_idx_t idx_str, reduce_mode_t reduce, axis_mode_t mode) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, in, idx;
    gs_opts_t opts = {true, true, true, false}; /* row_idx: dmr index on H */

    TNKR_REF_RESOLVE(ctx, &out, output, 0, "dmr.scatter", "out");
    TNKR_REF_RESOLVE(ctx, &in, input, in_str, "dmr.scatter", "in");
    TNKR_REF_RESOLVE(ctx, &idx, index, idx_str, "dmr.scatter", "index");
    TNKR_REF_CHECK("dmr.scatter", out.dn == 1 && in.dn == 1,
                   "rearrange shapes pin N to 1 (kernel.h dmr section)");
    if ((unsigned)reduce > (unsigned)REDUCE_MODE_ADD)
        return; /* C-class: reserved reduce encoding, silent */
    if (reduce == REDUCE_MODE_ADD && out.loc != TDR_LOC_GLOBAL)
        TNKR_REF_VIOLATE("dmr.scatter", "reduce_add requires a Global output");
    opts.reduce_add = (reduce == REDUCE_MODE_ADD);
    tnkr_scatter_ex(&out, &in, &idx, mode, &opts);
}

/* ==========================================================================
 * sort & statistics (kernel.h dmr section)
 * ========================================================================== */
namespace {

/* coordinate dtype set (kernel.h sort family) */
bool dmr_idx_dtype_ok(tn_dtype_t dt) {
    return dt == TN_DTYPE_U8 || dt == TN_DTYPE_U16 || dt == TN_DTYPE_U32;
}

/* placement of the sort/histogram families: URF -> HWAlign; Global ->
 * Continuous (sort also takes FREE, whose companion stride rides the
 * call; sort_mrg/histogram stay Continuous per spec); name personalizes
 * the violation */
void dmr_check_place(const char *op, const tnkr_view_t *v, const char *name, bool allow_free) {
    if (v->loc == TDR_LOC_URF)
        TNKR_REF_CHECK(op, v->layout == TDR_LAYOUT_HWALIGN, "%s: URF side must be HWAlign", name);
    else
        TNKR_REF_CHECK(op,
                       v->loc == TDR_LOC_GLOBAL && (v->layout == TDR_LAYOUT_CONTINUOUS ||
                                                    (allow_free && v->layout == TDR_LAYOUT_FREE)),
                       "%s: Global side must be Continuous%s", name, allow_free ? " or Free" : "");
}

/* sort key: FP decodes to double, INT to the sign-extended integer — both
 * exactly representable in double (all dtypes <= 32 bits) */
double dmr_sort_key(uint64_t bits, tn_dtype_t dt) {
    if (tnkr_dt_is_fp(dt))
        return tnkr_decode_fp(bits, dt);
    return (double)tnkr_decode_int(bits, dt);
}

/* strict weak order on sort keys: numeric order with NaN above every
 * number; stability then keeps equal keys (incl. NaN among itself and
 * +0/-0) in input order */
bool dmr_key_less(double a, double b) {
    if (isnan(a) || isnan(b))
        return !isnan(a) && isnan(b);
    return a < b;
}

/* sort item: value bit pattern + comparison key + carried/implicit
 * coordinate */
struct dmr_item_t {
    double key;
    uint64_t bits;
    uint64_t coord;
};

/* res0/res1 operand law shared by sort/sort_mrg, resolved per the result
 * selection (an unused output is not touched; pass any slot) */
void dmr_sort_out_check(const char *op, const tnkr_view_t *out, const tnkr_view_t *out_idx,
                        const tnkr_view_t *in, bool want_val, bool want_idx, bool allow_free) {
    if (want_val) {
        dmr_check_place(op, out, "res0", allow_free);
        TNKR_REF_CHECK(op, out->loc == in->loc, "res0 must share opd0's location");
        TNKR_REF_CHECK(op, out->dn == 1 && out->dh == 1 && out->dc == in->dc,
                       "res0 shape must be (1,C,1,K) with opd0's channel count");
        TNKR_REF_CHECK(op, out->dtype == in->dtype, "res0 dtype must equal opd0");
    }
    if (want_idx) {
        dmr_check_place(op, out_idx, "res1", allow_free);
        TNKR_REF_CHECK(op, out_idx->loc == in->loc, "res1 must share opd0's location");
        TNKR_REF_CHECK(op, out_idx->dn == 1 && out_idx->dh == 1 && out_idx->dc == in->dc,
                       "res1 shape must be (1,C,1,K) with opd0's channel count");
        TNKR_REF_CHECK(op, dmr_idx_dtype_ok(out_idx->dtype), "res1 dtype not U8/U16/U32");
        TNKR_REF_CHECK(op, !want_val || out_idx->dw == out->dw, "res1 K must equal res0 K");
    }
}

/* coordinate range check shared by sort/sort_mrg: the widest coordinate
 * (implicit W-1 / 2W-1 or the carried max) must fit the res1 dtype */
void dmr_coord_fit(const char *op, uint64_t max_coord, const tnkr_view_t *out_idx) {
    TNKR_REF_CHECK(op, max_coord < (1ull << tn_dtype_bits(out_idx->dtype)),
                   "coordinate %llu does not fit the res1 dtype", (unsigned long long)max_coord);
}

/* sort-family encoding legality: 0/1/2 defined, the rest reserved
 * (C-class silent, README §4) */
bool dmr_sort_enc_ok(sort_dir_t ascend, sort_input_idx_t has_input_idx, sort_result_t result) {
    return (unsigned)ascend <= (unsigned)SORT_DIR_ASCEND &&
           (unsigned)has_input_idx <= (unsigned)SORT_IDX_CARRIED &&
           (unsigned)result <= (unsigned)SORT_RESULT_BOTH;
}

} // anonymous namespace

extern "C" void dmr_sort(tdr_idx_t res0, tdr_idx_t res0_str, tdr_idx_t res1, tdr_idx_t opd0,
                         tdr_idx_t opd0_str, tdr_idx_t opd1, sort_dir_t ascend,
                         sort_input_idx_t has_input_idx, sort_result_t result) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t in, out, out_idx, in_idx;
    const bool want_val = (result == SORT_RESULT_VALUE || result == SORT_RESULT_BOTH);
    const bool want_idx = (result == SORT_RESULT_INDEX || result == SORT_RESULT_BOTH);
    /* carried coordinates ride along only when the result includes them */
    const bool carried = (has_input_idx == SORT_IDX_CARRIED) && want_idx;
    const bool asc = (ascend == SORT_DIR_ASCEND);
    std::vector<dmr_item_t> items;
    uint32_t c, w, k;

    if (!dmr_sort_enc_ok(ascend, has_input_idx, result))
        return; /* C-class: reserved encodings, silent */

    /* FREE companion strides: opd0/res0 carry their own (ignored unless
     * FREE); the coordinate sides resolve with the SHARED values slot —
     * one c_str register, element units, each side scaled by its own
     * dtype size (kernel.h "Shared c_stride", FREE pairs only) */
    TNKR_REF_RESOLVE(ctx, &in, opd0, opd0_str, "dmr.sort", "opd0");
    if (want_val)
        TNKR_REF_RESOLVE(ctx, &out, res0, res0_str, "dmr.sort", "res0");
    if (want_idx)
        TNKR_REF_RESOLVE(ctx, &out_idx, res1, res0_str, "dmr.sort", "res1");
    if (carried)
        TNKR_REF_RESOLVE(ctx, &in_idx, opd1, opd0_str, "dmr.sort", "opd1");

    dmr_check_place("dmr.sort", &in, "opd0", true);
    TNKR_REF_CHECK("dmr.sort", in.dn == 1 && in.dh == 1, "opd0 shape must be (1,C,1,W)");
    TNKR_REF_CHECK("dmr.sort", tnkr_pick_dtype_ok(in.dtype), "opd0 dtype not in the sort set");
    dmr_sort_out_check("dmr.sort", &out, &out_idx, &in, want_val, want_idx, true);
    if (carried) {
        dmr_check_place("dmr.sort", &in_idx, "opd1", true);
        TNKR_REF_CHECK("dmr.sort", in_idx.loc == in.loc, "opd1 must share opd0's location");
        TNKR_REF_CHECK("dmr.sort", in_idx.layout == in.layout, "opd1 layout must match opd0");
        TNKR_REF_CHECK("dmr.sort",
                       in_idx.dn == 1 && in_idx.dh == 1 && in_idx.dc == in.dc && in_idx.dw == in.dw,
                       "opd1 shape must match opd0");
        TNKR_REF_CHECK("dmr.sort", dmr_idx_dtype_ok(in_idx.dtype), "opd1 dtype not U8/U16/U32");
    }
    if (want_idx && want_val)
        TNKR_REF_CHECK("dmr.sort", out_idx.layout == out.layout, "res1 layout must match res0");
    TNKR_REF_CHECK("dmr.sort", (!want_val || out.dw <= in.dw) && (!want_idx || out_idx.dw <= in.dw),
                   "topk K exceeds the input length W");

    /* validate-first: the widest coordinate must fit res1 before any write */
    if (want_idx && in.dw > 0) {
        uint64_t max_coord = carried ? 0 : (uint64_t)in.dw - 1;
        if (carried)
            for (c = 0; c < in.dc; c++)
                for (w = 0; w < in.dw; w++) {
                    uint64_t v = tnkr_view_read(&in_idx, 0, c, 0, w);
                    if (v > max_coord)
                        max_coord = v;
                }
        dmr_coord_fit("dmr.sort", max_coord, &out_idx);
    }

    /* per-channel stable sort; the first K items write out */
    for (c = 0; c < in.dc; c++) {
        items.clear();
        for (w = 0; w < in.dw; w++) {
            uint64_t bits = tnkr_view_read(&in, 0, c, 0, w);
            dmr_item_t it = {dmr_sort_key(bits, in.dtype), bits,
                             carried ? tnkr_view_read(&in_idx, 0, c, 0, w) : (uint64_t)w};
            items.push_back(it);
        }
        std::stable_sort(items.begin(), items.end(),
                         [asc](const dmr_item_t &a, const dmr_item_t &b) {
                             return asc ? dmr_key_less(a.key, b.key) : dmr_key_less(b.key, a.key);
                         });
        const uint32_t K = want_val ? out.dw : out_idx.dw;
        for (k = 0; k < K && k < items.size(); k++) {
            if (want_val)
                tnkr_view_write(&out, 0, c, 0, k, items[k].bits);
            if (want_idx)
                tnkr_view_write(&out_idx, 0, c, 0, k, items[k].coord);
        }
    }
}

extern "C" void dmr_sort_mrg(tdr_idx_t res0, tdr_idx_t res1, tdr_idx_t opd0, tdr_idx_t opd1,
                             tdr_idx_t opd2, tdr_idx_t opd3, sort_dir_t ascend,
                             sort_input_idx_t has_input_idx, sort_result_t result) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t in0, in1, out, out_idx, idx0, idx1;
    const bool want_val = (result == SORT_RESULT_VALUE || result == SORT_RESULT_BOTH);
    const bool want_idx = (result == SORT_RESULT_INDEX || result == SORT_RESULT_BOTH);
    const bool carried = (has_input_idx == SORT_IDX_CARRIED) && want_idx;
    const bool asc = (ascend == SORT_DIR_ASCEND);
    std::vector<uint64_t> coords;
    uint32_t c, w, k;
    uint64_t max_coord;

    if (!dmr_sort_enc_ok(ascend, has_input_idx, result))
        return; /* C-class: reserved encodings, silent */

    TNKR_REF_RESOLVE(ctx, &in0, opd0, 0, "dmr.sort_mrg", "opd0");
    TNKR_REF_RESOLVE(ctx, &in1, opd1, 0, "dmr.sort_mrg", "opd1");
    if (want_val)
        TNKR_REF_RESOLVE(ctx, &out, res0, 0, "dmr.sort_mrg", "res0");
    if (want_idx)
        TNKR_REF_RESOLVE(ctx, &out_idx, res1, 0, "dmr.sort_mrg", "res1");
    if (carried) {
        TNKR_REF_RESOLVE(ctx, &idx0, opd2, 0, "dmr.sort_mrg", "opd2");
        TNKR_REF_RESOLVE(ctx, &idx1, opd3, 0, "dmr.sort_mrg", "opd3");
    }

    dmr_check_place("dmr.sort_mrg", &in0, "opd0", false);
    dmr_check_place("dmr.sort_mrg", &in1, "opd1", false);
    TNKR_REF_CHECK("dmr.sort_mrg", in0.dn == 1 && in0.dh == 1 && in1.dn == 1 && in1.dh == 1,
                   "input shapes must be (1,C,1,W)");
    TNKR_REF_CHECK("dmr.sort_mrg", in0.dc == in1.dc && in0.dw == in1.dw && in0.dtype == in1.dtype,
                   "opd0/opd1 must share shape and dtype");
    TNKR_REF_CHECK("dmr.sort_mrg", tnkr_pick_dtype_ok(in0.dtype),
                   "input dtype not in the sort set");
    dmr_sort_out_check("dmr.sort_mrg", &out, &out_idx, &in0, want_val, want_idx, false);
    if (carried) {
        dmr_check_place("dmr.sort_mrg", &idx0, "opd2", false);
        dmr_check_place("dmr.sort_mrg", &idx1, "opd3", false);
        TNKR_REF_CHECK("dmr.sort_mrg", idx0.loc == in0.loc && idx1.loc == in0.loc,
                       "opd2/opd3 must share opd0's location");
        TNKR_REF_CHECK("dmr.sort_mrg",
                       idx0.dn == 1 && idx0.dh == 1 && idx0.dc == in0.dc && idx0.dw == in0.dw &&
                           idx1.dn == 1 && idx1.dh == 1 && idx1.dc == in0.dc && idx1.dw == in0.dw,
                       "opd2/opd3 shapes must match opd0/opd1");
        TNKR_REF_CHECK("dmr.sort_mrg", dmr_idx_dtype_ok(idx0.dtype) && dmr_idx_dtype_ok(idx1.dtype),
                       "opd2/opd3 dtype not U8/U16/U32");
    }
    TNKR_REF_CHECK("dmr.sort_mrg",
                   (!want_val || out.dw <= 2 * in0.dw) && (!want_idx || out_idx.dw <= 2 * in0.dw),
                   "topk K exceeds the merged length 2W");

    /* validate-first: sortedness per direction, carried-coordinate
     * conflicts, and the coordinate range — everything before any write */
    max_coord = carried ? 0 : (in0.dw > 0 ? 2 * (uint64_t)in0.dw - 1 : 0);
    for (c = 0; c < in0.dc; c++) {
        for (w = 1; w < in0.dw; w++) {
            double p0 = dmr_sort_key(tnkr_view_read(&in0, 0, c, 0, w - 1), in0.dtype);
            double v0 = dmr_sort_key(tnkr_view_read(&in0, 0, c, 0, w), in0.dtype);
            double p1 = dmr_sort_key(tnkr_view_read(&in1, 0, c, 0, w - 1), in1.dtype);
            double v1 = dmr_sort_key(tnkr_view_read(&in1, 0, c, 0, w), in1.dtype);
            if ((asc && dmr_key_less(v0, p0)) || (!asc && dmr_key_less(p0, v0)))
                TNKR_REF_VIOLATE("dmr.sort_mrg", "opd0 not sorted per the sort direction");
            if ((asc && dmr_key_less(v1, p1)) || (!asc && dmr_key_less(p1, v1)))
                TNKR_REF_VIOLATE("dmr.sort_mrg", "opd1 not sorted per the sort direction");
        }
        if (carried) {
            coords.clear();
            for (w = 0; w < in0.dw; w++) {
                uint64_t v0 = tnkr_view_read(&idx0, 0, c, 0, w);
                uint64_t v1 = tnkr_view_read(&idx1, 0, c, 0, w);
                coords.push_back(v0);
                coords.push_back(v1);
                if (v0 > max_coord)
                    max_coord = v0;
                if (v1 > max_coord)
                    max_coord = v1;
            }
            std::sort(coords.begin(), coords.end());
            for (k = 1; k < coords.size(); k++)
                TNKR_REF_CHECK("dmr.sort_mrg", coords[k] != coords[k - 1],
                               "carried coordinates conflict (opd2/opd3 duplicate %llu)",
                               (unsigned long long)coords[k]);
        }
    }
    if (want_idx && in0.dw > 0)
        dmr_coord_fit("dmr.sort_mrg", max_coord, &out_idx);

    /* per-channel stable merge: ties pick opd0 first ("opd1 ranks after
     * opd0"); the first K items write out */
    const uint32_t W = in0.dw;
    const uint32_t K = want_val ? out.dw : out_idx.dw;
    for (c = 0; c < in0.dc; c++) {
        uint32_t i = 0, j = 0;
        for (k = 0; k < K && (i < W || j < W); k++) {
            bool take0;
            if (i >= W)
                take0 = false;
            else if (j >= W)
                take0 = true;
            else {
                double k0 = dmr_sort_key(tnkr_view_read(&in0, 0, c, 0, i), in0.dtype);
                double k1 = dmr_sort_key(tnkr_view_read(&in1, 0, c, 0, j), in1.dtype);
                take0 = asc ? !dmr_key_less(k1, k0) : !dmr_key_less(k0, k1);
            }
            uint64_t bits =
                take0 ? tnkr_view_read(&in0, 0, c, 0, i) : tnkr_view_read(&in1, 0, c, 0, j);
            uint64_t coord;
            if (carried)
                coord =
                    take0 ? tnkr_view_read(&idx0, 0, c, 0, i) : tnkr_view_read(&idx1, 0, c, 0, j);
            else
                coord = take0 ? (uint64_t)i : (uint64_t)W + j;
            if (want_val)
                tnkr_view_write(&out, 0, c, 0, k, bits);
            if (want_idx)
                tnkr_view_write(&out_idx, 0, c, 0, k, coord);
            if (take0)
                i++;
            else
                j++;
        }
    }
}

extern "C" void dmr_histogram_count(tdr_idx_t res0, tdr_idx_t opd0, tdr_idx_t opd0_str) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    tnkr_view_t out, in;
    std::vector<uint64_t> buckets;
    uint32_t c, w, e;
    uint64_t v;

    TNKR_REF_RESOLVE(ctx, &in, opd0, opd0_str, "dmr.hist", "opd0");
    TNKR_REF_RESOLVE(ctx, &out, res0, 0, "dmr.hist", "res0");
    dmr_check_place("dmr.hist", &in, "opd0", true);
    dmr_check_place("dmr.hist", &out, "res0", false);
    TNKR_REF_CHECK("dmr.hist", in.dn == 1 && in.dh == 1, "opd0 shape must be (1,S,1,K)");
    TNKR_REF_CHECK("dmr.hist", out.dn == 1 && out.dh == 1 && out.dw == 1,
                   "res0 shape must be (1,E,1,1)");
    TNKR_REF_CHECK("dmr.hist", dmr_idx_dtype_ok(in.dtype), "opd0 dtype not U8/U16/U32");
    TNKR_REF_CHECK("dmr.hist", dmr_idx_dtype_ok(out.dtype), "res0 dtype not U8/U16/U32");

    /* E is the res0 C extent (shape-driven, no tie to the data max): one
     * counting pass into local buckets with the range check inline — no
     * tensor side effect before the final write loop (validate-first,
     * README §4). An empty input zero-fills every bucket. */
    buckets.assign(out.dc, 0);
    for (c = 0; c < in.dc; c++)
        for (w = 0; w < in.dw; w++) {
            v = tnkr_view_read(&in, 0, c, 0, w);
            TNKR_REF_CHECK("dmr.hist", v < (uint64_t)out.dc, "value %llu over the bucket count %u",
                           (unsigned long long)v, (unsigned)out.dc);
            buckets[(uint32_t)v]++;
        }

    /* counts saturate at the res0 dtype (fixed C-class encode) */
    for (e = 0; e < out.dc; e++)
        tnkr_view_write(&out, 0, e, 0, 0,
                        tnkr_encode_int((__int128)buckets[e], out.dtype, OVF_MODE_SATURATE));
}
