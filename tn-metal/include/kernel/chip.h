// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: include/kernel/chip.h
 *
 * Device / context layer — capabilities and lifecycle
 *
 * Responsibilities:
 *   1. Capability access (tnkr_spec_info): TDR slots / URF capacity etc. —
 *      the single source of truth for hardware capabilities, a compile-time
 *      constant set. TDR_NUM / GPR_NUM / SYS_NUM in kernel/kernel_types.h
 *      are the ISA 5-bit operand encoding limits; the spec header asserts
 *      its counts against them at compile time
 *   2. Context lifecycle (tnkr_init / tnkr_deinit): create / destroy tnkr_ctx_t
 *
 * Everything that touches context CONTENTS — the TDR shadow, the slot
 * allocator, the URF bump state, tnkr_ctx_default — is kernel-private
 * (src/kernel/common/context_internal.h): on-chip op code uses kernel.h
 * tdr-class instructions and the kernel/tensor.h helpers, and the
 * host-side configuration path (driver / kernel tests) uses the shadow
 * write APIs declared there.
 *
 * Context model:
 *   Single global context. kernel.h instructions carry no ctx parameter
 *   and bind implicitly to the default context from tnkr_init.
 *   One core's world: each core runs its own scalar + firmware instance,
 *   and every register file (TDR/GPR/SYS) and the URF is per-core — the
 *   context models exactly the core its firmware runs on, so there is no
 *   current-core switch. core_num in the spec is informational (how many
 *   such independent cores the chip carries).
 *   Multi-context is reserved for a later generation; the migration path
 *   is adding a leading ctx parameter to kernel.h signatures — this
 *   header's API is already ctx-organized, only kernel.h would change.
 *
 * Dependency: kernel.h does not depend on kernel/chip.h (the instruction
 *             layer stays stateless pure semantics).
 * ========================================================================== */
#pragma once

#include <stdint.h>

#include "tndef.h"

#ifdef __cplusplus
extern "C" {
#endif

/* opaque context handle; defined in src/kernel/common/context_internal.h */
typedef struct tnkr_ctx tnkr_ctx_t;

/* ---- hardware capability description ----
 * Compile-time constant set, statically initialized from the selected
 * chip's TN_SPEC_* count macros; tnkr_spec_info() exposes it and tnkr_init
 * copies it into the context.
 * dtype_mask: bit i set means tn_dtype_t value i is supported,
 *             build with TNKR_DTYPE_BIT(TN_DTYPE_xxx) */
#define TNKR_DTYPE_BIT(dt) (1u << (dt))

/* ---- chip spec (single source of truth) ----
 * Each chip generation's counts are defined once, as macros in
 * src/kernel/common/spec_<name>.h (compile-time ISA-limit asserts
 * included); the context's static storage (context_internal.h) and the
 * capability set below (context.c) both derive from them, so both
 * backends share identical capabilities by construction (a cross-check
 * prerequisite).
 *
 * Multi-chip strategy:
 *   - The spec is selected at compile time via -DTN_CHIP=<name> (macro
 *     TN_CHIP_SPEC_HDR), orthogonal to backend targets: each
 *     (backend, chip) pair builds its own static library; one build tree
 *     is pinned to one chip — use separate build trees for side-by-side
 *     chips
 *   - New generation = 1) src/kernel/common/spec_<name>.h count macros,
 *                        spec_id incremented
 *                     2) <name> appended to TN_CHIPS in CMakeLists.txt
 *                    Details: skill tn-add-chip
 *   - Backends never diverge within a generation; differences exist only
 *     between generations
 *   - Compiler-emitted blobs must record spec_id, checked before load */

/* All register-file / URF counts are PER CORE: a chip carries core_num
 * independent cores, each with its own scalar + firmware instance and its
 * own TDR/GPR/SYS files and URF */
typedef struct tnkr_chip_info {
    uint32_t spec_id;         /* chip spec id, from TN_SPEC_ID */
    uint32_t smc_num;         /* SMC lanes per core */
    uint32_t core_num;        /* independent cores per chip */
    uint32_t tdr_num;         /* TDR slots per core */
    uint32_t gpr_num;         /* GPR count per core */
    uint32_t sys_num;         /* SYS register count per core */
    uint32_t urf_bytes;       /* URF capacity per core (bytes), bounds URF addresses */
    uint32_t bank_num;        /* banks per SMC URF split */
    uint32_t urf_width_bytes; /* URF width; HWALIGN plane / WALIGN row
                                 alignment granularity, tied to the
                                 src/kernel/common/tdr.h frozen value */
    uint32_t dtype_mask;      /* supported dtypes, see TNKR_DTYPE_BIT */
} tnkr_chip_info_t;

/* ---- capabilities ----
 * The kernel library runs on the chip's scalar core: there is exactly one
 * device (the one the firmware runs on), so there is nothing to query at
 * runtime — capabilities are a compile-time constant set, statically
 * initialized from the selected spec's count macros (spec_<chip>.h, the
 * single source of truth). tnkr_spec_info returns a pointer to it; the
 * pointer is never NULL and stays valid for the program's lifetime. */
const tnkr_chip_info_t *tnkr_spec_info(void);

/* ---- lifecycle ----
 * tnkr_init:   initialize the default context (static storage), copying the
 *            spec_info capabilities into it; on success returns ctx with
 *            *err=TN_SUCCESS, on failure NULL with *err=TN_ERR_STATE
 *            (double init); err may be NULL
 * tnkr_deinit: deactivate the context (no resources to release);
 *            NULL-safe; afterwards the implicit instruction context
 *            is invalid — issuing further instructions is undefined */
tnkr_ctx_t *tnkr_init(int *err);
void tnkr_deinit(tnkr_ctx_t *ctx);

#ifdef __cplusplus
}
#endif
