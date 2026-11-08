// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: include/kernel/kernel.h
 *
 * Kernel interface
 *
 * Naming: <type>_<engine>_<op>[_<variant>]
 *   type:    sys / tdr / top / dmr;  engine: mm / vec / ldst
 *   variant: quant type / required data layout, e.g. bq / ntn
 *   Instructions avoid fp/int distinctions where possible; the dtype
 *   comes from the TDR.
 *
 * Argument order: operands (tdr_idx_t) -> aggregated attrs -> mode/immediate
 *   operands  5-bit register indices (0..31):
 *             tdr_idx_t (tensor descriptors, set up by tdr-class instrs),
 *             gpr_idx_t (scalar GPRs of the per-core scalar coprocessor),
 *             sys_idx_t (system registers);
 *             three independent index spaces, referenced by index only
 *   modes     *_t enums, encoded directly into the instruction
 *   attrs     conv_attr_t / pool_attr_t aggregates (const pointer)
 *
 * Execution model:
 *   Instructions act on the core their firmware instance runs on: every
 *   register file (TDR/GPR/SYS) is per-core (kernel/chip.h).
 *   - sys/tdr: executed in issue order by the current core's scalar
 *     coprocessor
 *   - top_*:   issued to an engine (mm/vec/ldst) and run asynchronously;
 *     return does not imply completion; in-engine issue order is kept,
 *     cross-engine order is not; completion/fence primitives TBD
 *   - ref (tna1) models no timing: "issue = complete" — a legal refinement
 *     of the async semantics in a timeless model; cross-check by result
 * ========================================================================== */
#pragma once

#include "kernel_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * sys — system register access
 * Move scalars between SYS and GPR (output-first):
 *   sys_read : GPR[gpr_dst] <- SYS[sys_src]
 *   sys_write: SYS[sys_dst] <- GPR[gpr_src]
 * Uninitialized / out-of-range index -> A-class violation in the ref
 * backend (violate.h: report + abort); error reporting on hardware is
 * up to the execution-model spec;
 * SYS count = spec.sys_num
 * ========================================================================== */
void sys_read(gpr_idx_t gpr_dst, sys_idx_t sys_src);

void sys_write(sys_idx_t sys_dst, gpr_idx_t gpr_src);

/* ==========================================================================
 * tdr — TDR access
 * GPRs are 64-bit, so a full 128-bit TDR read/write takes a GPR pair:
 *   dst0/src0 = low 64 bits, dst1/src1 = high 64 bits
 * Argument order is output-first.
 * Field rewrite semantics (tdr_insert* / tdr_add*): one instruction
 *   touches exactly one 64-bit half of the TDR — offset (tdr_half_t)
 *   selects it (TDR_HALF_LO = bits 0:63, TDR_HALF_HI = bits 64:127; any
 *   other encoding is a violation) and LSB/MSB (0..63) address the
 *   field inside that half, so a single source GPR / 64-bit immediate
 *   covers every field:
 *   insert: TDR[offset*64+MSB : offset*64+LSB] <- low (MSB-LSB+1) bits
 *           of value, rest unchanged
 *   add:    TDR[offset*64+MSB : offset*64+LSB] <- (field + value)
 *           truncated to the field width
 *   Express offset/LSB/MSB with src/kernel/common/tdr.h TDR_<field>_LO /
 *   TDR_<field>_W combos (offset = (tdr_half_t)(TDR_X_LO / 64),
 *   LSB = TDR_X_LO % 64, MSB = LSB + TDR_X_W - 1), no magic numbers
 * Note: these rewrite raw bits — no tdr_check / alloc checks;
 *       uninitialized / out-of-range / illegal field -> A-class
 *       violation in ref (violate.h);
 *       checked host-side configuration goes through the kernel-private
 *       tdr_shadow_write* (src/kernel/common/context_internal.h)
 * ========================================================================== */
void tdr_read(gpr_idx_t gpr_dst0, gpr_idx_t gpr_dst1, tdr_idx_t tdr_src);

void tdr_write(tdr_idx_t tdr_dst, gpr_idx_t gpr_src0, gpr_idx_t gpr_src1);

void tdr_add(tdr_idx_t tdr_dst, gpr_idx_t gpr_src, tdr_half_t offset, uint8_t LSB, uint8_t MSB);

void tdr_add_imm(tdr_idx_t tdr_dst, uint64_t imm, tdr_half_t offset, uint8_t LSB, uint8_t MSB);

void tdr_insert(tdr_idx_t tdr_dst, gpr_idx_t gpr_src, tdr_half_t offset, uint8_t LSB, uint8_t MSB);

void tdr_insert_imm(tdr_idx_t tdr_dst, uint64_t imm, tdr_half_t offset, uint8_t LSB, uint8_t MSB);

/* ==========================================================================
 * top — compute instructions (engine: mm / vec / ldst, async, see above)
 * Engines only interpret TDR bits, never tdr_check
 * (same as hardware: an illegal descriptor behaves as hardware does)
 * ========================================================================== */

/* ---- top / mm : matmul ----
 * Modes: nnn/ntn/ntt = per-operand transpose of (left, right, output)
 * dtype families:
 *   fp - left/right BF16, output BF16/FP32
 *   bq - block-quant: left/right in {FP8, FP4},
 *        scales in {UE8M0, UE5M3}, output BF16/FP32
 *   (both families accumulate the K-reduction in FP32; a BF16 output is
 *   the FP32 accumulation cast at write-out)
 *   bq scale semantics (spec pseudocode): the K range splits into
 *   g_size blocks; each block is reduced WITHOUT scales (block dot
 *   product), then scaled once —
 *     UE8M0:  scale_b = 2^(ls_b + rs_b - 127 - 127)          (exact)
 *     FP4+UE5M3: scale_b = the ls_b*rs_b product rounded in the UE5M3
 *               domain (e5m3 multiply, RNE) before the FP32 widen
 *     acc += scale_b * blk_b   (block-granularity scaling)
 *   Homogeneous scale pairing only (mxfp4: both UE8M0; nxfp4: both
 *   UE5M3); a mixed pair is a violation.
 *
 * Operand shapes (all HWAlign):
 *   nnn: left (1,M,1,K), right (1,K,1,N), output (1,M,1,N)
 *   ntn: left (1,M,1,K), right (1,N,1,K), output (1,M,1,N)
 *   ntt: left (1,M,1,K), right (1,N,1,K), output (1,N,1,M)
 *   bias (fp/bq): nnn/ntn (1,SMC_NUM,1,N) BF16/FP32 — a C-broadcast
 *   add against the (1,M,1,N) product: output channel c adds bias row
 *   c % SMC_NUM (the rows are per-lane data, NOT one replicated table);
 *   ntt: (1,N,1,1) — a W-broadcast add against the (1,N,1,M) product.
 *   bias exists only under bias_mode == PER_CHANNEL; with
 *   BIAS_MODE_NONE the bias index is ignored (it need not name a
 *   configured TDR)
 *   left_scale / right_scale (bq): (1,M,1,K//g_size) / (1,N,1,K//g_size)
 *
 * Constraints (spec):
 *   - acc=ACCUMULATE: output dtype FP32 only; the accumulator must match
 *     output in shape and start address
 *   - ACCUMULATE and PER_CHANNEL bias are mutually exclusive: a call that
 *     applies bias must not also accumulate and vice versa ("apply bias
 *     first, then accumulate" is the recommended call sequence, not an
 *     in-call order)
 *   - g_size selects the block-quant group width (fp data only); the
 *     scale W extent is K//g, so g must divide K
 */
void top_mm_matmul_fp_nnn(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right, tdr_idx_t bias,
                          acc_mode_t acc, bias_mode_t bias_mode);

void top_mm_matmul_fp_ntn(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right, tdr_idx_t bias,
                          acc_mode_t acc, bias_mode_t bias_mode);

void top_mm_matmul_fp_ntt(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right, tdr_idx_t bias,
                          acc_mode_t acc, bias_mode_t bias_mode);

void top_mm_matmul_bq_ntn(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right, tdr_idx_t bias,
                          tdr_idx_t left_scale, tdr_idx_t right_scale, group_size_t g_size,
                          acc_mode_t acc, bias_mode_t bias_mode);

void top_mm_matmul_bq_ntt(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right, tdr_idx_t bias,
                          tdr_idx_t left_scale, tdr_idx_t right_scale, group_size_t g_size,
                          acc_mode_t acc, bias_mode_t bias_mode);

/* ---- top / mm : convolution ----
 *   input  (N,IC,H,W)          FP8/BF16,  HWAlign
 *   kernel (1,OC,KH*KW,IC)     FP8/BF16,  WAlign (conv-only layout;
 *                              the external side already carries this
 *                              shape from compile-time reorganization —
 *                              the load materializes the per-row
 *                              urf-width padding)
 *   output (N,OC,OH,OW)        BF16/FP32, HWAlign
 *   bias   (1,OC,1,1)          BF16/FP32, PER_CHANNEL only (with
 *                              BIAS_MODE_NONE the bias index is ignored)
 * Output shape (floor; windows that do not fit are dropped):
 *   H_ins = (H-1)*(insert+1)+1, KHeff = (KH-1)*dilation+1,
 *   OH = (H_ins+pad_t+pad_b-KHeff)/stride_h + 1, OW likewise;
 *   padding cells and insert holes contribute zero
 * Geometry aggregated in conv_attr_t (stride<=15, pad/insert/dilation
 * <=3, kernel_h/kernel_w<=15); kernel_h*kernel_w must equal kernel.dh
 * Numeric semantics (golden model, see mm.cpp): one K = KH*KW*IC
 * reduction per output element accumulated in FP32 (golden keeps the
 * tail in double), bias added after the reduction, RNE + saturate
 * encode at write-out; the chip's MAC order and intermediate rounding
 * are chip-backend tolerance questions
 */
void top_mm_conv_fp(tdr_idx_t output, tdr_idx_t input, tdr_idx_t kernel, tdr_idx_t bias,
                    const conv_attr_t *attr, bias_mode_t bias_mode);

/* ---- top / ldst : load / store ----
 * load: external memory -> URF;  store: URF -> external memory
 * shape (N,C,H,W), dtype S8..U32/FP8/BF16/FP32, identical on both sides
 * layout constraints:
 *   URF side:  HWAlign; load also accepts WAlign (conv kernel; per-row
 *              urf-width alignment, see tensor.cpp)
 *   external:  Continuous / Free — "external" may be another SMC/LPU's
 *              URF, addressed as GLOBAL (TDR loc=GLOBAL)
 * mode: 0=normal, 1=NC transpose; every other encoding is a violation
 *       (CW_TRANS arrives with dmr; 3 is undefined)
 *
 * stride pairing: FREE is banned on URF except for mv, so FREE appears
 * only on the external side; the stride TDR follows the FREE operand it
 * describes (load in_str / store out_str). URF operands take no stride.
 */
void top_ldst_load(tdr_idx_t output, tdr_idx_t input, tdr_idx_t in_str, ldst_mode_t mode);

void top_ldst_store(tdr_idx_t output, tdr_idx_t out_str, tdr_idx_t input, ldst_mode_t mode);

/* ---- top / ldst : gather / scatter ----
 * top_ldst_gather (top.ldst.gat): per-channel indexed pick.
 *   input (N,C,H,W) FP8/BF16/FP32 + S8/S16/S32 + U8/U16/U32, HWAlign
 *   index (1,C,1,K) U8/U16/U32 HWAlign — one coordinate vector per
 *     channel, K = pick count
 *   output: mode W: (N,C,H,K); mode H: (N,C,K,W) — input dtype, HWAlign
 *   out(n,c,h,k) = in(n,c,h,idx(0,c,0,k))         (mode W)
 *   out(n,c,k,w) = in(n,c,idx(0,c,0,k),w)         (mode H)
 *   OOB coordinate (>= the indexed extent) fills per value:
 *     0: zero, 1: one, 2: -1 (unsigned dtypes take the width-truncated
 *     all-ones pattern, the natural -1 encoding), 3: keep — the output
 *     element is not written
 *   LUT use (gather as table lookup): the table input's C == SMC_NUM
 *     and the index C dim is chunked by SMC_NUM per issue
 *
 * top_ldst_scatter (top.ldst.scat): inverse of gather;
 *   input is the compacted (N,C,H,K)/(N,C,K,W) side, output the expanded
 *   (N,C,H,W) side; idx.dw must equal the input's picked extent
 *   (K == in.dw for mode W, K == in.dh for mode H);
 *   OOB coordinates are dropped (no fill, no value parameter);
 *   duplicate coordinates: last write wins in ascending k order
 */
void top_ldst_gather(tdr_idx_t output, tdr_idx_t input, tdr_idx_t index, axis_mode_t mode,
                     pad_val_t value);

void top_ldst_scatter(tdr_idx_t output, tdr_idx_t input, tdr_idx_t index, axis_mode_t mode);

/* ---- top / vec : move ----
 * top_vec_move (top.vec.mv): URF-to-URF copy, never crosses SMC;
 *   the only instruction accepting FREE layout on URF
 *   input layout: HWAlign/ZeroStride/Free/Constant;  output: HWAlign/Free
 *   in_str/out_str: companion stride slot of the FREE operand
 *   (ignored for non-FREE; pass anything)
 */
void top_vec_move(tdr_idx_t output, tdr_idx_t out_str, tdr_idx_t input, tdr_idx_t in_str);

/* ---- top / vec : element-wise arithmetic ----
 * Common rules:
 *   left/right (opd0/opd1): (N,C,H,W), same dtype, layout
 *     ZeroStride/HWAlign/Constant. Constant placement: mul/mac/max/min
 *     take it on right (opd1) only, add/sub on either side but not
 *     both, div never on right
 *   output (res0): (N,C,H,W), HWAlign
 *   Promotion: output width >= input (add/sub/mul/mac);
 *     max/min/div keep the input dtype; U sub may output S or U
 *   mode (ovf): integers only, 0=saturate 1=truncate;
 *     FP is fixed saturate (clamp ±max, no inf) + RNE
 * Per-op dtypes:
 *   add/sub : all integers (S8/S16/S32, U8/U16/U32) + FP8/BF16/FP32
 *   mul     : same as add, but FP output is BF16/FP32 only
 *   mac     : FP only (no integer arithmetic, hence no mode); out
 *             doubles as the accumulator; output BF16/FP32 only
 *   div     : FP only (BF16/FP32), no mode
 *   max/min : integers + FP, output = input dtype
 */
void top_vec_add(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right, ovf_mode_t mode);

void top_vec_sub(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right, ovf_mode_t mode);

void top_vec_mul(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right, ovf_mode_t mode);

void top_vec_div(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right);

void top_vec_mac(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right);

void top_vec_max(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right);

void top_vec_min(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right);

/* Unary arithmetic: input/output both HWAlign
 *   square: in FP8/BF16/FP32, out BF16/FP32
 *   abs   : in S8/S16/S32 + FP8/BF16/FP32, out = in
 *   floor : in FP8/BF16/FP32, out = in
 */
void top_vec_square(tdr_idx_t output, tdr_idx_t input);

void top_vec_abs(tdr_idx_t output, tdr_idx_t input);

void top_vec_floor(tdr_idx_t output, tdr_idx_t input);

/* ---- top / vec : logic ----
 * Integers only (S8/S16/S32, U8/U16/U32); left/right
 * ZeroStride/HWAlign/Constant (Constant on right only), output
 * HWAlign = input dtype
 */
void top_vec_and(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right);

void top_vec_or(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right);

void top_vec_xor(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right);

void top_vec_nxor(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right);

/* top_vec_not: unary; input/output HWAlign, output = input dtype */
void top_vec_not(tdr_idx_t output, tdr_idx_t input);

/* ---- top / vec : shifts ----
 * input  (opd0): (N,C,H,W) S8/S16/S32/U8/U16/U32, ZeroStride/HWAlign
 * shift  (opd1): (N,C,H,W) integer, ZeroStride/HWAlign;
 *   dtype may differ from input; amount must be < input width
 * shift_imm: uint5 immediate (0..31) when mode=CONSTANT — immediates go
 *   through mode+imm, not Constant layout (spec constraint)
 * output (res0): input dtype, HWAlign
 *   logic right fills 0; arith right fills sign; rotate is circular
 */
void top_vec_logic_left_shift(tdr_idx_t output, tdr_idx_t input, tdr_idx_t shift, uint8_t shift_imm,
                              shift_mode_t mode);

void top_vec_logic_right_shift(tdr_idx_t output, tdr_idx_t input, tdr_idx_t shift,
                               uint8_t shift_imm, shift_mode_t mode);

void top_vec_rotate_left_shift(tdr_idx_t output, tdr_idx_t input, tdr_idx_t shift,
                               uint8_t shift_imm, shift_mode_t mode);

void top_vec_rotate_right_shift(tdr_idx_t output, tdr_idx_t input, tdr_idx_t shift,
                                uint8_t shift_imm, shift_mode_t mode);

void top_vec_arith_right_shift(tdr_idx_t output, tdr_idx_t input, tdr_idx_t shift,
                               uint8_t shift_imm, shift_mode_t mode);

/* ---- top / vec : reductions ----
 * input:  (N,C,H,W) S8/S16/S32, U8/U16/U32, FP8/BF16/FP32; HWAlign
 * output: (N,C,H or 1,W or 1) — reduced axis becomes 1, rest as input;
 *         input dtype, HWAlign
 * axis:   0=W, 1=H (W/H only)
 */
void top_vec_reduce_max(tdr_idx_t output, tdr_idx_t input, axis_mode_t axis);

void top_vec_reduce_min(tdr_idx_t output, tdr_idx_t input, axis_mode_t axis);

void top_vec_reduce_sum(tdr_idx_t output, tdr_idx_t input, axis_mode_t axis);

void top_vec_reduce_absmax(tdr_idx_t output, tdr_idx_t input, axis_mode_t axis);

/* ---- top / vec : prefix sum ----
 * W axis only (spec constraint); include: 0=exclusive, 1=inclusive
 * input/output: (N,C,H,W) S8/S16/S32, U8/U16/U32, FP8/BF16/FP32,
 *   HWAlign, output = input dtype
 * Overflow (spec): integers saturate per element;
 *   FP is fixed saturate + RNE
 */
void top_vec_cumsum(tdr_idx_t output, tdr_idx_t input, cumsum_include_t include);

/* ---- top / vec : compare ----
 * left/right: (N,C,H,W) integers + FP, same dtype,
 *   HWAlign/ZeroStride/Constant (Constant on right only)
 * output: (N,C,H,W) HWAlign; 0/1 for integer inputs, 0.0/1.0 for FP
 */
void top_vec_cmp_gt(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right);

void top_vec_cmp_lt(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right);

void top_vec_cmp_le(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right);

void top_vec_cmp_eq(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right);

void top_vec_cmp_ge(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right);

/* top_vec_cmp_select_* (top.vec.csel_*):
 *   res0 = left OP right ? left_result : right_result (element-wise)
 *   left/right: HWAlign/ZeroStride/Constant (Constant on right only)
 *   left_result/right_result: HWAlign/ZeroStride; auxiliary state is
 *     configured by the implementation along with the instruction,
 *     transparent to the caller
 *   output: HWAlign, input dtype
 */
void top_vec_cmp_select_gt(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right, tdr_idx_t left_result,
                           tdr_idx_t right_result);

void top_vec_cmp_select_lt(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right, tdr_idx_t left_result,
                           tdr_idx_t right_result);

void top_vec_cmp_select_le(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right, tdr_idx_t left_result,
                           tdr_idx_t right_result);

void top_vec_cmp_select_eq(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right, tdr_idx_t left_result,
                           tdr_idx_t right_result);

void top_vec_cmp_select_ge(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right, tdr_idx_t left_result,
                           tdr_idx_t right_result);

/* ---- top / vec : special functions ----
 * input/output (opd0/res0): (N,C,H,W), BF16/FP32 (normal/getmant also FP8),
 *   HWAlign; output = input dtype unless noted
 *   normal   : FP exponent extraction, res = exp - bias; zero/subnormal/
 *              inf/nan -> 0; output dtype fixed S8
 *   getmant  : FP mantissa extraction (hidden leading 1 restored);
 *              zero/subnormal/inf/nan -> 0;
 *              output U8/U16/U32 for input FP8/BF16/FP32
 *   rsqrt    : reciprocal sqrt, magic-number guess + Newton iterations
 *              (FP32 2 rounds, BF16 1); negative -> NaN, zero -> ±inf
 *   taylor   : polynomial evaluation (Horner); coeff holds the coefficients
 *              (1,SMC_NUM,1,step), step = expansion order, input dtype.
 *              One authored (1,1,1,step) table is C-broadcast at load
 *              time, so every lane holds an identical copy (C never
 *              exceeds SMC_NUM); reads wrap at lane c % SMC_NUM.
 *              The kernel is function-agnostic; prefit tables for the
 *              usual SFU activations (exp2/sigmoid/tanh/gelu_tanh/silu,
 *              FP32 + BF16) live in src/kernel/ref/taylor_lut.h,
 *              generated by gen_taylor_lut.py (minimax, absolute error)
 *   reciprocal: reciprocal (LUT + quadratic interpolation); table is an
 *              S32 LUT (1,SMC_NUM,1,384), three 128-entry tables.
 *              One authored (1,1,1,384) table is C-broadcast at load
 *              time, so every lane holds an identical copy (C never
 *              exceeds SMC_NUM); reads wrap at lane c % SMC_NUM;
 *              subnormal encodings unsupported: subnormal inputs flush to
 *              signed zero before the operation (result +-inf), subnormal
 *              results flush to signed zero
 */
void top_vec_normal(tdr_idx_t output, tdr_idx_t input);

void top_vec_getmant(tdr_idx_t output, tdr_idx_t input);

void top_vec_rsqrt(tdr_idx_t output, tdr_idx_t input);

void top_vec_taylor(tdr_idx_t output, tdr_idx_t input, tdr_idx_t coeff);

void top_vec_reciprocal(tdr_idx_t output, tdr_idx_t input, tdr_idx_t table);

/* ---- top / vec : cast & quantization ----
 * All operands HWAlign; quant instruction shapes are (1,M,1,N)
 * cast: plain conversion, no quant params; round: 0=RNE, 1=RTZ (only
 *   meaningful for FP32->int; everything else fixed RNE);
 *   rules (spec): low->high precision converts directly; same-width sign
 *   change S->U clamps negatives to 0, U->S saturates; high->low:
 *   integers saturate, FP32->BF16 saturate+RNE;
 *   no direct INT <-> low-precision FP (FP4/FP8/BF16) conversion
 *   (INT converts through FP32 only)
 * fp8 quant/dequant: block scale dtype UE8M0 (stored e+127 pattern),
 *   g_size: 0=16, 1=32; the scale is 2^(floor(log2(amax)) - emax) per
 *   group (OCP MX convention, emax = 8 for E4M3); elements are x/scale
 *   clamped to +-max_finite, then RNE
 * nxfp4 quant: scale dtype UE5M3, round_up(amax/6) per group (NVFP4
 *   semantics: the scale never rounds down, so the amax element never
 *   saturates; the chip has no subnormals — a round-up below the
 *   minimum normal flushes to it), g_size as above; elements multiply
 *   by the scale's reciprocal, resolved through an 8-entry mantissa
 *   LUT (the hardware's 6-decimal constants, no divider), one FP32
 *   multiply, then FP4 RNE
 * mxfp4 quant: scale dtype UE8M0 (2^n), g_size as above; the scale is
 *   2^ceil(log2(amax/6)) per group — the smallest power of two keeping
 *   the scaled amax <= 6, so the amax element never saturates
 * int8 dequant: scale and min stacked in one descriptor
 *   (1,M,N//g_size,2) BF16 only, formula W = scale*q - min with the
 *   product and the subtract each rounded to BF16 once (the chip MAC
 *   runs in BF16); output BF16 only;
 *   g_size: 0=64, 1=128
 * constraint: N must be divisible by g_size
 */
void top_vec_cast(tdr_idx_t output, tdr_idx_t input, round_mode_t round);

void top_vec_quant_fp8(tdr_idx_t output, tdr_idx_t out_scale, tdr_idx_t input, group_size_t g_size);

void top_vec_dequant_fp8(tdr_idx_t output, tdr_idx_t input, tdr_idx_t in_scale,
                         group_size_t g_size);

void top_vec_quant_nxfp4(tdr_idx_t output, tdr_idx_t out_scale, tdr_idx_t input,
                         group_size_t g_size);

void top_vec_quant_mxfp4(tdr_idx_t output, tdr_idx_t out_scale, tdr_idx_t input,
                         group_size_t g_size);

void top_vec_dequant_int8(tdr_idx_t output, tdr_idx_t input, tdr_idx_t scale_min,
                          group_size_i8_t g_size);

/* ---- top / vec : convolution ----
 *   input  (N,IC,H,W)    FP32, HWAlign
 *   kernel (IC,OC,KH,KW) FP32, HWAlign
 *                        reorganization (contrast the mm engine's
 *                        (1,OC,KH*KW,IC) WAlign): lane oc owns its
 *                        whole (IC,KH,KW) weight slice
 *   output (N,OC,OH,OW)  FP32, HWAlign
 *   bias   (1,OC,1,1)    FP32, PER_CHANNEL only (with BIAS_MODE_NONE
 *                        the bias index is ignored)
 * Geometry and OH/OW derivation: same as the mm conv (conv_attr_t in
 * full: stride<=15, pad/insert/dilation<=3, kernel_h/w 1..15); kernel
 * dh==kernel_h, dw==kernel_w
 * Numeric semantics: the KH*KW*IC reduction is the vec engine's FP32
 * MAC chain (per-step RNE+saturate, same as vec matmul — NOT the mm
 * double tail); tap order kh -> kw -> ic; the bias is appended as one
 * further FP32 add step
 */
void top_vec_conv(tdr_idx_t output, tdr_idx_t input, tdr_idx_t kernel, tdr_idx_t bias,
                  const conv_attr_t *attr, bias_mode_t bias_mode);

/* ---- top / vec : depthwise / pool ----
 * depthwise: input (N,IC,H,W) FP8/BF16, kernel (1,IC,KH,KW) = input
 *   dtype, output (N,IC,OH,OW) BF16/FP32, all HWAlign; bias (1,IC,1,1)
 *   BF16/FP32, PER_CHANNEL only (with BIAS_MODE_NONE the bias index is
 *   ignored); geometry and OH/OW derivation same as the mm conv
 *   (conv_attr_t in full: stride<=15, pad/insert/dilation<=3, kernel
 *   1..15; kernel dh==kernel_h, dw==kernel_w)
 * pool: input (N,C,H,W), output (N,C,OH,OW) = input dtype, all HWAlign;
 *   OH = (H + pad_t + pad_b - KH) / stride_h + 1 (floor; windows that
 *   do not fit are dropped), OW likewise; padding cells contribute the
 *   operation identity (max: -inf, min: +inf, sum: 0); geometry in
 *   pool_attr_t (stride<=15, pad<=3, kernel<=16383)
 * depthwise numeric semantics: the KH*KW reduction is the vec engine's
 *   FP32 MAC chain (per-step RNE+saturate, tap order kh -> kw, same as
 *   vec conv/matmul — NOT the mm double tail); the bias is appended as
 *   one further FP32 add step; the result is encoded to the output
 *   dtype (RNE + saturate)
 */
void top_vec_depthwise(tdr_idx_t output, tdr_idx_t input, tdr_idx_t kernel, tdr_idx_t bias,
                       const conv_attr_t *attr, bias_mode_t bias_mode);

void top_vec_pool_max(tdr_idx_t output, tdr_idx_t input, const pool_attr_t *attr);

void top_vec_pool_min(tdr_idx_t output, tdr_idx_t input, const pool_attr_t *attr);

void top_vec_pool_sum(tdr_idx_t output, tdr_idx_t input, const pool_attr_t *attr);

/* ---- top / vec : matmul ----
 * nnn only (the mm modes ntn/ntt are not provided on this engine):
 *   left (1,M,1,K), right (1,K,1,N), output (1,M,1,N), all HWAlign;
 *   left/right/output FP32 only
 *   bias (PER_CHANNEL): (1,SMC_NUM,1,N) FP32 — a C-broadcast add
 *   against the (1,M,1,N) product: channel c adds bias row c % SMC_NUM
 *   (same convention as mm nnn); with BIAS_MODE_NONE the bias index is
 *   ignored (it need not name a configured TDR)
 * Numeric semantics: the K-reduction is a chain of FP32 MAC steps,
 *   acc0 = 0, acc_{k+1} = RNE+saturate(acc_k + l*r) — each step rounds
 *   once to FP32 (the fused chain the vec engine executes), NOT the
 *   double-tail golden of mm/conv; the chip backend should match
 *   bit-exactly. PER_CHANNEL bias / the old output value (ACCUMULATE)
 *   are each appended as one further FP32 add step after the chain and
 *   are mutually exclusive (same rule as mm)
 */
void top_vec_matmul_nnn(tdr_idx_t output, tdr_idx_t left, tdr_idx_t right, tdr_idx_t bias,
                        acc_mode_t acc, bias_mode_t bias_mode);

/* ==========================================================================
 * dmr — data rearrangement & movement (async engine, same model as top)
 * Placement law: operands sit in URF (HWAlign; WAlign where noted) or
 * Global (Continuous/Free). The movement family (ld/st/mv) is a plain
 * bit copy — identical dtypes on both sides, no numeric interpretation.
 * ========================================================================== */

/* ---- dmr : movement (ld / st / mv) ----
 * dmr_ld (dmr.ld): Global -> URF load; out URF HWAlign/WAlign (conv
 *   kernel; WALIGN only with mode 0=normal), in Global Continuous/Free
 *   (in_str: FREE companion stride, ignored otherwise)
 * dmr_st (dmr.st): URF -> Global store; out Global Continuous/Free
 *   (out_str: FREE companion), in URF HWAlign; reduce=ADD accumulates
 *   into the existing output at the output dtype (FP: decode -> double
 *   add -> RNE + saturate; INT: saturating add — the chip atomic-add
 *   unit's rounding is a chip-backend tolerance question)
 * dmr_mv (dmr.mv): same-location copy (URF<->URF or Global<->Global);
 *   URF side HWAlign, Global side Continuous/Free (out_str/in_str: FREE
 *   companions, pass anything otherwise)
 * mode (2-bit): 0=normal, 1=C-W transpose, 2=N-C transpose — the reverse
 *   order of ldst_mode_t (kernel_types.h); 3 is reserved (silent)
 * shapes (exact permutations of the input extents, mismatch = violation):
 *   normal: out (N,C,H,W); cw: out (N,W,H,C); nc: out (C,N,H,W)
 *   out(n,w,h,c) = in(n,c,h,w) (cw); out(c,n,h,w) = in(n,c,h,w) (nc)
 */
void dmr_ld(tdr_idx_t output, tdr_idx_t input, tdr_idx_t in_str, dmr_mode_t mode);

void dmr_st(tdr_idx_t output, tdr_idx_t out_str, tdr_idx_t input, reduce_mode_t reduce,
            dmr_mode_t mode);

void dmr_mv(tdr_idx_t output, tdr_idx_t out_str, tdr_idx_t input, tdr_idx_t in_str,
            dmr_mode_t mode);

/* ---- dmr : rearrange (gather / scatter) ----
 * Same per-channel indexed-pick semantics as top_ldst_gather/scatter
 * (top/ldst section above) with the dmr differences:
 *   - placement: each operand independently URF (HWAlign) or Global;
 *     the input/index operands take Global FREE (in_str/idx_str: their
 *     companion strides, ignored otherwise), the output stays Continuous
 *     on Global per spec; no stride sharing (each operand strides itself)
 *   - shapes pin N to 1: gather in (1,C,H,W), out (1,C,H,K)/(1,C,K,W);
 *     scatter in (1,C,H,K)/(1,C,K,W), out (1,C,H,W)
 *   - dmr_scatter carries the row-scatter index vector on the H axis:
 *     index (1,C,1,K) mode W / (1,C,K,1) mode H (ldst: (1,C,1,K) both)
 *   - dmr_scatter reduce=ADD accumulates into the existing output at the
 *     output dtype (same accumulate rule as dmr_st); requires a Global
 *     output (violation otherwise); duplicate coordinates accumulate
 */
void dmr_gather(tdr_idx_t output, tdr_idx_t input, tdr_idx_t in_str, tdr_idx_t index,
                tdr_idx_t idx_str, axis_mode_t mode, pad_val_t value);

void dmr_scatter(tdr_idx_t output, tdr_idx_t input, tdr_idx_t in_str, tdr_idx_t index,
                 tdr_idx_t idx_str, reduce_mode_t reduce, axis_mode_t mode);

/* ---- dmr : sort & statistics (sort / sort_mrg / histogram_count) ----
 * Operand law (sort family): values FP8/BF16/FP32 + all 8/16/32-bit
 * integers; coordinates U8/U16/U32; placement per operand URF (HWAlign)
 * or Global (Continuous; sort also takes FREE — sort_mrg/histogram stay
 * Continuous per spec); outputs share opd0's location. The tctx-carried
 * coordinate tensors (dmr.tctx.w0) ride along as plain arguments,
 * bias/scale convention.
 * Shared c_stride (FREE pairs only): when a values/coordinate pair sits
 * in Global FREE the coordinate tensor carries no stride of its own —
 * it resolves with the values tensor's companion stride, one c_str
 * register in element units with each side scaled by its own dtype size
 * (the spec's "bit-width normalized" stride: opd1_c_stride =
 * opd0_c_stride / opd0_dtype_len). opd1<-opd0, res1<-res0; the pair
 * layouts must match (violation otherwise). Continuous is dense with
 * nothing to share, HWAlign derives each side's plane, URF never FREE.
 * Comparison: keys decode exactly to double (all dtypes <= 32 bits);
 * NaN orders above every number and keeps input order among itself;
 * +0/-0 compare equal; equal keys keep input order (stable sort).
 *
 * dmr_sort (dmr.sort): stable sort / topk of (1,C,1,W) per channel;
 *   K = the output W extent (full sort at K == W); res0 (1,C,1,K) the
 *   sorted values, res1 (1,C,1,K) their coordinates — implicit positions
 *   0..W-1 (has_input_idx NONE) or the carried opd1 coordinates
 *   (CARRIED; (1,C,1,W) matching opd0). result picks VALUE / INDEX /
 *   BOTH; an unused output is not touched (pass any slot).
 *   res0_str/opd0_str: FREE companion strides, ignored otherwise; when a
 *   pair is FREE the coordinate side resolves with the values side's
 *   slot (shared c_stride above).
 * dmr_sort_mrg (dmr.sort_mrg): stable merge of two sorted (1,C,1,W)
 *   inputs (same shape/dtype; a call on unsorted data is a violation);
 *   ties pick opd0 first ("opd1 ranks after opd0"); K = output W extent
 *   (<= 2W); implicit coordinates are opd0 positions 0..W-1 then opd1
 *   positions W..2W-1; carried coordinates (opd2/opd3) must be
 *   conflict-free across both (violation otherwise).
 * A coordinate that does not fit the res1 dtype (implicit W-1 / 2W-1 or
 * a carried value) is a violation; K > W (K > 2W) likewise.
 */
void dmr_sort(tdr_idx_t res0, tdr_idx_t res0_str, tdr_idx_t res1, tdr_idx_t opd0,
              tdr_idx_t opd0_str, tdr_idx_t opd1, sort_dir_t ascend, sort_input_idx_t has_input_idx,
              sort_result_t result);

void dmr_sort_mrg(tdr_idx_t res0, tdr_idx_t res1, tdr_idx_t opd0, tdr_idx_t opd1, tdr_idx_t opd2,
                  tdr_idx_t opd3, sort_dir_t ascend, sort_input_idx_t has_input_idx,
                  sort_result_t result);

/* dmr_histogram_count (dmr.histogram_count): per-value tally of
 * opd0 (1,S,1,K) U8/U16/U32 (the topk result, values in [0,E)) into
 * res0 (1,E,1,1) U8/U16/U32 — bucket v counts the values equal to v;
 * placements independent (in and out may sit on different sides); the
 * input takes Global FREE (opd0_str: its companion stride, ignored
 * otherwise), the output stays Continuous on Global per spec.
 * The bucket count E is the res0 C extent: shape-driven, with no tie to
 * the data max (buckets above the observed max simply count zero; an
 * empty input zero-fills every bucket); a value >= E is a violation;
 * counts saturate at the res0 dtype.
 */
void dmr_histogram_count(tdr_idx_t res0, tdr_idx_t opd0, tdr_idx_t opd0_str);

#ifdef __cplusplus
}
#endif
