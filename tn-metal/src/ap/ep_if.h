/*
 * ep_if.h — PCIe EP interface abstraction (scheduler-core fw view)
 *
 * Engine code (ring/dispatch/event/CQ) touches the two resource classes only
 * through this interface:
 *   ① scheduler register block — the same block the host sees over BAR0 (offsets are lpu_regs.h)
 *   ② host memory            — host PA translated through the EP window (ring contents/CQ/RP·EVT
 * shadows)
 *
 * Until the EP model lands:
 *   - production mode: placeholder window addresses (configs/sim_c906.h, ★ marked)
 *   - selftest mode (CONFIG_AP_SELFTEST): the register block and host area are
 *     faked in local DRAM, selftest plays host; the window translation formula
 *     is identical for both → zero engine changes.
 */
#ifndef FW_AP_EP_IF_H
#define FW_AP_EP_IF_H

#include <stddef.h>
#include <stdint.h>

#include "lpu/lpu_core_mb.h"

/* Initialization (called first in main). Production fills placeholder
   addresses; selftest points at the locally faked area. */
void sched_ep_init(void);

/* Access by offset within the scheduler register block (off is a register
   offset from lpu_regs.h) */
volatile uint32_t *sched_reg(uint32_t off);

/* Host memory read/write (pa is the host-side physical/bus address, from a
   descriptor or a shadow-base register) */
void sched_ep_host_read(uint64_t pa, void *dst, size_t n);
void sched_ep_host_write(uint64_t pa, const void *src, size_t n);

/* LPU core mailbox array (dispatch interface; layout in lpu_core_mb.h).
 * Selftest points at the locally faked array (the 0x90000000 placeholder
 * address has no backing on the sim machine); production is SCHED_MB_BASE
 * (★ placeholder). */
volatile lpu_core_mb_t *sched_mb(uint32_t cid);

#endif /* FW_AP_EP_IF_H */
