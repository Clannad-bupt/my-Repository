// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: include/kernel/tensor.h
 *
 * Tensor helpers for op authors — descriptors without the plumbing
 *
 * An op implementation never builds a TDR descriptor by hand: it
 * describes each tensor to a creator below and passes the returned
 * tdr_idx_t to the kernel.h instructions. The helpers allocate the slot
 * (and the URF region for ltensors), program the descriptor through
 * tdr-class instructions in both backends — never through a shadow
 * backdoor — and validate legality (dtype capability, extents,
 * footprint) against their own bookkeeping before issue, never by
 * reading the descriptor back.
 *
 * The usual op body:
 *
 *   tdr_idx_t g_in, g_out, l_in, l_out;
 *   tnkr_gtensor(in_addr, n, c, h, w, dt, &g_in);   // external memory
 *   tnkr_gtensor(out_addr, n, c, h, w, dt, &g_out);
 *   tnkr_ltensor(tn, tc, th, tw, dt, &l_in);        // URF working tiles
 *   tnkr_ltensor(tn, tc, th, tw, dt, &l_out);
 *   for (each tile) {
 *       tnkr_set_addr(g_in, tile_addr);             // advance the view
 *       top_ldst_load(l_in, g_in, 0, LDST_MODE_NORMAL);
 *       top_vec_add(l_out, l_in, l_in, OVF_MODE_SATURATE);
 *       top_ldst_store(g_out, 0, l_out, LDST_MODE_NORMAL);
 *       // ... and shrink the tail tile with tnkr_set_shape
 *   }
 *
 * Lifetime: slots and URF regions are op-scoped. There is no individual
 * free — the op dispatch calls tnkr_tensor_release() after every op
 * execution and reclaims everything at once, so op code (error exits
 * included) can never leak. The kernel context is lazy-initialized on
 * first use, so op code never manages it either.
 *
 * Two operand forms beyond plain tensors:
 *   - scalar operand (y = x + k, clamp bounds): tnkr_lconstant wraps the
 *     value in a CONSTANT descriptor — no URF storage, pairs with any
 *     consumer shape; change the value mid-loop with tnkr_set_value.
 *   - broadcast operand (per-channel bias over h*w): ZEROSTRIDE is a
 *     compute-time layout — no transfer ever targets it. Create the
 *     ltensor with the STORED shape (broadcast N/H/W store extent 1,
 *     broadcast C one lane set), fill it as HWALIGN via ldst from an
 *     equally-shaped gtensor, then tnkr_set_bcast() adopts the broadcast
 *     view in place — same slot, same data (ZEROSTRIDE shares HWALIGN's
 *     address map with the broadcast dims' strides zeroed). Flip back
 *     with bcast 0 to reload the next tile.
 *   - strided URF view: tnkr_ltensor_free places a tile with element
 *     strides inside the lane window; top_vec_move is the only
 *     instruction that accepts FREE on URF.
 *
 * Not to be confused with src/kernel/ref/tensor.h (the ref backend's
 * internal operand-view resolution).
 * ========================================================================== */
#pragma once

#include <stdint.h>
#include <string.h>

#include "kernel/kernel_types.h"
#include "tndef.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Footprint of an (n,c,h,w) HWALIGN tensor in one URF lane window, in
 * bytes — the formula the allocator and the ref resolve path agree on.
 * Scenario: tile-fit arithmetic when sizing a tile loop (all operands
 * of one tile must fit the lane window together, each bank-aligned).
 * Returns 0 for a reserved dtype. */
uint64_t tnkr_hwalign_bytes(uint16_t n, uint16_t c, uint16_t h, uint16_t w, tn_dtype_t dtype);

/* Footprint of an (n,c,h,w) WALIGN tensor in one URF lane window, in
 * bytes — the conv-kernel placement: the lane law is HWALIGN's (lane =
 * c % smc_num, planes stack [n][cb]), but each h row occupies its own
 * lane-width-aligned row instead of rows packing into one aligned plane.
 * Scenario: sizing the kernel-operand tile of a top_mm_conv_fp call.
 * Returns 0 for a reserved dtype. */
uint64_t tnkr_walign_bytes(uint16_t n, uint16_t c, uint16_t h, uint16_t w, tn_dtype_t dtype);

/* CONTINUOUS view of an external-memory tensor.
 * Scenario: the op's input/output tensors handed down by the runtime —
 * pair with tnkr_ltensor working tiles and ldst to move the data.
 * addr:  device address of element [0,0,0,0] (48-bit).
 * n..w:  extents in elements, all nonzero (TN_ERR_PARAM otherwise);
 *        TN_ERR_ADDR past 48 bits, TN_ERR_DTYPE for a reserved dtype or
 *        one outside the chip capability set.
 * out:   receives the descriptor slot to pass to kernel.h instructions. */
tn_status_t tnkr_gtensor(uint64_t addr, uint16_t n, uint16_t c, uint16_t h, uint16_t w,
                         tn_dtype_t dtype, tdr_idx_t *out);

/* FREE view of a strided sub-block of external memory + its companion
 * stride slot.
 * Scenario: a per-core work-share box inside a larger contiguous tensor
 * — the box's planes are strided by the parent's extents.
 * strides: w/h/c/n strides in elements of dtype, pairing tndef.h dims[i]
 *          (innermost first); 0 broadcasts the dim. The w-stride is
 *          pinned to 1 — anything else is TN_ERR_PARAM.
 * out:     receives the descriptor slot; out_str: the stride slot, pass
 *          it as the ldst stride operand. The stride slot rejects set_*;
 *          the descriptor takes the same guards as tnkr_gtensor's. */
tn_status_t tnkr_gtensor_free(uint64_t addr, uint16_t n, uint16_t c, uint16_t h, uint16_t w,
                              tn_dtype_t dtype, const uint32_t strides[4], tdr_idx_t *out,
                              tdr_idx_t *out_str);

/* HWALIGN working tile in the URF lane window.
 * Scenario: the compute-side buffer of a tile loop (see the banner) —
 * ldst fills it, vec/dmr instructions consume it, ldst stores from it.
 * n..w:  tile extents in elements, all nonzero.
 * The address is bank-aligned auto-allocation (consecutive ltensors
 * land on distinct banks); TN_ERR_NOMEM when the footprint overruns the
 * lane window — size tiles with tnkr_hwalign_bytes. */
tn_status_t tnkr_ltensor(uint16_t n, uint16_t c, uint16_t h, uint16_t w, tn_dtype_t dtype,
                         tdr_idx_t *out);

/* WALIGN working tile in the URF lane window (the conv-kernel placement,
 * see tnkr_walign_bytes).
 * Scenario: the kernel operand of top_mm_conv_fp — ldst fills it from a
 * global (1,OC,KH*KW,IC) tensor, the conv consumes it.
 * n..w: tile extents in elements, all nonzero.
 * Same bank-aligned auto-allocation and TN_ERR_NOMEM lane-window bound as
 * tnkr_ltensor — size tiles with tnkr_walign_bytes. */
tn_status_t tnkr_ltensor_walign(uint16_t n, uint16_t c, uint16_t h, uint16_t w, tn_dtype_t dtype,
                                tdr_idx_t *out);

/* FREE view of a URF lane-window region + its companion stride slot.
 * Scenario: top_vec_move operands with non-HWALIGN placement — strided
 * sub-blocks, or lane-local broadcast reads (a 0 stride wraps the dim).
 * The lane law matches HWALIGN (lane = c % smc); the strides act only
 * inside a lane window (the c stride steps c-blocks within it), counted
 * in elements of dtype; unlike GLOBAL FREE the w-stride is free too.
 * The bump allocation charges the view's exact per-lane reach — the
 * highest offset the address map can produce, (n-1)*ns +
 * (cblocks-1)*cs + (h-1)*hs + (w-1)*ws + 1 elements; TN_ERR_NOMEM when
 * the reach overruns the lane window. Sub-byte dtypes are undefined
 * (TN_ERR_DTYPE). set_shape cannot recheck the stride-fixed reach —
 * recreate the view instead. strides/out/out_str: as
 * tnkr_gtensor_free's. */
tn_status_t tnkr_ltensor_free(uint16_t n, uint16_t c, uint16_t h, uint16_t w, tn_dtype_t dtype,
                              const uint32_t strides[4], tdr_idx_t *out, tdr_idx_t *out_str);

/* Scalar operand value for tnkr_lconstant / tnkr_set_value.
 * Assign the member matching the dtype; the helper stores the member's
 * low dtype_bits as the descriptor immediate. Float dtypes without a
 * native C type carry their bit patterns; the tnkr_imm_* constructors
 * keep call sites to one line:
 *   tnkr_lconstant(TN_DTYPE_FP32, tnkr_imm_f32(1.5f), &k);
 *   tnkr_lconstant(TN_DTYPE_BF16, tnkr_imm_bf16(1.5f), &k);
 *   tnkr_lconstant(TN_DTYPE_S8,  tnkr_imm_s8(-2), &k); */
typedef union {
    float f32;     /* TN_DTYPE_FP32 */
    uint32_t u32;  /* TN_DTYPE_U32 */
    int32_t s32;   /* TN_DTYPE_S32 */
    uint16_t u16;  /* TN_DTYPE_U16 */
    int16_t s16;   /* TN_DTYPE_S16 */
    uint16_t bf16; /* TN_DTYPE_BF16: bit pattern (tnkr_imm_bf16 converts) */
    uint16_t f16;  /* TN_DTYPE_FP16: bit pattern */
    uint8_t u8;    /* TN_DTYPE_U8 / U4 (low nibble) / UE8M0 / UE5M3 */
    int8_t s8;     /* TN_DTYPE_S8 / S4 (low nibble) */
    uint8_t fp8;   /* TN_DTYPE_FP8_E4M3 / FP8_E5M2: bit pattern */
    uint8_t fp4;   /* TN_DTYPE_FP4: bit pattern (low nibble) */
} tnkr_imm_t;

static inline tnkr_imm_t tnkr_imm_f32(float v) {
    tnkr_imm_t t;
    t.f32 = v;
    return t;
}
static inline tnkr_imm_t tnkr_imm_u32(uint32_t v) {
    tnkr_imm_t t;
    t.u32 = v;
    return t;
}
static inline tnkr_imm_t tnkr_imm_s32(int32_t v) {
    tnkr_imm_t t;
    t.s32 = v;
    return t;
}
static inline tnkr_imm_t tnkr_imm_u16(uint16_t v) {
    tnkr_imm_t t;
    t.u16 = v;
    return t;
}
static inline tnkr_imm_t tnkr_imm_s16(int16_t v) {
    tnkr_imm_t t;
    t.s16 = v;
    return t;
}
static inline tnkr_imm_t tnkr_imm_u8(uint8_t v) {
    tnkr_imm_t t;
    t.u8 = v;
    return t;
}
static inline tnkr_imm_t tnkr_imm_s8(int8_t v) {
    tnkr_imm_t t;
    t.s8 = v;
    return t;
}
static inline tnkr_imm_t tnkr_imm_bf16(float v) {
    /* float -> BF16, round to nearest even */
    tnkr_imm_t t;
    uint32_t u;
    memcpy(&u, &v, sizeof(u));
    u += 0x7FFFu + ((u >> 16) & 1u);
    t.bf16 = (uint16_t)(u >> 16);
    return t;
}
static inline tnkr_imm_t tnkr_imm_fp8(float v) {
    /* float -> FP8 E4M3, round to nearest even; the encoding has no inf:
     * overflow saturates to the max finite, NaN -> 0x7F */
    tnkr_imm_t t;
    uint32_t u;
    memcpy(&u, &v, sizeof(u));
    const uint32_t sign = (u >> 24) & 0x80u;
    const int32_t exp = (int32_t)((u >> 23) & 0xFFu) - 127;
    const uint32_t mant = u & 0x7FFFFFu;
    if (exp == 128) { /* inf or NaN */
        t.fp8 = (uint8_t)(sign | 0x7Fu);
        return t;
    }
    if (exp > 8 || (exp == 8 && mant > 0x600000u)) { /* past the max finite */
        t.fp8 = (uint8_t)(sign | 0x7Eu);
        return t;
    }
    if (exp >= -6) { /* normal: rounding carry into the exponent is wanted */
        u += 0x7FFFFu + ((u >> 20) & 1u);
        t.fp8 = (uint8_t)(sign | ((((u >> 23) & 0xFFu) - 120u) << 3) | ((u >> 20) & 7u));
        return t;
    }
    if (exp < -10) { /* below half the denormal quantum */
        t.fp8 = (uint8_t)sign;
        return t;
    }
    const uint32_t mf = mant | 0x800000u;
    const int shift = 14 - (int)exp; /* 21..24 */
    const uint32_t code = (mf + (1u << (shift - 1)) - 1u + ((mf >> shift) & 1u)) >> shift;
    t.fp8 = (uint8_t)(sign | code); /* may round up into the min normal */
    return t;
}

/* CONSTANT immediate operand.
 * Scenario: scalar work — y = x + k, clamp bounds, a per-tile scale —
 * without spending URF: the value lives in the descriptor and pairs
 * with any consumer shape. Engine rules: not both sources constant,
 * and a div divisor is never constant.
 * dtype: the value's interpretation (which union member is read);
 * TN_ERR_DTYPE for a reserved or unsupported one. */
tn_status_t tnkr_lconstant(tn_dtype_t dtype, tnkr_imm_t value, tdr_idx_t *out);

/* Field rewrites on a live descriptor; the slot keeps its index. */

/* Advance a GLOBAL tensor's view to the next tile (URF addresses are
 * allocator-owned — TN_ERR_PARAM on ltensors). */
tn_status_t tnkr_set_addr(tdr_idx_t idx, uint64_t addr);

/* Shrink a tensor's extents for the tail tile. GLOBAL: free; an HWALIGN
 * ltensor must stay within its allocated footprint (TN_ERR_NOMEM — the
 * bump region is software bookkeeping no engine can check); a
 * broadcast-adopted tensor flips back first (tnkr_set_bcast idx, 0);
 * constant tensors are recreated instead. */
tn_status_t tnkr_set_shape(tdr_idx_t idx, uint16_t n, uint16_t c, uint16_t h, uint16_t w);

/* Reinterpret a GLOBAL tensor's element type (an ltensor's dtype sets
 * its footprint at creation — TN_ERR_PARAM). */
tn_status_t tnkr_set_dtype(tdr_idx_t idx, tn_dtype_t dtype);

/* Rewrite a constant tensor's immediate in place — the idiom for a
 * scalar that changes per tile. The dtype is the creation-time one;
 * assign value's matching member. */
tn_status_t tnkr_set_value(tdr_idx_t idx, tnkr_imm_t value);

/* Broadcast dims for tnkr_set_bcast: the mask mirrors the encoding's
 * bcast field (src/kernel/common/tdr.h's TDR_BCAST_*, asserted to match
 * in common/tensor.c) */
#define TNKR_BCAST_WB (1u << 0)
#define TNKR_BCAST_HB (1u << 1)
#define TNKR_BCAST_CB (1u << 2)
#define TNKR_BCAST_NB (1u << 3)

/* Adopt the ZEROSTRIDE broadcast view on an HWALIGN ltensor
 * bcast: TNKR_BCAST_xB combined; the full mask is ambiguous with
 *        CONSTANT (TN_ERR_BCAST_FULL), reserved bits are TN_ERR_PARAM;
 *        0 drops the view for the next tile's load.
 * e.g., W and H bcast: bcast = TNKR_BCAST_WB | TNKR_BCAST_HB
 * */
tn_status_t tnkr_set_bcast(tdr_idx_t idx, uint8_t bcast);

/* Reclaim every helper slot and URF region at once. Called by the op
 * dispatch after each op execution — never by op code itself. */
void tnkr_tensor_release(void);

#ifdef __cplusplus
}
#endif
