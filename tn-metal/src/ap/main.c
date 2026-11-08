/*
 * ap/main.c — AP firmware entry (4-core C906: core0 scheduler + core1..N-1 workers)
 *
 * core0's job (early scheduler stand-in): poll the 16 stream doorbells →
 * fetch the host-submitted descriptors (API info) through the EP → parse
 * task/cluster/block → dispatch a mailbox to the intended LPU core →
 * CQ/shadow writeback. The whole thing is later replaced by the QEMU scheduler
 * model; the interface contract (ring/doorbell/CQ/mailbox = lpu_regs.h +
 * lpu_desc.h + lpu_core_mb.h) stays unchanged.
 *
 * main only runs on core0: secondary cores never enter main — after the
 * release gate in start.S they go straight into the wakeup polling loop
 * (ap_worker.h protocol). Selftest mode validates the multi-core path by
 * waking cores via core0 ap_wake (the SMP section of selftest.c).
 */
#include "mini_libc.h"
#include <stdint.h>

#include "ap_worker.h"
#include "ep_if.h"
#include "sim_c906.h"
#include "uart8250.h"

/* sched_ring.c */
uint32_t sched_ring_poll(void);
/* selftest.c (CONFIG_AP_SELFTEST build) */
void ap_selftest(void);

int main(uint32_t core_id) {
    /* Defense: workers must not reach main (see the header comment); if one
       truly does (protocol broken), halt quietly */
    if (core_id != 0)
        for (;;)
            ;

    uart8250_init(FW_UART0_BASE, 0, FW_UART_BAUD, FW_UART_REG_SHIFT, FW_UART_REG_WIDTH);

    printf("\n");
    printf("=== ThinkNative LPU AP fw v0.1 ===\n");
    printf("ap fw: %u cores (core0 scheduler + %u workers, wake @ shared bss)\n", AP_NUM_CORES,
           AP_NUM_CORES - 1);

    sched_ep_init();

#ifdef CONFIG_AP_SELFTEST
    ap_selftest();
#else
    printf("polling doorbells: reg block @ 0x%08x, host win @ 0x%08x, "
           "mb @ 0x%08x\n",
           SCHED_REG_BLOCK_BASE, SCHED_HOST_WIN_BASE, SCHED_MB_BASE);
    for (;;)
        sched_ring_poll(); /* doorbell-polling main loop (no interrupts, pure polling v0) */
#endif

    printf("ap fw: main returned\n");
    return 0;
}
