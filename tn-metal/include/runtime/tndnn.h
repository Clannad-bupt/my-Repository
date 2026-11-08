// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: include/runtime/tndnn.h
 *
 * DNN operator library — the graph-building API for neural-network
 * inference ops, on top of the host runtime (tnrt.h): the caller declares
 * tensors (device address + shape), appends op nodes to a graph, then
 * enqueues the whole graph on a stream.
 *
 * Execution model:
 *   - One handle serves both run modes: tndnn_create() binds a loaded
 *     kernel module, a stream, the run mode and the core count; every
 *     tndnn_<op>() call then behaves according to the mode. Both modes
 *     share the scalar operator implementations.
 *   - Graph mode (TNDNN_RUNMODE_GRAPH): tndnn_<op>() only records a
 *     node; tndnn_graph_build() synchronously serializes the recorded
 *     graph into device memory, after which the handle is read-only;
 *     tndnn_graph_execute() launches the whole graph asynchronously as
 *     one tn_scalar_run_graph() task and may be called repeatedly.
 *   - Operator mode (TNDNN_RUNMODE_OP): each tndnn_<op>() call
 *     serializes a launch message (runtime/tnmsg.h) and immediately
 *     enqueues a launch of the unified tn_scalar_run_operator() entry
 *     through tnrt_kernel_launch(); the op payload header carries the
 *     opcode. tndnn_graph_build/execute are not valid in this mode.
 *   - Completion is observed through tnrt stream/event synchronization.
 *   - A graph is fully static: every tensor descriptor, including its
 *     device address, is frozen at build time; changing any tensor
 *     requires destroying and re-recording the graph. Repeated
 *     execution rewrites buffer contents, not addresses. The graph
 *     executor consumes an ordered node list and has no DAG dependency
 *     model.
 *
 * Tensor model:
 *   - Tensors are described by tn_tensor_t (tndef.h): a by-value
 *     descriptor of a caller-allocated device address plus a densely
 *     packed, contiguous shape, dims[0] innermost. A reshape or a slice
 *     that preserves the element order is pure descriptor arithmetic
 *     (address offset) and never creates a node; a permute is
 *     materialized with tndnn_permute.
 *   - No shape inference: every node output, including intermediates, is
 *     declared by the caller with its full shape; shapes/dtypes are
 *     validated at add time (TN_ERR_PARAM on violation).
 *   - The graph copies descriptors at add time; the caller keeps
 *     ownership of the device buffers, which must stay valid until the
 *     execution completes.
 *   - Eltwise / activation / norm / scale / clamp / rope nodes allow
 *     out == in (in-place); matmul, fattention, conv2d, permute, cast
 *     and gather nodes do not; scatter is in-place by definition.
 *
 * Design scope — transformer inference first:
 *   - matmul: dense BF16, dynamic-quant activation x activation,
 *     block-quant weight x activation, a two-level-scaled FP4 variant,
 *     and integer weight-only (w4/w8 x BF16) variants — activations are
 *     block-quantized on the fly inside the node where needed, so there
 *     are no graph-level quant/dequant nodes and weights arrive
 *     quantized from the model; batched with broadcasting; the quant
 *     paths require NT layout, the output orientation is selectable
 *     (N or T); all variants take an optional BF16/FP32 bias
 *   - eltwise binary / unary, scalar scale, clamp
 *   - activations and gated units (SwiGLU / GeGLU / ReGLU)
 *   - softmax over a selectable axis
 *   - RMSNorm / LayerNorm, RoPE (incl. YaRN scaling)
 *   - fattention: fused softmax(q @ k^T) @ v over dense BF16 K/V (the
 *     flash-attention form), with none / small-causal-tile / full mask
 *     modes; quant or paged KV attention is composed from matmul +
 *     softmax + matmul
 *   - conv2d: dense BF16 with stride / pad / dilation / groups
 *   - data movement: permute, dtype cast, embedding row gather,
 *     in-place row scatter (the KV-cache write)
 *   - axis reduce
 *   Host tensor I/O and cross-device copies are tnrt transfers, not
 *   nodes.
 *
 * Threading: same rule as tnrt — a handle may be used from any thread,
 * but concurrent use of the same handle requires external
 * synchronization. Note that repeated executions of one built graph
 * share the frozen buffers (intermediates and outputs), so executions
 * must be ordered by the stream rather than issued concurrently.
 *
 * Dependency: tndef.h + ops/params.h + runtime/tnrt.h; no kernel-layer
 * headers.
 *
 * TODO (v2): graph I/O signature with per-launch addresses, MROPE /
 * VISION rope modes, pool family, cumsum, on-device argsort / top_k,
 * paged / quant-KV attention, FP16
 * ========================================================================== */
#pragma once

#include <stdint.h>

#include "ops/params.h"
#include "runtime/tnrt.h"
#include "tndef.h"

#ifdef __cplusplus
extern "C" {
#endif

// ================================================
// global definitions
// ================================================

// opaque dual-mode context, created/destroyed in pairs
typedef struct tndnn_ctx *tndnn_handle_t;

typedef enum tndnn_runmode {
    TNDNN_RUNMODE_GRAPH = 0, // graph execute mode
    TNDNN_RUNMODE_OP = 1,    // operator execute mode
} tndnn_runmode_t;

// create a handle bound to a loaded kernel module, a stream, the run mode
// and the core count used by every launch (> 0, carried in the
// launch-message payload and passed to the driver); the kernel module
// must stay loaded until the handle is destroyed and all its launches
// complete; on failure *handle is NULL
tn_status_t tndnn_create(tndnn_handle_t *handle, const tnrt_kernel_t kernel,
                         const tnrt_stream_t stream, tndnn_runmode_t runmode, uint32_t num_cores);

// destroy a handle and its device-side graph (if built); the caller must
// ensure no execution is in flight. User tensor buffers and the kernel
// module are not released
tn_status_t tndnn_destroy(tndnn_handle_t handle);

// only graph mode: synchronously finalize and serialize the recorded graph
// into device memory; after a successful build the handle is read-only —
// adding nodes or rebuilding returns TN_ERR_STATE
tn_status_t tndnn_graph_build(tndnn_handle_t handle);

// only graph mode: asynchronously execute the built graph on the bound
// stream; may be called repeatedly. All tensor addresses are frozen at
// build time — repeated execution rewrites buffer contents, not addresses
tn_status_t tndnn_graph_execute(tndnn_handle_t handle);

// ================================================
// tensor malloc/free
// ================================================

tn_status_t tndnn_tensor_malloc(tndnn_handle_t handle, tn_tensor_t *tensor);
tn_status_t tndnn_tensor_free(tndnn_handle_t handle, tn_tensor_t *tensor);

// ================================================

// ================================================
// matmul nodes
// ================================================

// dense matmul: out = left x right [+ bias], shapes per mode
//   left/right: BF16 activations; out: BF16/FP32; accumulation is at
//   least FP32
//   bias: 1D {N} BF16/FP32 over the output N dim, or NULL
tn_status_t tndnn_matmul(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *left,
                         const tn_tensor_t *right, const tn_tensor_t *bias,
                         const tnop_matmul_param_t *param);

// dynamic-quant matmul: both operands are BF16 activations, block-
// quantized on the fly inside the node (qdtype, group g_size along K)
// and multiplied on the quant tensor core; NTN/NTT modes only
//   qdtype: TN_DTYPE_FP8_E4M3 / TN_DTYPE_FP4; g_size: 16 / 32
//   out: BF16/FP32; bias: 1D {N} BF16/FP32, or NULL
tn_status_t tndnn_matmul_dq(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *left,
                            const tn_tensor_t *right, const tn_tensor_t *bias,
                            const tnop_matmul_dq_param_t *param);

// weight-quant matmul: right is a block-quant weight with per-group
// scales; left is a BF16 activation, block-quantized on the fly inside
// the node to the weight's dtype and group size; NTN/NTT modes only
//   right: FP8_E4M3 / FP4 {N,K}; right_scale [K/g,N,...] UE8M0 (UE5M3
//   for FP4) — the group size g is implied by the scale shape,
//   g = K / right_scale->dims[0] (supported: 16, 32)
//   out: BF16/FP32; bias: 1D {N} BF16/FP32, or NULL
tn_status_t tndnn_matmul_bq(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *left,
                            const tn_tensor_t *right, const tn_tensor_t *right_scale,
                            const tn_tensor_t *bias, const tnop_matmul_bq_param_t *param);

// two-level-scaled variant of tndnn_matmul_bq, FP4 only: block scales
// are FP8_E4M3 (not restricted to powers of two), and each scaled
// operand carries an additional tensor-level scalar scale; NTN/NTT
// modes only
//   left: BF16 activation, block-quantized on the fly inside the node;
//     left_global_scale is applied before the per-group block scales
//     are formed
//   right: FP4 weight {N,K}; right_scale [K/g,N,...] FP8_E4M3 — the
//     group size g is implied by the scale shape,
//     g = K / right_scale->dims[0] (supported: 16, 32)
//   out: BF16/FP32, multiplied by out_global_scale
//   bias: 1D {N} BF16/FP32, or NULL
tn_status_t tndnn_matmul_bq_gs(tndnn_handle_t handle, const tn_tensor_t *out,
                               const tn_tensor_t *left, const tn_tensor_t *right,
                               const tn_tensor_t *right_scale, const tn_tensor_t *bias,
                               const tnop_matmul_bq_gs_param_t *param);

// 4-bit integer weight-only matmul: right packs two 4-bit weights per
// S8 byte along K (low nibble = even element); the node unpacks each
// byte into 2 INT8 values and dequantizes them to BF16
// (top_vec_dequant_int8); left is a BF16 activation, not quantized;
// NTN/NTT modes only
//   right: S8 {N,K/2} — the logical K is 2 * right->dims[0]
//   right_scale_off: BF16/FP32, shape [1,N,K/g,2] — the scale and
//     offset of each group interleaved in the innermost dim:
//     w = scale * q + offset
//   g_size: group length along K; must equal K / right_scale_off->dims[1]
//   out: BF16/FP32; bias: 1D {N} BF16/FP32, or NULL
tn_status_t tndnn_matmul_w4bf16(tndnn_handle_t handle, const tn_tensor_t *out,
                                const tn_tensor_t *left, const tn_tensor_t *right,
                                const tn_tensor_t *right_scale_off, const tn_tensor_t *bias,
                                const tnop_matmul_w4bf16_param_t *param);

// 8-bit variant of tndnn_matmul_w4bf16: right is S8 {N,K} (no nibble
// unpacking); right_scale_off, bias and g_size as above
tn_status_t tndnn_matmul_w8bf16(tndnn_handle_t handle, const tn_tensor_t *out,
                                const tn_tensor_t *left, const tn_tensor_t *right,
                                const tn_tensor_t *right_scale_off, const tn_tensor_t *bias,
                                const tnop_matmul_w8bf16_param_t *param);

// ================================================
// element-wise nodes
// ================================================

// out = a <op> b; all three operands share one shape (same-shape only)
tn_status_t tndnn_eltwise(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *a,
                          const tn_tensor_t *b, const tnop_eltwise_param_t *param);

// out = op(in)
tn_status_t tndnn_unary(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *in,
                        const tnop_unary_param_t *param);

// out = in * scale + bias; in/out FP
tn_status_t tndnn_scale(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *in,
                        const tnop_scale_param_t *param);

// out = min(max(in, lo), hi); in/out FP
tn_status_t tndnn_clamp(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *in,
                        const tnop_clamp_param_t *param);

// ================================================
// fused NN nodes
// ================================================

// out = act(in), element-wise; in/out same shape
tn_status_t tndnn_activation(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *in,
                             const tnop_activation_param_t *param);

// gated linear unit, split form: out = act(gate) * value;
// gate/value/out same shape; the interleaved single-tensor form lowers
// to two half views + this node
tn_status_t tndnn_glu(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *gate,
                      const tn_tensor_t *value, const tnop_glu_param_t *param);

// softmax over dims[axis]: out = softmax(in) along dims[axis]
//   in/out same shape, BF16/FP32; axis in [0, ndims)
tn_status_t tndnn_softmax(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *in,
                          const tnop_softmax_param_t *param);

// y = x * rsqrt(mean(x^2) + eps) [* weight] over dims[0];
// weight: 1D {dims[0]} BF16/FP32, or NULL for the unweighted norm; the
// graph compiler may fuse a following scale node into this node
tn_status_t tndnn_rmsnorm(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *in,
                          const tn_tensor_t *weight, const tnop_rmsnorm_param_t *param);

// y = (x - mean) * rsqrt(var + eps) [* weight] [+ bias] over dims[0];
// weight/bias: 1D {dims[0]} BF16/FP32, or NULL
tn_status_t tndnn_layernorm(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *in,
                            const tn_tensor_t *weight, const tn_tensor_t *bias,
                            const tnop_layernorm_param_t *param);

// rotary position embedding (RoPE): in [head_dim,n_head,n_token,...];
// pos S32 {n_token} gives each token's position; freq_factors: optional
// 1D {n_dims/2} FP32 (long-rope style per-dim factors); params follow
// the standard RoPE / YaRN formulation
tn_status_t tndnn_rope(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *in,
                       const tn_tensor_t *pos, const tn_tensor_t *freq_factors,
                       const tnop_rope_param_t *param);

// fused attention (flash-style): out = softmax(q @ k^T * scale + mask)
// @ v, computed in tiles without materializing the score matrix
//   q [head_dim,n_head,n_q,...], k/v [head_dim,n_head_kv,n_kv,...],
//   out [head_dim,n_head,n_q,...]; q/k/v share head_dim; q/k/v BF16,
//   out BF16/FP32, accumulation is at least FP32
//   n_head % n_head_kv == 0 (GQA/MQA head sharing); batch dims
//   (dims[3+]) broadcast pairwise as in matmul
//   mask usage depends on mask_type (score tensor is
//   [n_kv,n_head,n_q,...], dims[0] = n_kv innermost):
//     NONE : mask must be NULL
//     SMALL: mask [S,1,S] BF16/FP32 (e.g. a 128x128 causal triangle),
//       added to the SxS score tiles straddling the causal diagonal;
//       tiles fully below it pass, tiles fully above are dropped —
//       causal masking without materializing a full [n_kv,n_q] mask;
//       the diagonal is offset by n_kv - n_q, i.e. query row i attends
//       to keys 0..i + (n_kv - n_q)
//     FULL : mask [n_kv,M1,M2,M3] BF16/FP32 with M1 >= n_head,
//       n_q % M2 == 0, batch % M3 == 0 (broadcast over head/batch
//       dims), added into the logits
tn_status_t tndnn_fattention(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *q,
                             const tn_tensor_t *k, const tn_tensor_t *v, const tn_tensor_t *mask,
                             const tnop_fattention_param_t *param);

// ================================================
// conv nodes
// ================================================

// 2D convolution (cross-correlation): out = conv(in, weight) [+ bias]
//   in [W,H,C_in,N], weight [K_w,K_h,C_in/groups,C_out], out
//   [W_out,H_out,C_out,N] — dims[0] innermost, i.e. the logical layouts
//   are NCHW / OIHW
//   W_out = (W + pad[0] + pad[1] - dilation[0]*(K_w-1) - 1) / stride[0]
//   + 1, likewise for H_out with pad[2]/pad[3]
//   in/weight BF16; out BF16/FP32; accumulation is at least FP32
//   bias: 1D {C_out} BF16/FP32, or NULL
tn_status_t tndnn_conv2d(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *in,
                         const tn_tensor_t *weight, const tn_tensor_t *bias,
                         const tnop_conv2d_param_t *param);

// ================================================
// data movement nodes
// ================================================

// out = permute(in, order): out->dims[i] = in->dims[order[i]] for
// i < in->ndims — order must be a permutation of [0, in->ndims);
// entries past in->ndims are ignored; dtype and element count are
// unchanged and out->ndims == in->ndims (no shape inference: the
// caller declares out with the permuted shape)
tn_status_t tndnn_permute(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *in,
                          const tnop_permute_param_t *param);

// out = (out->dtype) in, element-wise dtype conversion, same shape;
// out->dtype == in->dtype degenerates to a plain copy. Conversion
// rules: FP->FP RNE, FP->int RTZ, int->FP exact; block-quant dtypes
// are not convertible (weights arrive quantized from the model)
tn_status_t tndnn_cast(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *in);

// row gather over dim1 (embedding lookup):
//   in [D0,D1,D2,D3], index [R,D2,D3] S32/S64/U32, out [D0,R,D2,D3]:
//   out[:,i,:,:] = in[:,index[i],:,:]; a negative or out-of-range index
//   yields a zero row (conditional-select patterns, e.g. MoE routing
//   with -1 skip)
tn_status_t tndnn_gather(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *in,
                         const tn_tensor_t *index);

// row scatter over dim1, in place — the KV-cache write:
//   dst [D0,D1,D2,D3] read-write, src [D0,R,E2,E3], index [R,I2,I3]
//   S32/S64/U32 with D2 % I2 == 0, D3 % I3 == 0, E2 == D2, E3 == D3:
//   dst[:,index[i],:,:] = src[:,i,:,:]; a negative index skips the row;
//   untouched dst rows are preserved
tn_status_t tndnn_scatter(tndnn_handle_t handle, const tn_tensor_t *dst, const tn_tensor_t *src,
                          const tn_tensor_t *index);

// ================================================
// reduce nodes
// ================================================

// reduce one axis: 0 = innermost dim, 1 = next-to-innermost; the reduced
// dim of out is 1, the rest match in
//   a mean is expressed as SUM followed by tndnn_scale(1/dims[axis])
tn_status_t tndnn_reduce(tndnn_handle_t handle, const tn_tensor_t *out, const tn_tensor_t *in,
                         const tnop_reduce_param_t *param);

#ifdef __cplusplus
}
#endif
