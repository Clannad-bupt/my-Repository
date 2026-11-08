/*
 * lpu_core_mb.h — core-side dispatch mailbox (scheduler-core fw ↔ LPU-core fw)
 *
 * ★★★ provisional protocol (PROVISIONAL) ★★★
 * The exact content of core dispatch messages has not yet been aligned with
 * the RTL/QEMU model and [may change] later.
 * Constraint: both field sets (dispatch message / completion writeback) are
 * defined only in this file; the fw body code must go through this file's
 * field access and must not scatter offsets around. The future QEMU scheduler
 * model (replacing the scheduler-core fw) must also implement this same
 * interface; when that happens, only this file changes.
 *
 * The field set mirrors LPU-SCHED-SPEC §6.5 core start regs:
 *   {cluster_id, block_id, role_in_block, block_N, entry_pc,
 *    param_ptr, cluster_param_ptr, start}
 * Completion = Core_Done semantics (§6.6), implemented here as a done flag + status.
 *
 * Handshake (no interrupts, polling version v0):
 *   scheduler core: fill fields → fence → write start=1
 *   LPU core: poll start==1 → run the operator → write done=1/status → poll start==0
 *   scheduler core: poll done==1 → read status → clear start, clear done → next round
 */
#ifndef LPU_CORE_MB_H
#define LPU_CORE_MB_H

#include <stdint.h>

#include "tndef.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LPU_CORE_MB_MAGIC 0x4C50554Du /* "LPUM" */
#define LPU_CORE_MB_VERSION 0u        /* provisional protocol version */

typedef struct {
    /* --- dispatch message (scheduler core → LPU core) --- */
    uint32_t magic;         /* +0x00 LPU_CORE_MB_MAGIC */
    uint16_t version;       /* +0x04 LPU_CORE_MB_VERSION */
    uint16_t core_id;       /* +0x06 target core (for validation) */
    uint32_t seq;           /* +0x08 dispatch sequence number (monotonically increasing) */
    uint32_t start;         /* +0x0C start flag: the scheduler core writes 1 to trigger */
    uint32_t cluster_id;    /* +0x10 owning cluster */
    uint32_t block_id;      /* +0x14 owning block */
    uint32_t role_in_block; /* +0x18 role within the block (SPMD gang index) */
    uint32_t block_n;       /* +0x1C gang core count N ∈ {1,2,4,8} */
    uint64_t entry_pc;      /* +0x20 operator entry (chip DRAM address) */
    uint64_t param_ptr;     /* +0x28 block params (address of the API info handed down by host) */
    uint64_t cluster_param_ptr; /* +0x30 cluster-level shared params */
    /* --- completion writeback (LPU core → scheduler core) --- */
    uint32_t done;   /* +0x38 Core_Done: the LPU core writes 1 */
    uint32_t status; /* +0x3C operator return value (0 = OK) */
} lpu_core_mb_t;     /* 64B */

TN_STATIC_ASSERT(sizeof(lpu_core_mb_t) == 64, "core mailbox v0 is 64B");

/* one mailbox per core, laid out contiguously; the base comes from the fw config */
#define LPU_CORE_MB_SIZEOF (sizeof(lpu_core_mb_t))
#define LPU_CORE_MB_ARRAY_BYTES (LPU_CORE_MB_SIZEOF * 8u)

/* operator entry signature (operator ABI, see docs/rt/lpu-sw-stack.md):
 * freestanding, PIE image, calling libc is forbidden; returns 0 = success */
typedef uint32_t (*lpu_op_entry_fn)(const void *param);

#ifdef __cplusplus
}
#endif
#endif /* LPU_CORE_MB_H */
