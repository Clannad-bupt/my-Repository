// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/common/tdr.h
 *
 * TDR (Tensor DescRiptor) — 128-bit tensor descriptor register
 *
 * Bit-field map:
 *   Low 64 bits (raw[0]):
 *     [47:0]  addr    URF uses [31:0] only; GLOBAL uses all [47:0];
 *                     CONSTANT reuses [31:0] as the constant value
 *     [52:48] dtype   see tn_dtype_t
 *     [55:53] layout  see tdr_layout_t
 *     [59:56] bcast   ZEROSTRIDE only: bit3=nb bit2=cb bit1=hb bit0=wb
 *                     1 = broadcast (stride 0), 0 = normal; composable
 *     [61:60] rsvd    always 0
 *     [63:62] loc     00: URF, 01: GLOBAL
 *   High 64 bits (raw[1]):
 *     [79:64]   w     [95:80]   h
 *     [111:96]  c     [127:112] n
 *
 * Legal layout x loc:
 *   GLOBAL: CONTINUOUS, FREE
 *   URF:    HWALIGN, ZEROSTRIDE, FREE, WALIGN, CONSTANT
 *
 * This header is the encoding contract only (types, constants, field
 * positions) — what the instruction layer's tdr_insert* offsets refer
 * to. It is kernel-internal: the op-facing surface is
 * include/kernel/tensor.h, whose TNKR_BCAST_* mirror TDR_BCAST_* here
 * (asserted in common/tensor.c). The construction/parsing helpers
 * (tdr_make_* / tdr_get_* / tdr_check / tdr_rd / tdr_wr) sit alongside
 * in tdr_util.h.
 * ========================================================================== */
#pragma once

#include <stdint.h>

#include "tndef.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- layout encoding ([55:53]; 5/6 reserved) ---- */
typedef enum {
    TDR_LAYOUT_HWALIGN = 0,    /* URF: lane layout, h*w plane aligned */
    TDR_LAYOUT_ZEROSTRIDE = 1, /* URF: broadcast, bcast picks dims (composable);
                                  C broadcast stores c == smc_num, other dims 1 */
    TDR_LAYOUT_FREE = 2,       /* URF/GLOBAL: free element-count strides via
                                  tdr_stride_t; on URF only top_vec_move
                                  accepts it */
    TDR_LAYOUT_WALIGN = 3,     /* URF: conv kernel layout only */
    TDR_LAYOUT_CONTINUOUS = 4, /* GLOBAL: packed contiguous */
    TDR_LAYOUT_CONSTANT = 7,   /* URF: constant tensor; only low 64 bits valid */
} tdr_layout_t;

/* ---- storage location ([63:62]; 2/3 reserved) ---- */
typedef enum {
    TDR_LOC_URF = 0, /* Unified Register File */
    TDR_LOC_GLOBAL = 1,
} tdr_loc_t;

/* ---- broadcast dim mask (ZEROSTRIDE only) ---- */
#define TDR_BCAST_WB (1u << 0) /* bit56: W broadcast */
#define TDR_BCAST_HB (1u << 1) /* bit57: H broadcast */
#define TDR_BCAST_CB (1u << 2) /* bit58: C broadcast */
#define TDR_BCAST_NB (1u << 3) /* bit59: N broadcast */

/* URF width in bytes: the alignment granularity of the URF layouts.
 * HWALIGN/ZEROSTRIDE align the h*w plane start; WALIGN (conv kernel)
 * aligns each h row start. An address constraint, not a size constraint:
 * shortfall is padding and the next plane/row starts at the next aligned
 * boundary. */
#define TDR_URF_WIDTH_BYTES (32)

/* ==========================================================================
 * Field bit ranges (explicit shift/width, decoupled from compiler bitfields)
 * LO = field LSB index (0..127), W = field width,
 * MASK = contiguous-ones mask relative to the field itself
 * ========================================================================== */
#define TDR_ADDR_LO 0
#define TDR_ADDR_W 48
#define TDR_ADDR_MASK 0xFFFFFFFFFFFFULL

#define TDR_DTYPE_LO 48
#define TDR_DTYPE_W 5
#define TDR_DTYPE_MASK 0x1FULL

#define TDR_LAYOUT_LO 53
#define TDR_LAYOUT_W 3
#define TDR_LAYOUT_MASK 0x7ULL

#define TDR_BCAST_LO 56
#define TDR_BCAST_W 4
#define TDR_BCAST_MASK 0xFULL

#define TDR_RSVD0_LO 60
#define TDR_RSVD0_W 2

#define TDR_LOC_LO 62
#define TDR_LOC_W 2
#define TDR_LOC_MASK 0x3ULL

#define TDR_W_LO 64
#define TDR_W_W 16
#define TDR_W_MASK 0xFFFFULL

#define TDR_H_LO 80
#define TDR_H_W 16
#define TDR_H_MASK 0xFFFFULL

#define TDR_C_LO 96
#define TDR_C_W 16
#define TDR_C_MASK 0xFFFFULL

#define TDR_N_LO 112
#define TDR_N_W 16
#define TDR_N_MASK 0xFFFFULL

/* ---- stride TDR fields ----
 * Four stride fields (nstr/cstr/hstr/wstr), 32 bits each in the 128-bit
 * stride TDR. The bits are a count of ELEMENTS, not bytes; the tensor
 * layer scales by the operand's dtype size when computing byte offsets
 * (the element size itself comes from the operand dtype, not from here). */
#define TDR_WSTR_LO 0
#define TDR_WSTR_W 32
#define TDR_WSTR_MASK 0xFFFFFFFFULL

#define TDR_HSTR_LO 32
#define TDR_HSTR_W 32
#define TDR_HSTR_MASK 0xFFFFFFFFULL

#define TDR_CSTR_LO 64
#define TDR_CSTR_W 32
#define TDR_CSTR_MASK 0xFFFFFFFFULL

#define TDR_NSTR_LO 96
#define TDR_NSTR_W 32
#define TDR_NSTR_MASK 0xFFFFFFFFULL

/* ---- 128-bit TDR (plain data, no bitfields) ----
 * raw[0] = bits[63:0], raw[1] = bits[127:64]; array access is endian-safe */
typedef struct {
    uint64_t raw[2];
} tdr_t;

/* tdr_t / tensor_desc_t only serve tdr-class instructions that rewrite TDR
 * register contents; kernel interface operands are tdr_idx_t */
typedef tdr_t tensor_desc_t;

/* ---- companion stride TDR for FREE layout (a second 128-bit) ---- */
typedef struct {
    uint64_t raw[2];
} tdr_stride_t;

TN_STATIC_ASSERT(sizeof(tdr_t) == 16, "TDR must be 128-bit");
TN_STATIC_ASSERT(sizeof(tdr_stride_t) == 16, "stride TDR must be 128-bit");

/* ---- compile-time encoding self-check (one assert per map row) ----
 * Position chain: each field's LO must equal the previous field's LO+W;
 * MASK must equal the contiguous-ones mask of its field. Any shift/width
 * change inconsistent with the bit-field map fails the build. */
TN_STATIC_ASSERT(TDR_ADDR_LO == 0 && TDR_ADDR_W == 48, "addr must be [47:0]");
TN_STATIC_ASSERT(TDR_DTYPE_LO == TDR_ADDR_LO + TDR_ADDR_W && TDR_DTYPE_W == 5,
                 "dtype must be [52:48]");
TN_STATIC_ASSERT(TDR_LAYOUT_LO == TDR_DTYPE_LO + TDR_DTYPE_W && TDR_LAYOUT_W == 3,
                 "layout must be [55:53]");
TN_STATIC_ASSERT(TDR_BCAST_LO == TDR_LAYOUT_LO + TDR_LAYOUT_W && TDR_BCAST_W == 4,
                 "bcast must be [59:56]");
TN_STATIC_ASSERT(TDR_RSVD0_LO == TDR_BCAST_LO + TDR_BCAST_W && TDR_RSVD0_W == 2,
                 "rsvd0 must be [61:60]");
TN_STATIC_ASSERT(TDR_LOC_LO == TDR_RSVD0_LO + TDR_RSVD0_W && TDR_LOC_W == 2,
                 "loc must be [63:62]");
TN_STATIC_ASSERT(TDR_LOC_LO + TDR_LOC_W == 64, "low 64-bit must be fully tiled");

TN_STATIC_ASSERT(TDR_ADDR_MASK == 0xFFFFFFFFFFFFULL, "addr mask mismatch");
TN_STATIC_ASSERT(TDR_DTYPE_MASK == 0x1FULL, "dtype mask mismatch");
TN_STATIC_ASSERT(TDR_LAYOUT_MASK == 0x7ULL, "layout mask mismatch");
TN_STATIC_ASSERT(TDR_BCAST_MASK == 0xFULL, "bcast mask mismatch");
TN_STATIC_ASSERT(TDR_LOC_MASK == 0x3ULL, "loc mask mismatch");

TN_STATIC_ASSERT(TDR_W_LO == 64 && TDR_W_W == 16, "w must be [79:64]");
TN_STATIC_ASSERT(TDR_H_LO == TDR_W_LO + TDR_W_W && TDR_H_W == 16, "h must be [95:80]");
TN_STATIC_ASSERT(TDR_C_LO == TDR_H_LO + TDR_H_W && TDR_C_W == 16, "c must be [111:96]");
TN_STATIC_ASSERT(TDR_N_LO == TDR_C_LO + TDR_C_W && TDR_N_W == 16, "n must be [127:112]");
TN_STATIC_ASSERT(TDR_N_LO + TDR_N_W == 128, "high 64-bit must be fully tiled");

TN_STATIC_ASSERT(TDR_WSTR_LO == 0 && TDR_WSTR_W == 32, "w_str must be [31:0]");
TN_STATIC_ASSERT(TDR_HSTR_LO == TDR_WSTR_LO + TDR_WSTR_W && TDR_HSTR_W == 32,
                 "h_str must be [63:32]");
TN_STATIC_ASSERT(TDR_CSTR_LO == TDR_HSTR_LO + TDR_HSTR_W && TDR_CSTR_W == 32,
                 "c_str must be [95:64]");
TN_STATIC_ASSERT(TDR_NSTR_LO == TDR_CSTR_LO + TDR_CSTR_W && TDR_NSTR_W == 32,
                 "n_str must be [127:96]");

#ifdef __cplusplus
}
#endif
