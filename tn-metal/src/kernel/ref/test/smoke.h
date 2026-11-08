// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/test/smoke.h
 *
 * Shared pieces of the ref backend smoke test: the CHECK assertion, the
 * memory map, descriptor/expectation helpers, and the standard binary-op
 * loopback. Test families are split per kernel.h section, mirroring the
 * engine files:
 *   test_mm_fp.c     — mm matmul fp family: nnn/ntn/ntt BF16 -> FP32
 *   test_mm_bq.c     — mm matmul bq family: FP8/FP4 + UE8M0/UE5M3 scales
 *   test_mm_conv.c   — mm conv: WAlign kernel FP8/BF16 -> BF16/FP32
 *   test_regops.c    — sys/tdr register ops: moves, field rewrite, wrap
 *   test_ldst.c      — ldst engine: load / store (HWALIGN + WALIGN)
 *   test_vec_move.c  — vec data movement: move
 *   test_ldst_gather.c — ldst gather/scatter (only top engine; core
 *                      shared with dmr)
 *   test_vec_arith.c — vec elementwise arith: add/sub/mul/div/max/min/...
 *   test_vec_mac.c   — vec mac: out += left*right
 *   test_vec_unary.c — vec unary arith: square/abs/floor
 *   test_vec_logic.c — vec logic: and/or/xor/nxor/not
 *   test_vec_shift.c — vec shifts: lsl/lsr/asr/rol/ror (tensor + imm)
 *   test_vec_reduce.c — vec reduce max/min/sum/absmax + cumsum
 *   test_vec_cmp.c   — vec compare: cmp gt/le/eq/ge + csel
 *   test_vec_special.c — vec special funcs: normal/getmant/rsqrt/taylor/rcp
 *   test_vec_cast.c  — vec cast: plain dtype conversion
 *   test_vec_quant.c — vec quant/dequant: fp8/mxfp4/nxfp4 + int8
 *   test_vec_conv.c  — vec conv: FP32, unreorganized (IC,OC,KH,KW) kernel
 *   test_vec_depthwise.c — vec depthwise: FP8/BF16 -> BF16/FP32 + bias
 *   test_vec_pool.c  — vec pool: max/min/sum windows
 *   test_vec_matmul.c — vec matmul: nnn FP32, strict FP32-step chain
 *   test_dmr_move.c  — dmr movement: ld/st (cw/nc transposes, reduce_add)
 *                      + mv (URF / Global)
 *   test_dmr_gather.c — dmr rearrange: gather/scatter (Global placement,
 *                      row idx, scatter reduce_add)
 *   test_dmr_sort.c  — dmr sort & stats: sort/sort_mrg/histogram_count
 * Adding a family file: implement suite_<family>(), register the call in
 * smoke.c's main, and add the file to CMakeLists' smoke_ref sources.
 * Chip-specific families (pinning one chip generation's concrete choices,
 * e.g. the capability set) live in test/<chip>/, named
 * suite_<chip>_<topic>(), registered under the TN_CHIP_<CHIP> macro —
 * shared families assert only behavioral invariants derived from
 * ctx->info, so they are reused across chips unchanged.
 * ========================================================================== */
#pragma once

#include "kernel/chip.h"
#include "kernel/common/context_internal.h"
#include "kernel/kernel.h"
#include "kernel/ref/violate.h"

#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern int g_fail;

#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (cond) {                                                                                \
            printf("  ok    %s\n", (msg));                                                         \
        } else {                                                                                   \
            printf("  FAIL  %s\n", (msg));                                                         \
            g_fail++;                                                                              \
        }                                                                                          \
    } while (0)

/* ---- violation capture (ref backend policy, see violate.h) ----
 * Negative cases wrap the illegal call between TNVI_BEGIN(expect) and
 * TNVI_END(): the capture handler longjmps back on the first violation
 * (safe — every check runs before any side effect), and TNVI_END
 * asserts the violation fired with `expect` as a message substring.
 * Outside a capture the default handler (print + abort) applies. */
void tnvi_begin(const char *expect_sub);
void tnvi_caught(void);
void tnvi_missed(void);
extern jmp_buf tnvi_jmp;

#define TNVI_BEGIN(expect)                                                                         \
    do {                                                                                           \
        tnvi_begin(expect);                                                                        \
        if (setjmp(tnvi_jmp) == 0) {
#define TNVI_END()                                                                                 \
    tnvi_missed();                                                                                 \
    }                                                                                              \
    else {                                                                                         \
        tnvi_caught();                                                                             \
    }                                                                                              \
    }                                                                                              \
    while (0)

/* ---- test memory map (byte offsets into the simulated global memory) ---- */
#define GA 0u     /* input A */
#define GB 8192u  /* input B */
#define GO 16384u /* output  */
/* URF-side descriptor bases (offsets within a lane window; windows are
 * equal-capacity with consecutive bases, see the tensor.cpp address map) */
#define UA 0u
#define UB 256u
#define UO 512u

#define NELEM 32u /* (1,1,4,8) */

/* tdr_alloc + tdr_shadow_write; on failure prints FATAL and counts */
uint8_t mk_desc(tnkr_ctx_t *ctx, const tdr_t *d);

/* Test-side independent integer expectation (kept separate from the
 * implementation on purpose) */
uint32_t t_expect_int(int64_t v, unsigned bits, int is_signed, ovf_mode_t ovf);

typedef void (*binop_fn)(tdr_idx_t, tdr_idx_t, tdr_idx_t, ovf_mode_t);

/* Standard binary case: A/B go through the global->URF->op->global loop,
 * output lands in OUT */
void run_binop(tnkr_ctx_t *ctx, binop_fn fn, tn_dtype_t idt, tn_dtype_t odt, ovf_mode_t ovf,
               const void *A, const void *B, void *OUT);

/* div/max/min/and/or/xor/nxor have no ovf parameter; adapted to the
 * common signature */
void op_div(tdr_idx_t o, tdr_idx_t l, tdr_idx_t r, ovf_mode_t m);
void op_max(tdr_idx_t o, tdr_idx_t l, tdr_idx_t r, ovf_mode_t m);
void op_min(tdr_idx_t o, tdr_idx_t l, tdr_idx_t r, ovf_mode_t m);
void op_and(tdr_idx_t o, tdr_idx_t l, tdr_idx_t r, ovf_mode_t m);
void op_or(tdr_idx_t o, tdr_idx_t l, tdr_idx_t r, ovf_mode_t m);
void op_xor(tdr_idx_t o, tdr_idx_t l, tdr_idx_t r, ovf_mode_t m);
void op_nxor(tdr_idx_t o, tdr_idx_t l, tdr_idx_t r, ovf_mode_t m);

typedef void (*unop_fn)(tdr_idx_t, tdr_idx_t);

/* Standard unary case: A goes through the global->URF->op->global loop,
 * output lands in OUT (for not and the unary arith family) */
void run_unary(tnkr_ctx_t *ctx, unop_fn fn, tn_dtype_t idt, tn_dtype_t odt, const void *A,
               void *OUT);

/* Integer binary case: expectations are computed independently on the
 * test side, then compared element by element; unsigned inputs are first
 * wrapped to the dtype width (matching the in-memory bit pattern) */
void check_int_binop(tnkr_ctx_t *ctx, const char *name, binop_fn fn, char op, tn_dtype_t idt,
                     tn_dtype_t odt, ovf_mode_t ovf, const int64_t *A, const int64_t *B);

/* ---- test family entries (one per file; main calls them in kernel.h
 * section order) ---- */
void suite_mm_fp(tnkr_ctx_t *ctx);
void suite_mm_bq(tnkr_ctx_t *ctx);
void suite_mm_conv(tnkr_ctx_t *ctx);
void suite_regops(tnkr_ctx_t *ctx);
void suite_ldst(tnkr_ctx_t *ctx);
void suite_ldst_gather(tnkr_ctx_t *ctx);
void suite_vec_move(tnkr_ctx_t *ctx);
void suite_vec_arith(tnkr_ctx_t *ctx);
void suite_vec_mac(tnkr_ctx_t *ctx);
void suite_vec_unary(tnkr_ctx_t *ctx);
void suite_vec_logic(tnkr_ctx_t *ctx);
void suite_vec_shift(tnkr_ctx_t *ctx);
void suite_vec_reduce(tnkr_ctx_t *ctx);
void suite_vec_cmp(tnkr_ctx_t *ctx);
void suite_vec_special(tnkr_ctx_t *ctx);
void suite_vec_cast(tnkr_ctx_t *ctx);
void suite_vec_quant(tnkr_ctx_t *ctx);
void suite_vec_conv(tnkr_ctx_t *ctx);
void suite_vec_depthwise(tnkr_ctx_t *ctx);
void suite_vec_pool(tnkr_ctx_t *ctx);
void suite_vec_matmul(tnkr_ctx_t *ctx);
void suite_dmr_move(tnkr_ctx_t *ctx);
void suite_dmr_gather(tnkr_ctx_t *ctx);
void suite_dmr_sort(tnkr_ctx_t *ctx);

/* ---- chip-specific family entries (test/<chip>/, compiled only for the
 * matching -DTN_CHIP; the TN_CHIP_<CHIP> macro is injected by CMake and
 * guards both the declaration and the registration in main) ---- */
#ifdef TN_CHIP_TNA1
void suite_tna1_dtype(tnkr_ctx_t *ctx);
#endif
