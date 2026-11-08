// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: include/kernel/kernel_types.h
 *
 * Common operator-layer types: mode enums, conv/pool attribute structs.
 * ========================================================================== */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- register indices ----
 * Operands are register indices; register contents are accessed only by
 * tdr/sys-class instructions. Three independent, non-overlapping files:
 *   tdr_idx_t - tensor descriptor registers (set up by tdr-class instrs)
 *   gpr_idx_t - scalar GPRs, owned by the per-core scalar coprocessor
 *   sys_idx_t - system registers
 * The *_NUM macros are the ISA 5-bit operand encoding limits; a chip
 * spec's counts must not exceed them. */
typedef uint8_t tdr_idx_t;
typedef uint8_t gpr_idx_t;
typedef uint8_t sys_idx_t;
#define TDR_IDX_MAX 31
#define TDR_NUM 32
#define GPR_IDX_MAX 31
#define GPR_NUM 32
#define SYS_IDX_MAX 31
#define SYS_NUM 32

/* bias provision; encodings outside the defined set are violations.
 * NONE: the instruction has no bias operand at all — the bias index is
 * ignored and need not name a configured TDR */
typedef enum {
    BIAS_MODE_NONE = 0,
    BIAS_MODE_PER_CHANNEL = 1,
} bias_mode_t;

/* output overwrite vs accumulate; encodings outside the defined set are
 * violations */
typedef enum {
    ACC_MODE_OVERWRITE = 0,
    ACC_MODE_ACCUMULATE = 1,
} acc_mode_t;

/* integer overflow handling; n/a for FP — FP is fixed saturate
 * (clamp to ±max, never inf) + RNE */
typedef enum {
    OVF_MODE_SATURATE = 0,
    OVF_MODE_TRUNCATE = 1,
} ovf_mode_t;

/* float-to-int rounding mode */
typedef enum {
    ROUND_MODE_RNE = 0,
    ROUND_MODE_RTZ = 1,
} round_mode_t;

/* block-quant group size (fp input) */
typedef enum {
    GROUP_SIZE_16 = 0,
    GROUP_SIZE_32 = 1,
} group_size_t;

/* block-quant group size (i8 input) */
typedef enum {
    GROUP_SIZE_64 = 0,
    GROUP_SIZE_128 = 1,
} group_size_i8_t;

/* gather out-of-bounds fill value */
typedef enum {
    PAD_VAL_ZERO = 0,
    PAD_VAL_ONE = 1,
    PAD_VAL_MINUS_ONE = 2,
    PAD_VAL_KEEP = 3,
} pad_val_t;

/* gather/scatter/reduce axis */
typedef enum {
    AXIS_MODE_W = 0,
    AXIS_MODE_H = 1,
} axis_mode_t;

/* tdr field-rewrite half select (offset): which 64-bit half of the
 * 128-bit TDR the instruction touches */
typedef enum {
    TDR_HALF_LO = 0, /* bits 0:63 */
    TDR_HALF_HI = 1, /* bits 64:127 */
} tdr_half_t;

/* load/store layout-conversion mode (value = instruction encoding);
 * encodings outside the supported set are violations */
typedef enum {
    LDST_MODE_NORMAL = 0,   /* no conversion */
    LDST_MODE_NC_TRANS = 1, /* N-C transpose */
    LDST_MODE_CW_TRANS = 2, /* C-W transpose; not supported by ldst yet, arrives with dmr */
} ldst_mode_t;

/* dmr transpose mode (value = instruction encoding). Note the ordering
 * difference to ldst_mode_t: the dmr family puts the C-W transpose first
 * (spec encoding), and mode 3 is reserved. */
typedef enum {
    DMR_MODE_NORMAL = 0,
    DMR_MODE_CW_TRANS = 1, /* C-W transpose: out (N,W,H,C) */
    DMR_MODE_NC_TRANS = 2, /* N-C transpose: out (C,N,H,W) */
} dmr_mode_t;

/* write-back reduce of the dmr stores (st / scatter); the atomic add is
 * performed at the output dtype (FP: RNE + saturate; INT: saturate) */
typedef enum {
    REDUCE_MODE_NONE = 0,
    REDUCE_MODE_ADD = 1,
} reduce_mode_t;

/* sort direction */
typedef enum {
    SORT_DIR_DESCEND = 0,
    SORT_DIR_ASCEND = 1,
} sort_dir_t;

/* whether the sort input carries its own coordinates (dmr.tctx.w0 opd1 /
 * opd2+opd3); the carried tensor rides along only when the result
 * includes coordinates */
typedef enum {
    SORT_IDX_NONE = 0,    /* positions are implicit 0..W-1 (mrg: W..2W-1) */
    SORT_IDX_CARRIED = 1, /* coordinates in opd1 (sort) / opd2+opd3 (mrg) */
} sort_input_idx_t;

/* sort result selection (result_idx_value encoding) */
typedef enum {
    SORT_RESULT_VALUE = 0, /* res0 only */
    SORT_RESULT_INDEX = 1, /* res1 only */
    SORT_RESULT_BOTH = 2,  /* res0 + res1 */
} sort_result_t;

/* prefix sum: whether the current element is included (1 bit) */
typedef enum {
    CUMSUM_EXCLUSIVE = 0,
    CUMSUM_INCLUSIVE = 1,
} cumsum_include_t;

typedef enum {
    TENSOR = 0,  /* shift amount is a tensor */
    CONSTANT = 1 /* shift amount is a 5-bit unsigned immediate */
} shift_mode_t;

/* conv geometry (aggregated to avoid bare-argument misordering);
 * each field maps 1:1 to an instruction immediate — mm conv, vec conv
 * and depthwise all obtain their geometry from this aggregate the same
 * way */
typedef struct {
    uint8_t stride_w;
    uint8_t stride_h;
    uint8_t pad_t;
    uint8_t pad_b;
    uint8_t pad_l;
    uint8_t pad_r;
    uint8_t insert;
    uint8_t dilation;
    uint16_t kernel_w; /* 1..15 */
    uint16_t kernel_h; /* 1..15 */
} conv_attr_t;

/* pool geometry */
typedef struct {
    uint8_t stride_w;
    uint8_t stride_h;
    uint8_t pad_t;
    uint8_t pad_b;
    uint8_t pad_l;
    uint8_t pad_r;
    uint16_t kernel_w; /* <= 16383 */
    uint16_t kernel_h; /* <= 16383 */
} pool_attr_t;

#ifdef __cplusplus
}
#endif
