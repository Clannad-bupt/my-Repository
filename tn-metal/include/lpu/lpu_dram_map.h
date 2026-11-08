/*
 * lpu_dram_map.h — unified card-side DRAM address map (M5, 2026-10-30)
 *
 * Three consumers must stay consistent:
 *   - the runtime device backend (backend_dev.c: pool base + host arena carving)
 *   - the card-variant configs of both fw images (fw/{ap,scalar}/configs/card/sim_c906.h)
 *   - QEMU tn_card.c and the launch rig (scripts/run_vm.sh; synced by value + cross-referenced
 * comments)
 *
 * Physical shape: an 8G file /dev/shm/tn-dram.bin; the two card-side QEMU
 * instances, ap and scalar, share the same file via memory-backend-file,share=on
 * (the chip DRAM model); the mailbox is a separate 4K shared file
 * /dev/shm/tn-mb.bin (a fixed region of tn_card.c).
 *
 *  layout (8G file offset == card-side DRAM address - LPU_DRAM_BASE):
 *   0x80000000 ┌ ap fw image (4M reserved; fw-load-addr=0x80000000, loaded via -bios)
 *   0x80400000 ├ scalar fw image + per-CPU slots (4M reserved; fw-load-addr=0x80400000)
 *   0x80800000 ├ (reserved, do not use — gap for the fw reserved region)
 *   0x81000000 ├ host arena 16M: rings/CQ/RP shadow/EVT shadow (carved by the dev backend
 * host_alloc) 0x82000000 ├ runtime pool: modules/params/data buffers (up to the 8G top)
 *   0x100000000┘
 *
 * Design constraints:
 *   - all card DRAM addresses < 4G → the 32-bit shadow base registers
 *     (Q_RP_SHADOW_BASE/EVT_SHADOW_ADDR) are satisfied naturally.
 *   - the ap fw EP host window uses an identity mapping (SCHED_HOST_WIN_BASE ==
 *     SCHED_HOST_PA_BASE == LPU_DRAM_BASE) → host_dev_addr(pa) == pa, so
 *     ring/CQ/shadow/descriptor addresses pass through unchanged and the fw
 *     engine code needs zero changes (the ep_if.c base-swap mechanism).
 */
#ifndef LPU_DRAM_MAP_H
#define LPU_DRAM_MAP_H

/* --- mailbox (separate 4K shared file, a fixed region of tn_card.c; not inside tn-dram.bin) --- */
#define LPU_MB_BASE 0x78000000u /* 8 cores × 64B slots, protocol in lpu_core_mb.h */
#define LPU_MB_REGION_SIZE 0x1000u

/* --- card DRAM body (/dev/shm/tn-dram.bin, 8G) --- */
#define LPU_DRAM_BASE 0x80000000u
#define LPU_DRAM_SIZE 0x200000000ull /* 8G (= BAR1 window size) */

/* ap fw image region (tn-card instance: -M ...,fw-load-addr=LPU_DRAM_AP_FW_BASE) */
#define LPU_DRAM_AP_FW_BASE 0x80000000u
#define LPU_DRAM_AP_FW_RESERVE 0x00400000u /* 4M */

/* scalar fw image + per-CPU slot region (scalar instance: fw-load-addr=LPU_DRAM_SCALAR_FW_BASE) */
#define LPU_DRAM_SCALAR_FW_BASE 0x80400000u
#define LPU_DRAM_SCALAR_FW_RESERVE 0x00400000u /* 4M */

/* host arena: rings/CQ/RP shadow/EVT shadow (carved by the dev backend's
 * "host_alloc"; the semantic equivalent of the original HOST_ALLOC ioctl — the
 * spec intent was host-coherent memory, but the card side has no host window,
 * so it lands in a reserved card DRAM region + identity mapping; deviation
 * documented in docs/rt/lpu-sw-stack.md §6) */
#define LPU_HOST_ARENA_BASE 0x81000000u
#define LPU_HOST_ARENA_SIZE 0x01000000u /* 16M */

/* runtime pool: operator modules/params/data buffers (up to the 8G top) */
#define LPU_DRAM_POOL_BASE 0x82000000u
#define LPU_DRAM_POOL_SIZE (LPU_DRAM_SIZE - (uint64_t)(LPU_DRAM_POOL_BASE - LPU_DRAM_BASE))

/* card-side alias address of the BAR0 register block (tn_accel_card.c
 * reg_alias; kept in sync with the fw card variant's SCHED_REG_BLOCK_BASE) */
#define LPU_REG_BLOCK_CARD_BASE 0x50000000u

#endif /* LPU_DRAM_MAP_H */
