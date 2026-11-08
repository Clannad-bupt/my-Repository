// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/common/tensor.c
 *
 * Tensor helpers for op authors (kernel/tensor.h): TDR slot + URF
 * allocation made implicit. Both backends compile this file.
 *
 * TDR access: descriptors are programmed exclusively through tdr-class
 * instructions (tdr_insert_imm — immediate halves for a full descriptor,
 * single-field inserts for rewrites), the same path op code would use by
 * hand; GPR pairs are never needed. What tdr_check / the shadow write
 * used to validate is covered here by input validation plus the helper
 * layer's own bookkeeping (tdr_kind / tdr_dtype / urf_region) — the
 * descriptor is legal by construction and never read back.
 *
 * URF allocation policy: a bump pointer over one lane window (the TDR
 * address is a window-internal offset, identical for every lane — see
 * ref/tensor.cpp's address map); every ltensor base aligns to the bank
 * size so consecutive tensors land on distinct banks.
 *
 * Lifetime: op-scoped. tnkr_tensor_release() (called by the op dispatch
 * after every op) reclaims all slots and the whole bump; there is no
 * individual free.
 * ========================================================================== */
#include "kernel/tensor.h"

#include <stddef.h>
#include <string.h>

#include "kernel/common/context_internal.h"
#include "kernel/common/tdr_util.h"
#include "kernel/kernel.h"

/* Op code never manages the context lifecycle: the first tensor call
 * initializes the default context, tnkr_init's double-init guard covers
 * an explicit init done earlier */
static tnkr_ctx_t *ensure_ctx(void) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    return (ctx != NULL) ? ctx : tnkr_init(NULL);
}

static uint64_t align_up(uint64_t v, uint64_t a) { return (v + a - 1) / a * a; }

/* defined (non-reserved) dtype and inside the chip capability set */
static int dtype_ok(const tnkr_ctx_t *ctx, tn_dtype_t dtype) {
    return tn_dtype_bits(dtype) != 0 && (ctx->info.dtype_mask & TNKR_DTYPE_BIT(dtype)) != 0;
}

/* program a full 128-bit slot (descriptor or FREE stride companion)
 * through two whole-half immediate inserts; tdr-class writes mark the
 * slot valid themselves */
static void program_raw(tdr_idx_t idx, const uint64_t raw[2]) {
    tdr_insert_imm(idx, raw[0], TDR_HALF_LO, 0, 63);
    tdr_insert_imm(idx, raw[1], TDR_HALF_HI, 0, 63);
}

static void program_desc(tdr_idx_t idx, const tdr_t *desc) { program_raw(idx, desc->raw); }

/* the public broadcast mask mirrors the encoding's bcast field */
TN_STATIC_ASSERT(TNKR_BCAST_WB == TDR_BCAST_WB && TNKR_BCAST_HB == TDR_BCAST_HB &&
                     TNKR_BCAST_CB == TDR_BCAST_CB && TNKR_BCAST_NB == TDR_BCAST_NB,
                 "TNKR_BCAST_* must match the encoding");

/* rewrite one live TDR field in place, addressed by kernel/common/tdr.h's
 * TDR_<field>_LO/_W (no magic bit positions) */
static void insert_field(tdr_idx_t idx, uint64_t v, uint32_t field_lo, uint32_t field_w) {
    tdr_insert_imm(idx, v, (tdr_half_t)(field_lo / 64u), (uint8_t)(field_lo % 64u),
                   (uint8_t)(field_lo % 64u + field_w - 1u));
}

uint64_t tnkr_hwalign_bytes(uint16_t n, uint16_t c, uint16_t h, uint16_t w, tn_dtype_t dtype) {
    const tnkr_chip_info_t *info = tnkr_spec_info();
    uint32_t dbits = tn_dtype_bits(dtype);
    uint64_t plane, cblocks;

    if (dbits == 0 || info->smc_num == 0 || info->urf_width_bytes == 0)
        return 0;
    /* aligned planes stacked [n][cb], same formula as the ref resolve
     * footprint (ref/tensor.cpp) */
    plane = align_up((uint64_t)h * w * dbits, (uint64_t)info->urf_width_bytes * 8) / 8;
    cblocks = ((uint64_t)c + info->smc_num - 1) / info->smc_num;
    return (uint64_t)n * cblocks * plane;
}

uint64_t tnkr_walign_bytes(uint16_t n, uint16_t c, uint16_t h, uint16_t w, tn_dtype_t dtype) {
    const tnkr_chip_info_t *info = tnkr_spec_info();
    uint32_t dbits = tn_dtype_bits(dtype);
    uint64_t row, cblocks;

    if (dbits == 0 || info->smc_num == 0 || info->urf_width_bytes == 0)
        return 0;
    /* each h row in its own lane-width-aligned row, stacked [n][cb] —
     * same formula as the ref resolve footprint (ref/tensor.cpp) */
    row = align_up((uint64_t)w * dbits, (uint64_t)info->urf_width_bytes * 8) / 8;
    cblocks = ((uint64_t)c + info->smc_num - 1) / info->smc_num;
    return (uint64_t)n * cblocks * h * row;
}

/* per-lane footprint law of the placed URF layouts (HWALIGN / WALIGN);
 * the set_shape growth guard and create_urf size by it */
static uint64_t urf_layout_bytes(tdr_layout_t ly, uint16_t n, uint16_t c, uint16_t h, uint16_t w,
                                 tn_dtype_t dtype) {
    return (ly == TDR_LAYOUT_WALIGN) ? tnkr_walign_bytes(n, c, h, w, dtype)
                                     : tnkr_hwalign_bytes(n, c, h, w, dtype);
}

tn_status_t tnkr_gtensor(uint64_t addr, uint16_t n, uint16_t c, uint16_t h, uint16_t w,
                         tn_dtype_t dtype, tdr_idx_t *out) {
    tnkr_ctx_t *ctx = ensure_ctx();
    tdr_t desc;
    int idx;

    if (ctx == NULL)
        return TN_ERR_STATE;
    if (out == NULL || n == 0 || c == 0 || h == 0 || w == 0)
        return TN_ERR_PARAM;
    if ((addr >> 48) != 0)
        return TN_ERR_ADDR;
    if (!dtype_ok(ctx, dtype))
        return TN_ERR_DTYPE;
    idx = tdr_alloc(ctx);
    if (idx < 0)
        return (tn_status_t)idx;
    desc = tdr_make_global(TDR_LAYOUT_CONTINUOUS, dtype, addr, n, c, h, w);
    program_desc((tdr_idx_t)idx, &desc);
    ctx->tdr_kind[idx] = (uint8_t)TNKR_TENSOR_GLOBAL;
    ctx->tdr_dtype[idx] = (uint8_t)dtype;
    ctx->tdr_layout[idx] = (uint8_t)tdr_get_layout(&desc);
    *out = (tdr_idx_t)idx;
    return TN_SUCCESS;
}

tn_status_t tnkr_gtensor_free(uint64_t addr, uint16_t n, uint16_t c, uint16_t h, uint16_t w,
                              tn_dtype_t dtype, const uint32_t strides[4], tdr_idx_t *out,
                              tdr_idx_t *out_str) {
    tnkr_ctx_t *ctx = ensure_ctx();
    tdr_t desc;
    tdr_stride_t str;
    int idx, sidx;

    if (ctx == NULL)
        return TN_ERR_STATE;
    if (out == NULL || out_str == NULL || strides == NULL || n == 0 || c == 0 || h == 0 || w == 0)
        return TN_ERR_PARAM;
    if (strides[0] != 1)
        return TN_ERR_PARAM; /* GLOBAL FREE pins the w-stride to 1 (kernel/common/tdr.h) */
    if ((addr >> 48) != 0)
        return TN_ERR_ADDR;
    if (!dtype_ok(ctx, dtype))
        return TN_ERR_DTYPE;
    idx = tdr_alloc(ctx);
    if (idx < 0)
        return (tn_status_t)idx;
    sidx = tdr_alloc(ctx);
    if (sidx < 0) {
        (void)tdr_free(ctx, (uint8_t)idx);
        return (tn_status_t)sidx;
    }
    desc = tdr_make_global(TDR_LAYOUT_FREE, dtype, addr, n, c, h, w);
    program_desc((tdr_idx_t)idx, &desc);
    str = tdr_make_stride(strides[0], strides[1], strides[2], strides[3]);
    program_raw((tdr_idx_t)sidx, str.raw);
    tdr_shadow_mark_stride(ctx, (uint8_t)sidx); /* the writes flagged it a descriptor */
    ctx->tdr_kind[idx] = (uint8_t)TNKR_TENSOR_GLOBAL;
    ctx->tdr_dtype[idx] = (uint8_t)dtype;
    ctx->tdr_layout[idx] = (uint8_t)tdr_get_layout(&desc);
    /* the stride slot keeps kind NONE: set_* rewrites target descriptors */
    *out = (tdr_idx_t)idx;
    *out_str = (tdr_idx_t)sidx;
    return TN_SUCCESS;
}

/* shared tail of the URF creators: bump-allocate the footprint, set the
 * address, program, record the bookkeeping */
static tn_status_t create_urf(tnkr_ctx_t *ctx, tdr_t *desc, uint16_t sn, uint16_t sc, uint16_t sh,
                              uint16_t sw, tn_dtype_t dtype, tnkr_tensor_kind_t kind,
                              tdr_idx_t *out) {
    const uint64_t lane = (uint64_t)ctx->info.urf_bytes / ctx->info.smc_num;
    const uint64_t bank = (uint64_t)tnkr_urf_bank_size(ctx);
    const uint64_t fp = urf_layout_bytes(tdr_get_layout(desc), sn, sc, sh, sw, dtype);
    uint64_t base;
    int idx;

    if (bank == 0 || fp == 0)
        return TN_ERR_DTYPE;
    base = align_up(ctx->urf_used, bank);
    if (base + fp > lane)
        return TN_ERR_NOMEM;
    idx = tdr_alloc(ctx);
    if (idx < 0)
        return (tn_status_t)idx;
    tdr_set_addr(desc, (uint32_t)base);
    program_desc((tdr_idx_t)idx, desc);
    ctx->urf_used = (uint32_t)(base + fp);
    ctx->urf_region[idx].base = (uint32_t)base;
    ctx->urf_region[idx].size = (uint32_t)fp;
    ctx->tdr_kind[idx] = (uint8_t)kind;
    ctx->tdr_dtype[idx] = (uint8_t)dtype;
    ctx->tdr_layout[idx] = (uint8_t)tdr_get_layout(desc);
    *out = (tdr_idx_t)idx;
    return TN_SUCCESS;
}

tn_status_t tnkr_ltensor(uint16_t n, uint16_t c, uint16_t h, uint16_t w, tn_dtype_t dtype,
                         tdr_idx_t *out) {
    tnkr_ctx_t *ctx = ensure_ctx();
    tdr_t desc;

    if (ctx == NULL)
        return TN_ERR_STATE;
    if (out == NULL || n == 0 || c == 0 || h == 0 || w == 0)
        return TN_ERR_PARAM;
    if (!dtype_ok(ctx, dtype))
        return TN_ERR_DTYPE;
    desc = tdr_make_urf(TDR_LAYOUT_HWALIGN, dtype, 0, n, c, h, w, 0);
    return create_urf(ctx, &desc, n, c, h, w, dtype, TNKR_TENSOR_URF, out);
}

tn_status_t tnkr_ltensor_walign(uint16_t n, uint16_t c, uint16_t h, uint16_t w, tn_dtype_t dtype,
                                tdr_idx_t *out) {
    tnkr_ctx_t *ctx = ensure_ctx();
    tdr_t desc;

    if (ctx == NULL)
        return TN_ERR_STATE;
    if (out == NULL || n == 0 || c == 0 || h == 0 || w == 0)
        return TN_ERR_PARAM;
    if (!dtype_ok(ctx, dtype))
        return TN_ERR_DTYPE;
    desc = tdr_make_urf(TDR_LAYOUT_WALIGN, dtype, 0, n, c, h, w, 0);
    return create_urf(ctx, &desc, n, c, h, w, dtype, TNKR_TENSOR_URF, out);
}

tn_status_t tnkr_ltensor_free(uint16_t n, uint16_t c, uint16_t h, uint16_t w, tn_dtype_t dtype,
                              const uint32_t strides[4], tdr_idx_t *out, tdr_idx_t *out_str) {
    tnkr_ctx_t *ctx = ensure_ctx();
    tdr_t desc;
    tdr_stride_t str;
    uint64_t lane, bank, esz, cblocks, fp, base;
    int idx, sidx;

    if (ctx == NULL)
        return TN_ERR_STATE;
    if (out == NULL || out_str == NULL || strides == NULL || n == 0 || c == 0 || h == 0 || w == 0)
        return TN_ERR_PARAM;
    if (!dtype_ok(ctx, dtype))
        return TN_ERR_DTYPE;
    esz = tn_dtype_bits(dtype) / 8u;
    if (esz == 0)
        return TN_ERR_DTYPE; /* sub-byte FREE is undefined (ref resolve) */
    /* the bump charges the view's exact per-lane reach: the highest
     * element offset the address map can produce inside one lane
     * window (strides wrap c-blocks within the lane), plus the element
     * itself; holes between strides are simply unused */
    cblocks = ((uint64_t)c + ctx->info.smc_num - 1) / ctx->info.smc_num;
    fp = ((uint64_t)(n - 1) * strides[3] + (cblocks - 1) * strides[2] +
          (uint64_t)(h - 1) * strides[1] + (uint64_t)(w - 1) * strides[0] + 1) *
         esz;
    lane = (uint64_t)ctx->info.urf_bytes / ctx->info.smc_num;
    bank = (uint64_t)tnkr_urf_bank_size(ctx);
    if (bank == 0)
        return TN_ERR_DTYPE;
    base = align_up(ctx->urf_used, bank);
    if (base + fp > lane)
        return TN_ERR_NOMEM;
    idx = tdr_alloc(ctx);
    if (idx < 0)
        return (tn_status_t)idx;
    sidx = tdr_alloc(ctx);
    if (sidx < 0) {
        (void)tdr_free(ctx, (uint8_t)idx);
        return (tn_status_t)sidx;
    }
    desc = tdr_make_urf(TDR_LAYOUT_FREE, dtype, 0, n, c, h, w, 0);
    tdr_set_addr(&desc, (uint32_t)base);
    program_desc((tdr_idx_t)idx, &desc);
    str = tdr_make_stride(strides[0], strides[1], strides[2], strides[3]);
    program_raw((tdr_idx_t)sidx, str.raw);
    tdr_shadow_mark_stride(ctx, (uint8_t)sidx); /* the writes flagged it a descriptor */
    ctx->urf_used = (uint32_t)(base + fp);
    ctx->urf_region[idx].base = (uint32_t)base;
    ctx->urf_region[idx].size = (uint32_t)fp;
    ctx->tdr_kind[idx] = (uint8_t)TNKR_TENSOR_URF_FREE;
    ctx->tdr_dtype[idx] = (uint8_t)dtype;
    ctx->tdr_layout[idx] = (uint8_t)tdr_get_layout(&desc);
    /* the stride slot keeps kind NONE: set_* rewrites target descriptors */
    *out = (tdr_idx_t)idx;
    *out_str = (tdr_idx_t)sidx;
    return TN_SUCCESS;
}

/* the member matching dtype, truncated to dtype_bits: the pattern the
 * descriptor immediate carries (the engines mask the same way at read) */
static uint32_t imm_bits(tn_dtype_t dtype, tnkr_imm_t v) {
    const uint32_t dbits = tn_dtype_bits(dtype);
    uint32_t bits;
    switch (dtype) {
    case TN_DTYPE_FP32:
        memcpy(&bits, &v.f32, sizeof(bits));
        break;
    case TN_DTYPE_U32:
        bits = v.u32;
        break;
    case TN_DTYPE_S32:
        bits = (uint32_t)v.s32;
        break;
    case TN_DTYPE_U16:
        bits = v.u16;
        break;
    case TN_DTYPE_S16:
        bits = (uint16_t)v.s16;
        break;
    case TN_DTYPE_BF16:
        bits = v.bf16;
        break;
    case TN_DTYPE_FP16:
        bits = v.f16;
        break;
    case TN_DTYPE_U8:
    case TN_DTYPE_U4:
    case TN_DTYPE_UE8M0:
    case TN_DTYPE_UE5M3:
        bits = v.u8;
        break;
    case TN_DTYPE_S8:
    case TN_DTYPE_S4:
        bits = (uint8_t)v.s8;
        break;
    case TN_DTYPE_FP8_E4M3:
    case TN_DTYPE_FP8_E5M2:
        bits = v.fp8;
        break;
    case TN_DTYPE_FP4:
        bits = v.fp4;
        break;
    default:
        return 0; /* unreachable: dtype_ok already rejected */
    }
    return (dbits >= 32u) ? bits : (bits & ((1u << dbits) - 1u));
}

tn_status_t tnkr_lconstant(tn_dtype_t dtype, tnkr_imm_t value, tdr_idx_t *out) {
    tnkr_ctx_t *ctx = ensure_ctx();
    tdr_t desc;
    int idx;

    if (ctx == NULL)
        return TN_ERR_STATE;
    if (out == NULL)
        return TN_ERR_PARAM;
    if (!dtype_ok(ctx, dtype))
        return TN_ERR_DTYPE;
    idx = tdr_alloc(ctx);
    if (idx < 0)
        return (tn_status_t)idx;
    desc = tdr_make_urf_constant(dtype, imm_bits(dtype, value));
    program_desc((tdr_idx_t)idx, &desc);
    ctx->tdr_kind[idx] = (uint8_t)TNKR_TENSOR_URF_CONST;
    ctx->tdr_dtype[idx] = (uint8_t)dtype;
    ctx->tdr_layout[idx] = (uint8_t)tdr_get_layout(&desc);
    *out = (tdr_idx_t)idx;
    return TN_SUCCESS;
}

tn_status_t tnkr_set_addr(tdr_idx_t idx, uint64_t addr) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();

    if (ctx == NULL)
        return TN_ERR_STATE;
    if (idx >= ctx->info.tdr_num || ctx->tdr_kind[idx] != TNKR_TENSOR_GLOBAL)
        return TN_ERR_PARAM; /* URF addresses are allocator-owned */
    if ((addr >> 48) != 0)
        return TN_ERR_ADDR;
    insert_field(idx, addr, TDR_ADDR_LO, TDR_ADDR_W);
    return TN_SUCCESS;
}

tn_status_t tnkr_set_shape(tdr_idx_t idx, uint16_t n, uint16_t c, uint16_t h, uint16_t w) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();

    if (ctx == NULL)
        return TN_ERR_STATE;
    if (idx >= ctx->info.tdr_num || n == 0 || c == 0 || h == 0 || w == 0)
        return TN_ERR_PARAM;
    switch (ctx->tdr_kind[idx]) {
    case TNKR_TENSOR_GLOBAL:
        break; /* no allocator state to protect */
    case TNKR_TENSOR_URF:
        /* the one bound no engine can check: the bump region is a pure
         * software convention, growth past it corrupts the neighbour;
         * the footprint law is the tensor's own placement's */
        if (urf_layout_bytes((tdr_layout_t)ctx->tdr_layout[idx], n, c, h, w,
                             (tn_dtype_t)ctx->tdr_dtype[idx]) > ctx->urf_region[idx].size)
            return TN_ERR_NOMEM;
        break;
    default:
        /* broadcast-adopted: flip back with tnkr_set_bcast(idx, 0) first;
         * constant / FREE ltensor: recreate instead (the FREE reach is
         * stride-fixed and can't be rechecked against a new shape) */
        return TN_ERR_PARAM;
    }
    insert_field(idx, w, TDR_W_LO, TDR_W_W);
    insert_field(idx, h, TDR_H_LO, TDR_H_W);
    insert_field(idx, c, TDR_C_LO, TDR_C_W);
    insert_field(idx, n, TDR_N_LO, TDR_N_W);
    return TN_SUCCESS;
}

tn_status_t tnkr_set_dtype(tdr_idx_t idx, tn_dtype_t dtype) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();

    if (ctx == NULL)
        return TN_ERR_STATE;
    if (idx >= ctx->info.tdr_num || ctx->tdr_kind[idx] != TNKR_TENSOR_GLOBAL)
        return TN_ERR_PARAM; /* an ltensor's dtype sets its footprint at creation */
    if (!dtype_ok(ctx, dtype))
        return TN_ERR_DTYPE;
    insert_field(idx, (uint64_t)dtype, TDR_DTYPE_LO, TDR_DTYPE_W);
    ctx->tdr_dtype[idx] = (uint8_t)dtype;
    return TN_SUCCESS;
}

tn_status_t tnkr_set_value(tdr_idx_t idx, tnkr_imm_t value) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();

    if (ctx == NULL)
        return TN_ERR_STATE;
    if (idx >= ctx->info.tdr_num || ctx->tdr_kind[idx] != TNKR_TENSOR_URF_CONST)
        return TN_ERR_PARAM;
    /* the immediate lives in the addr field; the write zero-extends */
    insert_field(idx, (uint64_t)imm_bits((tn_dtype_t)ctx->tdr_dtype[idx], value), TDR_ADDR_LO,
                 TDR_ADDR_W);
    return TN_SUCCESS;
}

/* broadcast view adoption: the tensor was created and filled as HWALIGN
 * (kernel/tensor.h's idiom), so this rewrites only the layout/bcast
 * fields — ZEROSTRIDE shares HWALIGN's address map, the data stays put.
 * The stored-extent rules (broadcast N/H/W extent 1, C one lane set) are
 * the engine resolve's check, same as for a hand-programmed descriptor */
tn_status_t tnkr_set_bcast(tdr_idx_t idx, uint8_t bcast) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();

    if (ctx == NULL)
        return TN_ERR_STATE;
    if (idx >= ctx->info.tdr_num)
        return TN_ERR_PARAM;
    if (ctx->tdr_kind[idx] != TNKR_TENSOR_URF && ctx->tdr_kind[idx] != TNKR_TENSOR_URF_BCAST)
        return TN_ERR_PARAM; /* GLOBAL/constant/FREE tensors have no broadcast view */
    if ((bcast & ~(uint8_t)TDR_BCAST_MASK) != 0)
        return TN_ERR_PARAM;
    if (bcast == (uint8_t)TDR_BCAST_MASK)
        return TN_ERR_BCAST_FULL; /* ambiguous with CONSTANT (kernel/common/tdr.h) */
    insert_field(idx, (uint64_t)(bcast != 0 ? TDR_LAYOUT_ZEROSTRIDE : TDR_LAYOUT_HWALIGN),
                 TDR_LAYOUT_LO, TDR_LAYOUT_W);
    insert_field(idx, (uint64_t)bcast, TDR_BCAST_LO, TDR_BCAST_W);
    ctx->tdr_kind[idx] = (uint8_t)(bcast != 0 ? TNKR_TENSOR_URF_BCAST : TNKR_TENSOR_URF);
    ctx->tdr_layout[idx] = (uint8_t)(bcast != 0 ? TDR_LAYOUT_ZEROSTRIDE : TDR_LAYOUT_HWALIGN);
    return TN_SUCCESS;
}

void tnkr_tensor_release(void) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    uint32_t i;

    if (ctx == NULL)
        return;
    for (i = 0; i < ctx->info.tdr_num; ++i) {
        if (TNKR_BIT_TEST(ctx->alloc, (int)i))
            (void)tdr_free(ctx, (uint8_t)i);
    }
    ctx->urf_used = 0;
    memset(ctx->urf_region, 0, sizeof(ctx->urf_region));
    memset(ctx->tdr_kind, 0, sizeof(ctx->tdr_kind));
    memset(ctx->tdr_dtype, 0, sizeof(ctx->tdr_dtype));
    memset(ctx->tdr_layout, 0, sizeof(ctx->tdr_layout));
}
