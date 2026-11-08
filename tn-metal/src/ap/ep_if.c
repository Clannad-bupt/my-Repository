/*
 * ep_if.c — EP interface implementation (production placeholders / selftest
 * local closed loop share one translation formula)
 */
#include "fw_debug.h"
#include "mini_libc.h"
#include <stddef.h>
#include <stdint.h>

#include "ep_if.h"
#include "lpu/lpu_regs.h"
#include "sim_c906.h"

#ifdef CONFIG_AP_SELFTEST
/* Locally faked resources (selftest mode): a 0x400B register block (covers
 * every offset in lpu_regs.h), a 16KB "host area", and the core mailbox
 * array — selftest plays host writing the rings/CQ/shadows, inline fake cores
 * fetch dispatches from the mailbox. On the sim machine the placeholder
 * addresses have no backing, so everything lives locally. */
static uint32_t sched_local_regs[LPU_SCHED_REG_BYTES / 4];
static uint8_t sched_local_host[SCHED_LOCAL_HOST_BYTES];
static lpu_core_mb_t sched_local_mb[LPU_NUM_CORES];
#endif

static struct {
    uintptr_t reg_block;   /* scheduler register block (device-side address) */
    uintptr_t host_win;    /* host memory window (device-side address) */
    uint64_t host_pa_base; /* host PA base the window maps */
    uint64_t host_win_size;
} ep;

void sched_ep_init(void) {
#ifdef CONFIG_AP_SELFTEST
    ep.reg_block = (uintptr_t)sched_local_regs;
    ep.host_win = (uintptr_t)sched_local_host;
    ep.host_pa_base = SCHED_LOCAL_HOST_PA_BASE;
    ep.host_win_size = SCHED_LOCAL_HOST_BYTES;
#else
    ep.reg_block = SCHED_REG_BLOCK_BASE;
    ep.host_win = SCHED_HOST_WIN_BASE;
    ep.host_pa_base = SCHED_HOST_PA_BASE;
    ep.host_win_size = SCHED_HOST_WIN_SIZE;
#endif
    DBG("[ep] init: reg_block=%08x host_win=%08x host_pa_base=%08llx size=%08llx\n",
        (uint32_t)ep.reg_block, (uint32_t)ep.host_win, (unsigned long long)ep.host_pa_base,
        (unsigned long long)ep.host_win_size);
}

volatile uint32_t *sched_reg(uint32_t off) { return (volatile uint32_t *)(ep.reg_block + off); }

/* host PA → device address within the window. Out of bounds on the sim is
   just an access to an unimplemented region (cannot happen in selftest; on
   real silicon the EP raises a bus error); no defense here on purpose, to
   stay consistent with hardware behavior. */
static uintptr_t host_dev_addr(uint64_t pa) {
    return ep.host_win + (uintptr_t)(pa - ep.host_pa_base);
}

void sched_ep_host_read(uint64_t pa, void *dst, size_t n) {
    memcpy(dst, (const void *)host_dev_addr(pa), n);
}

void sched_ep_host_write(uint64_t pa, const void *src, size_t n) {
    memcpy((void *)host_dev_addr(pa), src, n);
}

volatile lpu_core_mb_t *sched_mb(uint32_t cid) {
#ifdef CONFIG_AP_SELFTEST
    return (volatile lpu_core_mb_t *)&sched_local_mb[cid];
#else
    return (volatile lpu_core_mb_t *)(uintptr_t)(SCHED_MB_BASE + cid * LPU_CORE_MB_SIZEOF);
#endif
}
