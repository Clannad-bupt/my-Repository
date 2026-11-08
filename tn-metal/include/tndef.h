// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: include/tndef.h
 *
 * Project-wide basic types: status codes, data type encodings,
 * bit-width/byte-size utilities, and the by-value tensor descriptor.
 * Data type encodings are fixed at 5 bits; 0x10..0x1F are reserved.
 * ========================================================================== */
#pragma once

#include <stdint.h>

/* Compile-time assertion, valid in both C and C++ translation units */
#ifdef __cplusplus
#define TN_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define TN_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TN_SUCCESS = 0,
    TN_ERR_PARAM = -1, /* NULL parameter or out-of-range value */
    TN_ERR_NODEV = -2, /* dev_idx does not exist */
    TN_ERR_NOMEM = -3, /* Resource allocation failed */
    TN_ERR_STATE = -4, /* Invalid state: uninitialized, duplicate init, unallocated slot, etc. */
    TN_ERR_NOT_READY = -5, /* Async operation not yet complete (event query) */
    // TDR Errors
    TN_ERR_LOC = -100,        /* Invalid layout and loc combination */
    TN_ERR_LAYOUT = -101,     /* Reserved layout value */
    TN_ERR_ALIGN = -102,      /* HWALIGN h*w does not meet alignment requirements */
    TN_ERR_BCAST = -103,      /* Broadcast flag set for a non-ZEROSTRIDE layout */
    TN_ERR_BCAST_FULL = -104, /* bcast == 0xF is ambiguous with CONSTANT */
    TN_ERR_ADDR = -105,       /* Nonzero URF address bits [47:32] */
    TN_ERR_DTYPE = -106, /* Reserved dtype or dtype outside the chip capability set (dtype_mask) */
    TN_ERR_BCAST_EXT = -107, /* Invalid stored extent for a ZEROSTRIDE broadcast dimension */
} tn_status_t;

typedef enum {
    TN_DTYPE_U4 = 0x0, /* Unsupported in this version */
    TN_DTYPE_S4 = 0x1, /* Unsupported in this version */
    TN_DTYPE_U8 = 0x2,
    TN_DTYPE_S8 = 0x3,
    TN_DTYPE_U16 = 0x4,
    TN_DTYPE_S16 = 0x5,
    TN_DTYPE_U32 = 0x6,
    TN_DTYPE_S32 = 0x7,
    TN_DTYPE_FP4 = 0x8, /* Used only for FP4 MatMul */
    TN_DTYPE_FP8_E4M3 = 0x9,
    TN_DTYPE_FP8_E5M2 = 0xa, /* Unsupported in this version */
    TN_DTYPE_UE8M0 = 0xb,    /* BlockScale quantization scale; 2^n is implicitly represented */
    TN_DTYPE_UE5M3 = 0xc,    /* BlockScale quantization scale; used only for FP4 MatMul */
    TN_DTYPE_BF16 = 0xd,
    TN_DTYPE_FP16 = 0xe, /* Unsupported in this version */
    TN_DTYPE_FP32 = 0xf,
} tn_dtype_t;

/* Sub-byte types (U4/S4/FP4) are packed: tn_dtype_bits() returns the actual
 * bit width, while tn_dtype_size() returns the minimum addressable unit of
 * 1 byte (two packed elements). */
static inline uint32_t tn_dtype_bits(tn_dtype_t dt) {
    switch (dt) {
    case TN_DTYPE_U4:
    case TN_DTYPE_S4:
    case TN_DTYPE_FP4:
        return 4;
    case TN_DTYPE_U8:
    case TN_DTYPE_S8:
    case TN_DTYPE_FP8_E4M3:
    case TN_DTYPE_FP8_E5M2:
    case TN_DTYPE_UE8M0:
    case TN_DTYPE_UE5M3:
        return 8;
    case TN_DTYPE_U16:
    case TN_DTYPE_S16:
    case TN_DTYPE_BF16:
    case TN_DTYPE_FP16:
        return 16;
    case TN_DTYPE_U32:
    case TN_DTYPE_S32:
    case TN_DTYPE_FP32:
        return 32;
    default:
        return 0;
    }
}

static inline uint32_t tn_dtype_size(tn_dtype_t dt) {
    uint32_t bits = tn_dtype_bits(dt);
    return (bits >= 8) ? (bits / 8) : 1;
}

#define TN_MAX_DIMS 6

/* By-value tensor descriptor: a caller-allocated device address plus
 * extents (dims), dims[0] innermost, densely packed and contiguous;
 * ndims is 1..TN_MAX_DIMS and entries past ndims are treated as dims = 1.
 * For packed sub-byte dtypes (FP4), dims[0] counts logical elements; two
 * elements share one byte of storage. A reshape or a slice that preserves
 * the element order is pure descriptor arithmetic (address offset). */
typedef struct tn_tensor {
    uint32_t dims[TN_MAX_DIMS]; /* extents, dims[0] innermost; past ndims: treated as 1 */
    uint32_t ndims;             /* 1..TN_MAX_DIMS */
    tn_dtype_t dtype;
    uint64_t addr; /* device address of element [0,0,0,0]; never a host pointer */
} tn_tensor_t;

static inline uint64_t tn_calc_bytes(tn_dtype_t dtype, uint32_t *dims, uint32_t num_dims) {
    uint64_t size = 1;
    for (uint32_t i = 0; i < num_dims; ++i) {
        size *= dims[i];
    }
    uint32_t bits = tn_dtype_bits(dtype);
    if (bits < 8) {
        size = (size + 1) / 2; /* two sub-byte elements per byte */
    } else {
        size *= (bits / 8);
    }
    return size;
}

static inline uint64_t tn_tensor_calc_bytes(tn_tensor_t *tensor) {
    return tn_calc_bytes(tensor->dtype, tensor->dims, tensor->ndims);
}

#ifdef __cplusplus
}
#endif
