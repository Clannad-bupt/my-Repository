/*
 * AP fw — th-c906 sim environment config (★ placeholder addresses until real
 * silicon / the EP model lands)
 *
 * Production layout (placeholders):
 *   - scheduler register block @ 0x93000000 (BAR0 maps it to the host; the fw
 *     accesses it through the same in-block offsets, see lpu_regs.h)
 *   - EP host memory window @ 0x92000000 (maps host DRAM into the device side)
 *   - LPU core mailbox array @ 0x90000000 (matching the scalar fw FW_MB_BASE)
 * Selftest layout (CONFIG_AP_SELFTEST, local closed loop):
 *   the register block and "host area" are both faked in a static DRAM area
 *   the fw itself can reach; selftest plays host programming registers +
 *   writing the ring + ringing doorbells; zero changes to engine code.
 */
#ifndef SCHED_CONFIG_H
#define SCHED_CONFIG_H

#define FW_RAM_BASE 0x80000000u
#define FW_UART0_BASE 0x04140000u
#define FW_UART_BAUD 0u
#define FW_UART_REG_SHIFT 2u
#define FW_UART_REG_WIDTH 1u

/* --- production placeholder addresses (★ refill once the EP model / QEMU card-side machine lands)
 * --- */
#define SCHED_REG_BLOCK_BASE 0x93000000u /* ★ scheduler register block */
#define SCHED_HOST_WIN_BASE 0x92000000u  /* ★ EP host memory window */
#define SCHED_HOST_WIN_SIZE 0x10000000u  /* ★ 256MB */
#define SCHED_HOST_PA_BASE 0x10000000u   /* ★ host PA base the window maps */

/* LPU core mailbox array (64B per core, protocol in lpu_core_mb.h)
 * ★ placeholder: kept consistent with the core fw's FW_MB_BASE */
#define SCHED_MB_BASE 0x90000000u

/* --- local selftest (CONFIG_AP_SELFTEST) --- */
/* faked host PA base: the address selftest writes from the host's viewpoint
   (ring/CQ/shadows) */
#define SCHED_LOCAL_HOST_PA_BASE SCHED_HOST_PA_BASE
#define SCHED_LOCAL_HOST_BYTES 0x4000u /* 16KB faked host area */

#endif /* SCHED_CONFIG_H */
