// Copyright (c) 2026 ThinkNative. All rights reserved.
// clang-format off
/* ==========================================================================
 * tn-metal: src/kernel/ref/taylor_lut.h
 * GENERATED FILE -- do not edit; regenerate with:
 *     python3 src/kernel/ref/gen_taylor_lut.py
 *
 * Prefit coefficient tables for top_vec_taylor: one (1,1,1,order) authored
 * row per function (Horner order: index k holds the x^k coefficient).
 * Load into a (1,SMC_NUM,1,order) operand with a c_str=0 companion stride
 * so ldst C-broadcasts the row to every lane -- the kernel then reads
 * coeff[lane, k] identically on all lanes (same convention as rcp_lut.h).
 * Each function ships FP32 bit patterns (U32) and BF16 bit patterns (U16,
 * RNE); memcpy the matching array into the coeff tensor backing store.
 * ========================================================================== */
#ifndef TN_REF_TAYLOR_LUT_H
#define TN_REF_TAYLOR_LUT_H

#include <stdint.h>

#define TNKR_TAYLOR_EXP2_LO 0
#define TNKR_TAYLOR_EXP2_HI 1
#define TNKR_TAYLOR_EXP2_ORDER 7u
#define TNKR_TAYLOR_SIGMOID_LO 0
#define TNKR_TAYLOR_SIGMOID_HI 3
#define TNKR_TAYLOR_SIGMOID_ORDER 9u
#define TNKR_TAYLOR_TANH_LO 0
#define TNKR_TAYLOR_TANH_HI 2
#define TNKR_TAYLOR_TANH_ORDER 7u
#define TNKR_TAYLOR_GELU_TANH_LO -4
#define TNKR_TAYLOR_GELU_TANH_HI 4
#define TNKR_TAYLOR_GELU_TANH_ORDER 13u
#define TNKR_TAYLOR_SILU_LO -4
#define TNKR_TAYLOR_SILU_HI 4
#define TNKR_TAYLOR_SILU_ORDER 11u

/* exp2: minimax on [0, 1], order 7, max abs err 2.649e-09 (fp32) / 1.801e-03 (bf16);
 * fp32 and bf16 fitted separately (quantize-and-refit, see generator) */
static const uint32_t TNKR_TAYLOR_EXP2_F32[TNKR_TAYLOR_EXP2_ORDER] = { 0x3F800000u, 0x3F317214u, 0x3E75FEFFu, 0x3D633F41u, 0x3C1EAD8Cu, 0x3AA25423u, 0x39658306u };
static const uint16_t TNKR_TAYLOR_EXP2_BF16[TNKR_TAYLOR_EXP2_ORDER] = { 0x3F80u, 0x3F31u, 0x3E76u, 0x3D63u, 0x3C1Fu, 0x3A9Eu, 0x3970u };

/* sigmoid: minimax on [0, 3], order 9, max abs err 5.391e-07 (fp32) / 1.175e-03 (bf16);
 * fp32 and bf16 fitted separately (quantize-and-refit, see generator) */
static const uint32_t TNKR_TAYLOR_SIGMOID_F32[TNKR_TAYLOR_SIGMOID_ORDER] = { 0x3EFFFFEEu, 0x3E800434u, 0xB9A482D7u, 0xBCA0FBA7u, 0xBB11C2E1u, 0x3B905944u, 0xBAB2AED3u, 0x39404924u, 0xB71DAB0Fu };
static const uint16_t TNKR_TAYLOR_SIGMOID_BF16[TNKR_TAYLOR_SIGMOID_ORDER] = { 0x3F00u, 0x3E80u, 0xBA02u, 0xBC9Du, 0xBB3Cu, 0x3B9Fu, 0xBACAu, 0x3967u, 0xB751u };

/* tanh: minimax on [0, 2], order 7, max abs err 1.176e-04 (fp32) / 2.454e-02 (bf16);
 * fp32 and bf16 fitted separately (quantize-and-refit, see generator) */
static const uint32_t TNKR_TAYLOR_TANH_F32[TNKR_TAYLOR_TANH_ORDER] = { 0x38F6B480u, 0x3F7E51EDu, 0x3D741211u, 0xBF08A54Fu, 0x3E9D9D4Bu, 0xBD91C81Au, 0x3BB8E6E7u };
static const uint16_t TNKR_TAYLOR_TANH_BF16[TNKR_TAYLOR_TANH_ORDER] = { 0x3996u, 0x3F7Du, 0x3DC0u, 0xBF1Au, 0x3EBFu, 0xBDCBu, 0x3C27u };

/* gelu_tanh: minimax on [-4, 4], order 13, max abs err 7.593e-04 (fp32) / 8.014e-02 (bf16);
 * fp32 and bf16 fitted separately (quantize-and-refit, see generator) */
static const uint32_t TNKR_TAYLOR_GELU_TANH_F32[TNKR_TAYLOR_GELU_TANH_ORDER] = { 0x3A46AA05u, 0x3F000000u, 0x3EC973E9u, 0xADED8923u, 0xBD76E83Au, 0x2D292396u, 0x3BEABB78u, 0xABC39D4Au, 0xBA090BFDu, 0x29C553FAu, 0x37AE6D45u, 0xA7107854u, 0xB4B7F363u };
static const uint16_t TNKR_TAYLOR_GELU_TANH_BF16[TNKR_TAYLOR_GELU_TANH_ORDER] = { 0x3AFFu, 0x3F00u, 0x3EC7u, 0x3232u, 0xBD67u, 0xB13Au, 0x3BCBu, 0x2FAAu, 0xB9D7u, 0xAD8Eu, 0x3778u, 0x2AB2u, 0xB46Du };

/* silu: minimax on [-4, 4], order 11, max abs err 4.595e-04 (fp32) / 5.040e-03 (bf16);
 * fp32 and bf16 fitted separately (quantize-and-refit, see generator) */
static const uint32_t TNKR_TAYLOR_SILU_F32[TNKR_TAYLOR_SILU_ORDER] = { 0x39F0ECE2u, 0x3F000000u, 0x3E7D6E00u, 0xAA939949u, 0xBC980624u, 0x299991A6u, 0x3AA5F61Fu, 0xA7DF5503u, 0xB85D032Du, 0x25525EEAu, 0x357C6940u };
static const uint16_t TNKR_TAYLOR_SILU_BF16[TNKR_TAYLOR_SILU_ORDER] = { 0x3A9Cu, 0x3F00u, 0x3E7Bu, 0xAE5Du, 0xBC90u, 0x2D1Du, 0x3A90u, 0xAB30u, 0xB82Cu, 0x2888u, 0x3530u };

/* table-of-tables: iterate to pick a function at runtime */
typedef struct {
    const char *name;
    double lo, hi;      /* range-reduced domain the fit covers */
    uint32_t order;     /* step: number of coefficients */
    const uint32_t *f32;
    const uint16_t *bf16;
} tnkr_taylor_lut_t;

static const tnkr_taylor_lut_t TNKR_TAYLOR_LUTS[] = {
    { "exp2", TNKR_TAYLOR_EXP2_LO, TNKR_TAYLOR_EXP2_HI, TNKR_TAYLOR_EXP2_ORDER, TNKR_TAYLOR_EXP2_F32, TNKR_TAYLOR_EXP2_BF16 },
    { "sigmoid", TNKR_TAYLOR_SIGMOID_LO, TNKR_TAYLOR_SIGMOID_HI, TNKR_TAYLOR_SIGMOID_ORDER, TNKR_TAYLOR_SIGMOID_F32, TNKR_TAYLOR_SIGMOID_BF16 },
    { "tanh", TNKR_TAYLOR_TANH_LO, TNKR_TAYLOR_TANH_HI, TNKR_TAYLOR_TANH_ORDER, TNKR_TAYLOR_TANH_F32, TNKR_TAYLOR_TANH_BF16 },
    { "gelu_tanh", TNKR_TAYLOR_GELU_TANH_LO, TNKR_TAYLOR_GELU_TANH_HI, TNKR_TAYLOR_GELU_TANH_ORDER, TNKR_TAYLOR_GELU_TANH_F32, TNKR_TAYLOR_GELU_TANH_BF16 },
    { "silu", TNKR_TAYLOR_SILU_LO, TNKR_TAYLOR_SILU_HI, TNKR_TAYLOR_SILU_ORDER, TNKR_TAYLOR_SILU_F32, TNKR_TAYLOR_SILU_BF16 },
};

#define TNKR_TAYLOR_LUT_COUNT (sizeof(TNKR_TAYLOR_LUTS) / sizeof(TNKR_TAYLOR_LUTS[0]))

#endif /* TN_REF_TAYLOR_LUT_H */
// clang-format on
