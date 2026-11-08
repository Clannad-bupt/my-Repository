/*
 * core/main.c — LPU core-resident firmware entry (resident on every core, single fw image)
 *
 * Responsibility split (strict division of labor with the scheduler-core fw):
 * this fw [only receives] mailbox messages dispatched by the scheduler, picks
 * up the host-delivered API info per message fields (the param pointer),
 * executes the dynamically loaded operator, and writes back Core_Done.
 * dispatch (ring dequeue/parse/dispatch decision) is the scheduler's job;
 * this fw has none of it.
 *
 * start.S (percpu path) enters via main(core_id): core id = mhartid (real
 * silicon goes through IC-specific registers/EFUSE, see start.S ★); after
 * fw_core_id_init stores it into this core's TLS slot, lpu_fw_core_id() works
 * (single fw image, runtime core id, not compile time).
 *
 * Output discipline (finalized from the 2026-10-30 live lesson): only core0
 * calls the output functions (uart8250_init/printf), the other cores never
 * touch the UART — single output core, lock-free, no character interleaving.
 * It used to be all cores printing concurrently + all cores clearing .bss
 * concurrently (old start.S design): a lagging core zeroed the printing
 * core's uart8250_dev → fault-park silence (only 1 of 8 cores survived); the
 * fix = the start.S release gate (only core0 clears .bss) + this discipline.
 * Cores 1..N-1 report execution via the mailbox done/status fields
 * (host-readable, verified by rig-mb-poke), not via the serial port.
 */
#include "fw_debug.h"
#include "mini_libc.h"
#include <stdint.h>

#include "lpu/lpu_core_mb.h"
#include "lpu/lpu_fw_api.h"
#include "sim_c906.h"
#include "uart8250.h"

/* op_exec.c */
uint32_t fw_core_exec(const volatile lpu_core_mb_t *mb);
/* fw_api.c */
void fw_core_id_init(uint32_t id);
/* selftest.c (CONFIG_FW_SELFTEST build) */
void fw_core_selftest(void);

#ifndef CONFIG_FW_SELFTEST
/* Production main loop: poll the mailbox (polling handshake v0, see
 * lpu_core_mb.h). The slot address uses the runtime core id (single fw image
 * → the same code runs on every core). */
static void dispatch_loop(void) {
    uint32_t id = lpu_fw_core_id();
    volatile lpu_core_mb_t *mb =
        (volatile lpu_core_mb_t *)(uintptr_t)(FW_MB_BASE + id * LPU_CORE_MB_SIZEOF);

    if (id == 0) /* output discipline: only core0 prints */
        printf("core 0: mailbox @ 0x%08x, polling for dispatch...\n",
               (uint32_t)(FW_MB_BASE + id * LPU_CORE_MB_SIZEOF));

    for (;;) {
        if (mb->start != 1u)
            continue;

        uint32_t st;
        if (mb->magic != LPU_CORE_MB_MAGIC) {
            st = 2; /* protocol mismatch → complete with error */
            if (id == 0)
                DBG("[core0] seq=%u magic=%08x != %08x -> status 2\n", mb->seq, mb->magic,
                    LPU_CORE_MB_MAGIC);
        } else if (mb->core_id != id) {
            st = 3; /* target-core mismatch (scheduler wrote the wrong slot) */
            if (id == 0)
                DBG("[core0] seq=%u core_id=%u != %u -> status 3\n", mb->seq, mb->core_id, id);
        } else {
            st = fw_core_exec(mb); /* operator execution (Core_Done semantics) */
        }

        mb->status = st;
        mb->done = 1;
        if (id == 0) /* other cores report via the done/status fields */
            printf("core 0: seq=%u cluster=%u block=%u role=%u -> status %u\n", mb->seq,
                   mb->cluster_id, mb->block_id, mb->role_in_block, st);

        while (mb->start == 1u)
            ; /* wait for the scheduler to clear start (v0 polling) */
    }
}
#endif

int main(uint32_t core_id) {
    fw_core_id_init(core_id); /* this core's TLS slot: lpu_fw_core_id ready */

    /* Output discipline (see file header): only core0 touches the UART. The
       release gate (start.S) guarantees the shared .bss is fully cleared and
       nobody clears it again by the time core0 prints. */
    if (core_id == 0) {
        uart8250_init(FW_UART0_BASE, 0, FW_UART_BAUD, FW_UART_REG_SHIFT, FW_UART_REG_WIDTH);

        printf("\n");
        printf("=== ThinkNative LPU core fw v0.1 ===\n");
        printf("core 0 sole printer; op ABI: uint32_t op_entry(const void *param)\n");
    }

#ifdef CONFIG_FW_SELFTEST
    fw_core_selftest();
#else
    dispatch_loop();
#endif

    printf("core fw: main returned\n");
    return 0;
}
