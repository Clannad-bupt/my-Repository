/*
 * AP fw — card variant config (tn-card machine + EP model + shared-file DRAM, M5)
 *
 * Differences from configs/sim_c906.h (th-c906 sim placeholders) — production
 * addresses all settled:
 *   - register block @ 0x50000000 (LPU_REG_BLOCK_CARD_BASE, the card-side
 *     alias of BAR0)
 *   - the EP host window is an identity map: WIN_BASE == PA_BASE == 0x80000000 →
 *     host_dev_addr(pa) == pa, so ring/CQ/shadow/descriptor (all in card DRAM,
 *     see the lpu_dram_map.h host arena) addresses pass through, zero changes
 *     to engine code
 *   - mailbox @ 0x78000000 (cross-process shared file, a fixed region of
 *     tn_card.c)
 * Constants come uniformly from include/lpu/lpu_dram_map.h (do not change
 * values in this file).
 */
#ifndef SCHED_CONFIG_H
#define SCHED_CONFIG_H

#include "lpu/lpu_dram_map.h"

#define FW_RAM_BASE LPU_DRAM_AP_FW_BASE
#define FW_UART0_BASE 0x04140000u
#define FW_UART_BAUD 0u
#define FW_UART_REG_SHIFT 2u
#define FW_UART_REG_WIDTH 1u

/* --- production addresses (aligned with the tn-accel-card EP model) --- */
#define SCHED_REG_BLOCK_BASE LPU_REG_BLOCK_CARD_BASE
/* identity map: host window == host PA base → card DRAM addresses pass through */
#define SCHED_HOST_WIN_BASE LPU_DRAM_BASE
#define SCHED_HOST_WIN_SIZE LPU_DRAM_SIZE
#define SCHED_HOST_PA_BASE LPU_DRAM_BASE

/* LPU core mailbox array (64B per core, protocol in lpu_core_mb.h; same
 * source as scalar fw configs/card/sim_c906.h FW_MB_BASE) */
#define SCHED_MB_BASE LPU_MB_BASE

/* --- local selftest (CONFIG_AP_SELFTEST; not enabled in the card variant,
   kept for compile compatibility) --- */
#define SCHED_LOCAL_HOST_PA_BASE SCHED_HOST_PA_BASE
#define SCHED_LOCAL_HOST_BYTES 0x4000u /* 16KB faked host area */

#endif /* SCHED_CONFIG_H */
