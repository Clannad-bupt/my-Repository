/*
 * ap_worker.c — AP firmware inter-core wakeup (core0 → core1..N-1)
 *
 * The wakeup slot array ap_ctx[] lives in shared .bss (AP cores are cache
 * coherent → no uncached requirements); slot 0 is unused (core0 is the waker).
 * .bss is zeroed by core0 before the release gate (start.S), and workers only
 * run after the gate → slot zero values are guaranteed (fn=NULL = start idle;
 * no zsbl-style worker self-clear-slot step needed).
 *
 * The worker-side polling loop lives in start.S (assembly, not in main) — this
 * file has only the core0 side: ap_wake/ap_idle.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ap_worker.h"
#include "fw_debug.h"

/* RVWMO ordering (real 4-core run on real silicon; do not gamble on TCG strong ordering) */
#define AP_FENCE() __asm__ volatile("fence rw, rw" ::: "memory")

/* wakeup slot array (.bss; volatile: shared across cores, keeps the compiler
 * from caching it in a register). Not static: the start.S worker polling loop
 * references it by symbol. */
volatile ap_ctx_t ap_ctx[AP_NUM_CORES];

/* Wake worker `core` to run fn(priv); returns immediately (function-call
   semantics; whether to wait is managed by the caller) */
int ap_wake(uint32_t core, ap_fn_t fn, void *priv) {
    if (core == 0 || core >= AP_NUM_CORES || fn == NULL)
        return -1;

    volatile ap_ctx_t *c = &ap_ctx[core];
    if (c->fn != NULL) {
        DBG("[wake] core%u busy (prev task not returned)\n", core);
        return -2; /* previous task not returned yet (conservative read; a stale read can only
                      falsely report busy) */
    }

    DBG("[wake] core%u fn=%08llx priv=%08llx\n", core, (unsigned long long)(uintptr_t)fn,
        (unsigned long long)(uintptr_t)priv);
    c->priv = priv;
    AP_FENCE(); /* priv becomes visible before fn */
    c->fn = fn; /* wakeup gate, written last */
    return 0;
}

/* Core idle query (fn==NULL means idle; read-only; a stale read mistaking busy
   for idle is backstopped by ap_wake's -2) */
int ap_idle(uint32_t core) {
    if (core == 0 || core >= AP_NUM_CORES)
        return -1;
    return ap_ctx[core].fn == NULL;
}
