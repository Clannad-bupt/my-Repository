// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/common/spec_tna1.h
 *
 * tna1 chip counts — the single source of truth: the capability set
 * (context.c g_info) and the context's static storage
 * (context_internal.h) both derive from these macros, and ISA-limit
 * conformance is asserted here at compile time. One spec_<name>.h per
 * chip generation; the build selects it via -DTN_CHIP_SPEC_HDR (see
 * CMakeLists.txt). Steps to add a chip: skill tn-add-chip.
 *
 * All register-file / URF counts are PER CORE: a chip carries
 * TN_SPEC_CORE_NUM independent cores, each with its own scalar + firmware
 * instance and its own TDR/GPR/SYS files and URF.
 * ========================================================================== */
#pragma once

#include "kernel/chip.h"
#include "kernel/kernel_types.h"

#define TN_SPEC_ID 1u /* spec id: increments per chip generation */
#define TN_SPEC_SMC_NUM 32u
#define TN_SPEC_CORE_NUM 8u
#define TN_SPEC_TDR_NUM 32u
#define TN_SPEC_GPR_NUM 32u
#define TN_SPEC_SYS_NUM 32u
#define TN_SPEC_URF_BYTES (1u << 22) /* TBD */
#define TN_SPEC_BANK_NUM 16u
/* same source as the kernel/common/tdr.h frozen value */
#define TN_SPEC_URF_WIDTH_BYTES TDR_URF_WIDTH_BYTES
#define TN_SPEC_DTYPE_MASK                                                                         \
    (TNKR_DTYPE_BIT(TN_DTYPE_U8) | TNKR_DTYPE_BIT(TN_DTYPE_S8) | TNKR_DTYPE_BIT(TN_DTYPE_U16) |    \
     TNKR_DTYPE_BIT(TN_DTYPE_S16) | TNKR_DTYPE_BIT(TN_DTYPE_U32) | TNKR_DTYPE_BIT(TN_DTYPE_S32) |  \
     TNKR_DTYPE_BIT(TN_DTYPE_FP8_E4M3) | TNKR_DTYPE_BIT(TN_DTYPE_FP4) |                            \
     TNKR_DTYPE_BIT(TN_DTYPE_UE8M0) | TNKR_DTYPE_BIT(TN_DTYPE_UE5M3) |                             \
     TNKR_DTYPE_BIT(TN_DTYPE_BF16) | TNKR_DTYPE_BIT(TN_DTYPE_FP32))

/* ISA conformance: the register counts must fit the 5-bit operand
 * encoding caps (kernel/kernel_types.h) */
TN_STATIC_ASSERT(TN_SPEC_TDR_NUM <= TDR_NUM, "tna1: tdr_num over the ISA cap");
TN_STATIC_ASSERT(TN_SPEC_GPR_NUM <= GPR_NUM, "tna1: gpr_num over the ISA cap");
TN_STATIC_ASSERT(TN_SPEC_SYS_NUM <= SYS_NUM, "tna1: sys_num over the ISA cap");
TN_STATIC_ASSERT(TN_SPEC_CORE_NUM >= 1, "tna1: no cores");
TN_STATIC_ASSERT(TN_SPEC_SMC_NUM >= 1 && TN_SPEC_BANK_NUM >= 1, "tna1: no SMCs/banks");
/* the bank split must divide the URF evenly */
TN_STATIC_ASSERT(TN_SPEC_URF_BYTES % (TN_SPEC_SMC_NUM * TN_SPEC_BANK_NUM) == 0,
                 "tna1: urf banks must divide evenly");
