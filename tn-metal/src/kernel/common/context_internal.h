// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/common/context_internal.h
 *
 * Context internals (private header, src/ only)
 *
 * The kernel library runs on the chip's scalar core; there is no device
 * enumeration and no backend interface. Exactly one context exists, in
 * static storage (context.c): tnkr_init initializes it, tnkr_deinit
 * deactivates it, kernel.h instructions bind it via tnkr_ctx_default().
 *
 * Dependency rules:
 *   - Both backends (ref/chip) compile context.c into their static lib
 *   - Shared lifecycle/shadow/allocator logic lives only in context.c
 *
 * This header also holds the kernel-private context APIs that used to
 * sit in kernel/chip.h:
 *   - tnkr_ctx_default / tnkr_urf_bank_size — instruction-layer and
 *     tensor-helper plumbing
 *   - tdr_shadow_write(+_stride) — the checked host-side configuration
 *     path (driver / kernel tests only): validate (tdr_check +
 *     dtype_mask), update the mirror, program the hardware TDR on chip
 *     builds. Production op/helper code never calls these — it programs
 *     descriptors through kernel.h tdr-class instructions (whose ref
 *     implementations are the register file's raw writers); the checked
 *     path exists because the instructions deliberately do not validate
 *     (hardware tdr_write accepts anything)
 *   - tdr_shadow_mark_stride — slot-content classification (software
 *     bookkeeping, not a TDR access)
 *   - tdr_alloc / tdr_free — the slot allocator (software convention)
 * ========================================================================== */
#pragma once

#include "kernel/chip.h"
#include "kernel/common/tdr_util.h"
#include "kernel/kernel_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- context access ---- */

/* default context the instruction layer binds to; NULL before tnkr_init */
tnkr_ctx_t *tnkr_ctx_default(void);

/* ---- URF bank ----
 * Each SMC's URF is split into bank_num equal banks:
 * bank_size = urf_bytes / smc_num / bank_num.
 * NULL ctx -> 0 */
uint32_t tnkr_urf_bank_size(const tnkr_ctx_t *ctx);

/* ---- TDR shadow (scalar-side mirror of the local core's TDR) ----
 * Each slot is a 128-bit physical register holding one of two contents:
 *   descriptor — tdr_shadow_write: validate (tdr_check + dtype_mask) +
 *                update mirror; returns TN_SUCCESS / tdr_check code /
 *                TN_ERR_PARAM
 *   stride     — tdr_shadow_write_stride: companion stride of FREE layout
 *                (referenced by instructions as its own tdr_idx, e.g.
 *                top_ldst_load's in_str); raw bit data, no tdr_check
 * Note: a successful shadow write does not imply hardware has been
 *       programmed — chip builds do that in tnkr_chip_tdr_program */
int tdr_shadow_write(tnkr_ctx_t *ctx, uint8_t idx, const tdr_t *desc);
int tdr_shadow_write_stride(tnkr_ctx_t *ctx, uint8_t idx, const tdr_stride_t *s);

/* ---- TDR slot allocator (pure software convention) ----
 * tdr_alloc: first-fit, returns slot idx (>= 0);
 *            negative error on failure (TN_ERR_PARAM / TN_ERR_NOMEM)
 * tdr_free:  releases the slot and clears its shadow valid bit;
 *            freeing an unallocated slot is TN_ERR_STATE, NULL ctx /
 *            out-of-range idx is TN_ERR_PARAM; TN_SUCCESS on success */
int tdr_alloc(tnkr_ctx_t *ctx);
int tdr_free(tnkr_ctx_t *ctx, uint8_t idx);

/* ---- context structure ----
 * Lifecycle: tnkr_init -> instruction/ops layers -> tnkr_deinit
 * Sizing: all storage is static, sized exactly by the selected chip's
 *         count macros (TN_SPEC_*, common/spec_<chip>.h — the single
 *         source of truth, whose TN_STATIC_ASSERTs already guarantee
 *         conformance with the ISA limits at compile time)
 * Fields:
 *   info       - capability copy of tnkr_spec_info(), filled at init,
 *                read-only afterwards
 *   shadow     - TDR host mirror (device-global); changed only via
 *                tdr_shadow_write* / tdr-class instructions
 *   valid      - bitmap, bit i: shadow[i] configured and not freed
 *   is_stride  - bitmap, bit i: shadow[i] holds a stride (0 = descriptor)
 *   alloc      - bitmap, bit i: slot owned by the allocator
 *   gpr / sys  - the local core's register files (one firmware instance
 *                per core): architectural truth in ref, scalar-side
 *                mirror in chip (input to instruction encoding)
 *   urf_used / urf_region - URF lane-window bump allocator state (both
 *                backends: ltensors need addresses on chip too)
 *   tdr_kind / tdr_dtype - common/tensor.c's per-slot bookkeeping: what
 *                the slot was created as (set_* guards) and its current
 *                dtype (ltensor footprint math); pure software state,
 *                never read back from the descriptor
 *   urf/global - simulated memory, ref builds only (compiled out in chip,
 *                so a chip-side access is a compile error); identical
 *                capacities are a cross-check prerequisite */

/* A chip spec must be selected at compile time via -DTN_CHIP= (the build
 * maps it to TN_CHIP_SPEC_HDR); a missing selection fails here (same
 * pattern as kernel_backend.h) */
#ifndef TN_CHIP_SPEC_HDR
#error "tn-metal: chip spec header undefined, build with: cmake -DTN_CHIP=<spec>"
#endif
#include TN_CHIP_SPEC_HDR

/* shadow slot: two interpretations of one 128-bit physical register, one
 * valid at a time (tracked by is_stride, see kernel/chip.h shadow section) */
typedef union {
    tdr_t desc;
    tdr_stride_t stride;
} tnkr_tdr_slot_t;

/* Simulated global-memory capacity (ref only; static array size). Real
 * global-memory capacity is a board property, not part of the chip spec */
#define TNKR_SIM_GLOBAL_BYTES (1ull << 26)

/* what a slot was created as (common/tensor.c's creators); the set_*
 * guards key off this instead of reading the descriptor back */
typedef enum {
    TNKR_TENSOR_NONE = 0,  /* not created via common/tensor.c */
    TNKR_TENSOR_GLOBAL,    /* tnkr_gtensor (CONTINUOUS) */
    TNKR_TENSOR_URF,       /* tnkr_ltensor (HWALIGN) */
    TNKR_TENSOR_URF_BCAST, /* tnkr_set_bcast-adopted view (ZEROSTRIDE) */
    TNKR_TENSOR_URF_CONST, /* tnkr_lconstant (CONSTANT) */
    TNKR_TENSOR_URF_FREE   /* tnkr_ltensor_free (FREE in a lane window) */
} tnkr_tensor_kind_t;

struct tnkr_ctx {
    tnkr_chip_info_t info;
    tnkr_tdr_slot_t shadow[TN_SPEC_TDR_NUM];
    uint8_t valid[(TN_SPEC_TDR_NUM + 7u) / 8u];
    uint8_t is_stride[(TN_SPEC_TDR_NUM + 7u) / 8u];
    uint8_t alloc[(TN_SPEC_TDR_NUM + 7u) / 8u];
    uint64_t gpr[TN_SPEC_GPR_NUM];
    uint64_t sys[TN_SPEC_SYS_NUM];
    /* URF lane-window bump allocator (common/tensor.c): per-ltensor
     * region records, bases bank-aligned, byte offsets within one lane
     * window; size 0 = the slot is not an ltensor */
    uint32_t urf_used;
    struct {
        uint32_t base;
        uint32_t size;
    } urf_region[TN_SPEC_TDR_NUM];
    uint8_t tdr_kind[TN_SPEC_TDR_NUM];   /* tnkr_tensor_kind_t */
    uint8_t tdr_dtype[TN_SPEC_TDR_NUM];  /* tn_dtype_t */
    uint8_t tdr_layout[TN_SPEC_TDR_NUM]; /* tdr_layout_t */

#if defined(TN_BACKEND_REF)
    uint8_t urf[TN_SPEC_URF_BYTES];
    uint8_t global[TNKR_SIM_GLOBAL_BYTES];
#endif
};

/* dynamic bitmap ops (idx < info.tdr_num guaranteed by callers) */
#define TNKR_BIT_TEST(m, i) (((m)[(i) >> 3] >> ((i) & 7u)) & 1u)
#define TNKR_BIT_SET(m, i) ((m)[(i) >> 3] |= (uint8_t)(1u << ((i) & 7u)))
#define TNKR_BIT_CLR(m, i) ((m)[(i) >> 3] &= (uint8_t)(~(1u << ((i) & 7u))))

/* ---- chip TDR programming hook (TN_BACKEND_CHIP builds only) ----
 * Called by tdr_shadow_write* after the mirror update; programs the
 * hardware TDR with slot idx's new content. ref works off the shadow and
 * never calls it. */
typedef enum {
    TNKR_TDR_PROGRAM_DESC = 0,   /* descriptor */
    TNKR_TDR_PROGRAM_STRIDE = 1, /* FREE companion stride */
} tnkr_tdr_program_kind_t;

void tnkr_chip_tdr_program(tnkr_ctx_t *ctx, uint8_t idx, tnkr_tdr_program_kind_t kind);

/* common/tensor.c's FREE companion: the stride payload lands through
 * tdr-class writes (which flag the slot a descriptor); flip the shadow's
 * is_stride bit back — slot-content classification is software convention,
 * not TDR content */
void tdr_shadow_mark_stride(tnkr_ctx_t *ctx, uint8_t idx);

#ifdef __cplusplus
}
#endif
