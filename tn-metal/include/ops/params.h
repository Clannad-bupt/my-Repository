// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: include/ops/params.h
 *
 * Operator ABI — the device opcodes, the per-core work-split struct,
 * per-op enums and non-tensor parameter structs shared by every operator
 * surface: the graph builder
 * (runtime/tndnn.h), operator-mode message assembly through tnrt, and the
 * typed operator entries of the ops module (ops.h).
 *
 * All param structs are serialized PODs, copied by value into graph nodes
 * and launch messages: fixed-width fields only, no pointers, natural
 * alignment. Changing a struct changes the graph/launch ABI.
 *
 * Dependency: tndef.h only; no runtime- or kernel-layer headers.
 * ========================================================================== */
#pragma once

#include <stdint.h>

#include "tndef.h"

#ifdef __cplusplus
extern "C" {
#endif

// device opcode — the stable ABI number identifying each operator in the
// graph node table (docs/tndnn-graph-format.md §7.1) and in the operator
// message header (docs/tndnn-execution.md §6), where the unified scalar
// entry tn_scalar_run_operator() dispatches on it; encoded as uint32_t on
// the wire. New ops append; released values are never reused or renumbered
typedef enum {
    TNOP_OPCODE_MATMUL = 0,
    TNOP_OPCODE_MATMUL_DQ = 1,
    TNOP_OPCODE_MATMUL_BQ = 2,
    TNOP_OPCODE_MATMUL_BQ_GS = 3,
    TNOP_OPCODE_MATMUL_W4BF16 = 4,
    TNOP_OPCODE_MATMUL_W8BF16 = 5,
    TNOP_OPCODE_ELTWISE = 6,
    TNOP_OPCODE_UNARY = 7,
    TNOP_OPCODE_SCALE = 8,
    TNOP_OPCODE_CLAMP = 9,
    TNOP_OPCODE_ACTIVATION = 10,
    TNOP_OPCODE_GLU = 11,
    TNOP_OPCODE_SOFTMAX = 12,
    TNOP_OPCODE_RMSNORM = 13,
    TNOP_OPCODE_LAYERNORM = 14,
    TNOP_OPCODE_ROPE = 15,
    TNOP_OPCODE_FATTENTION = 16,
    TNOP_OPCODE_CONV2D = 17,
    TNOP_OPCODE_PERMUTE = 18,
    TNOP_OPCODE_CAST = 19,
    TNOP_OPCODE_GATHER = 20,
    TNOP_OPCODE_SCATTER = 21,
    TNOP_OPCODE_REDUCE = 22,
    TNOP_OPCODE_COUNT = 23,
} tnop_opcode_t;

// per-core work split, carried in the op launch message header
// (docs/tndnn-execution.md §6.3): each op's host path computes the
// core-0 share of the launch — core 0 takes the first contiguous chunk
// per split dim, so its share is the worst case every core's
// local-memory allocation must fit. B0..B3 map to tensor dims per op
// (dims[0] innermost, dims past ndims read as 1); the scalar side
// consumes the split as-is and never re-runs the split logic. Extents
// are uint16_t: the host split logic rejects any dim it cannot
// represent (dim > UINT16_MAX, including an unsplit full extent)
typedef struct tnop_split {
    uint16_t B0;
    uint16_t B1;
    uint16_t B2;
    uint16_t B3;
} tnop_split_t;

// matmul layout mode: left is always {M,K} (K innermost); the mode
// selects the right-operand and output layouts — batch dims (dims[2+])
// append on all operands and broadcast pairwise (the larger must be a
// multiple of the smaller), covering per-head attention matmuls and
// head sharing (n_head % n_head_kv == 0):
//   NNN: right {K,N} -> out {M,N} (dense tndnn_matmul only)
//   NTN: right {N,K} -> out {M,N}
//   NTT: right {N,K} -> out {N,M}
// all quant paths (everything except dense tndnn_matmul) require an NT
// mode
typedef enum {
    TNOP_MATMUL_NNN = 0,
    TNOP_MATMUL_NTN = 1,
    TNOP_MATMUL_NTT = 2,
} tnop_matmul_mode_t;

// element-wise binary op:
//   add/sub/mul: integers + FP, out dtype may be wider (promotion)
//   div        : FP only (BF16/FP32)
//   max/min    : integers + FP, out dtype = input dtype
typedef enum {
    TNOP_EWISE_ADD = 0,
    TNOP_EWISE_SUB = 1,
    TNOP_EWISE_MUL = 2,
    TNOP_EWISE_DIV = 3,
    TNOP_EWISE_MAX = 4,
    TNOP_EWISE_MIN = 5,
} tnop_ewise_op_t;

// integer overflow handling of add/sub/mul (div/max/min and FP operands
// have no use for it); mirrors the kernel's ovf_mode_t — the ops ABI
// cannot include kernel headers, so the op static_asserts the encoding
typedef enum {
    TNOP_OVF_SATURATE = 0,
    TNOP_OVF_TRUNCATE = 1,
} tnop_ovf_mode_t;

// element-wise unary op
typedef enum {
    TNOP_UNARY_SQUARE = 0, // in FP8/BF16/FP32, out BF16/FP32
    TNOP_UNARY_ABS = 1,    // in integers + FP, out = in
    TNOP_UNARY_FLOOR = 2,  // in FP, out = in
    TNOP_UNARY_RSQRT = 3,  // in BF16/FP32, out = in
    TNOP_UNARY_SQRT = 4,   // in BF16/FP32, out = in
    TNOP_UNARY_NEG = 5,    // in integers + FP, out = in
    TNOP_UNARY_SIN = 6,    // in BF16/FP32, out = in
    TNOP_UNARY_COS = 7,    // in BF16/FP32, out = in
} tnop_unary_op_t;

// activation, element-wise; in/out BF16/FP32, same shape
//   GELU is the tanh approximation
typedef enum {
    TNOP_ACT_RELU = 0,
    TNOP_ACT_GELU = 1,
    TNOP_ACT_SILU = 2,
    TNOP_ACT_TANH = 3,
    TNOP_ACT_SIGMOID = 4,
} tnop_act_op_t;

// gated linear unit, split form: out = act(gate) * value
typedef enum {
    TNOP_GLU_SWIGLU = 0, // act = silu
    TNOP_GLU_GEGLU = 1,  // act = gelu
    TNOP_GLU_REGLU = 2,  // act = relu
} tnop_glu_op_t;

// attention mask mode for tndnn_fattention
typedef enum {
    TNOP_ATTN_MASK_NONE = 0,  // no mask; mask must be NULL
    TNOP_ATTN_MASK_SMALL = 1, // small tile mask for causal-style masking
    TNOP_ATTN_MASK_FULL = 2,  // full additive mask, see tndnn_fattention
} tnop_attn_mask_type_t;

// reduction op over one axis; out dtype = in dtype
typedef enum {
    TNOP_REDUCE_MAX = 0,
    TNOP_REDUCE_MIN = 1,
    TNOP_REDUCE_SUM = 2,
    TNOP_REDUCE_ABSMAX = 3,
} tnop_reduce_op_t;

// rope rotation style; MROPE / VISION reserved
typedef enum {
    TNOP_ROPE_NORMAL = 0, // interleaved pairs [cscs...]
    TNOP_ROPE_NEOX = 1,   // GPT-NeoX half rotation [ccss...]
} tnop_rope_mode_t;

// per-op non-tensor parameters; the graph copies them at add time
typedef struct tnop_matmul_param {
    tnop_matmul_mode_t mode;
} tnop_matmul_param_t;

typedef struct tnop_matmul_dq_param {
    tn_dtype_t qdtype;
    int32_t g_size;
    tnop_matmul_mode_t mode;
} tnop_matmul_dq_param_t;

typedef struct tnop_matmul_bq_param {
    tnop_matmul_mode_t mode;
} tnop_matmul_bq_param_t;

typedef struct tnop_matmul_bq_gs_param {
    float left_global_scale;
    float out_global_scale;
    tnop_matmul_mode_t mode;
} tnop_matmul_bq_gs_param_t;

typedef struct tnop_matmul_w4bf16_param {
    int32_t g_size;
    tnop_matmul_mode_t mode;
} tnop_matmul_w4bf16_param_t;

typedef struct tnop_matmul_w8bf16_param {
    int32_t g_size;
    tnop_matmul_mode_t mode;
} tnop_matmul_w8bf16_param_t;

typedef struct tnop_eltwise_param {
    tnop_ewise_op_t op;
    tnop_ovf_mode_t ovf;
} tnop_eltwise_param_t;

typedef struct tnop_unary_param {
    tnop_unary_op_t op;
} tnop_unary_param_t;

typedef struct tnop_scale_param {
    float scale;
    float bias;
} tnop_scale_param_t;

typedef struct tnop_clamp_param {
    float lo;
    float hi;
} tnop_clamp_param_t;

typedef struct tnop_activation_param {
    tnop_act_op_t op;
} tnop_activation_param_t;

typedef struct tnop_glu_param {
    tnop_glu_op_t op;
} tnop_glu_param_t;

typedef struct tnop_softmax_param {
    int32_t axis;
} tnop_softmax_param_t;

typedef struct tnop_rmsnorm_param {
    float eps;
} tnop_rmsnorm_param_t;

typedef struct tnop_layernorm_param {
    float eps;
} tnop_layernorm_param_t;

// rope parameters; the YaRN defaults (freq_scale=1, ext_factor=0,
// attn_factor=1, beta_fast=beta_slow=0) disable YaRN scaling
typedef struct tnop_rope_param {
    int32_t n_dims; // rotated dims along dims[0]; the rest pass through
    tnop_rope_mode_t mode;
    int32_t n_ctx_orig; // original training context length (YaRN)
    float freq_base;    // theta base
    float freq_scale;   // YaRN: 1/ratio
    float ext_factor;   // YaRN extrapolation factor
    float attn_factor;  // YaRN magnitude scaling
    float beta_fast;
    float beta_slow;
} tnop_rope_param_t;

typedef struct tnop_fattention_param {
    tnop_attn_mask_type_t mask_type;
    float scale;
} tnop_fattention_param_t;

// conv2d parameters; stride/dilation index 0 applies to dims[0] (W,
// the innermost spatial dim), index 1 to dims[1] (H); pad has two
// entries per spatial dim: pad[0]/pad[1] on the low / high side of W,
// pad[2]/pad[3] on the low / high side of H
typedef struct tnop_conv2d_param {
    int32_t stride[2];
    int32_t pad[4];
    int32_t dilation[2];
    int32_t groups; // 1 = dense; groups == C_in is depthwise
} tnop_conv2d_param_t;

typedef struct tnop_permute_param {
    uint8_t order[TN_MAX_DIMS];
} tnop_permute_param_t;

typedef struct tnop_reduce_param {
    tnop_reduce_op_t op;
    int32_t axis;
} tnop_reduce_param_t;

#ifdef __cplusplus
}
#endif
