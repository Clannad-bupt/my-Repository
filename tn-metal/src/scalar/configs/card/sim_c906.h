/*
 * scalar fw — card variant config (tn-card machine + shared-file DRAM, M5)
 *
 * Differences from configs/sim_c906.h (th-c906 sim placeholder):
 *   - fw image @ 0x80400000 (LPU_DRAM_SCALAR_FW_BASE, three-way consistent
 *     with the link_card.ld RAM ORIGIN and QEMU -M ...,fw-load-addr=0x80400000)
 *   - mailbox @ 0x78000000 (cross-process shared file, fixed region in
 *     tn_card.c; same source as the ap fw configs/card/sim_c906.h SCHED_MB_BASE)
 * Constants come uniformly from include/lpu/lpu_dram_map.h (do not change
 * values in this file).
 */
#ifndef FW_CONFIG_H
#define FW_CONFIG_H

#include "lpu/lpu_dram_map.h"
#include "lpu/lpu_prelink.h" /* LPU_OP_BASE (operator module region base; only selftest ③ uses it) */

#define FW_RAM_BASE LPU_DRAM_SCALAR_FW_BASE
#define FW_UART0_BASE 0x04140000u
#define FW_UART_BAUD                                                                               \
    0u /* 0 = skip divisor programming (QEMU UART does not care about baud rate)                   \
        */
#define FW_UART_REG_SHIFT 2u /* th-c906: DW8250 regshift 2 */
#define FW_UART_REG_WIDTH 1u

/* fixed landing spot for operator modules (only selftest ③'s prelink header
 * check uses it; in card form the runtime loads operators into the pool
 * LPU_DRAM_POOL_BASE, not the fw region) */
#define FW_OP_BASE LPU_OP_BASE

/* this core's number — runtime (start.S reads mhartid → main →
 * fw_core_id_init writes it into this core's TLS slot). This constant is
 * only the selftest expected value (card variant -smp 8, cores 0..7). */
#define FW_CORE_ID 0u

/* scheduler → LPU core mailbox array base (64B per core, see lpu_core_mb.h) */
#define FW_MB_BASE LPU_MB_BASE

#endif /* FW_CONFIG_H */
