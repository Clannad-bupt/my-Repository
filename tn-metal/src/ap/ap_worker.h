/*
 * ap_worker.h — AP firmware inter-core wakeup slots (core0 → core1..N-1, fw-internal)
 *
 * Wakeup = an ordinary function call, just executed on another core (the zsbl
 * wake_up_other_core model, cf. ~/code/zsbl arch/riscv/src/smp.c + vector.S
 * secondary_core_poll):
 *   core0 (ap_wake)  : write priv → fence → write fn (non-NULL = wakeup gate,
 *                       written last) → return immediately. Whether to wait for
 *                       completion, and how, is managed by upper-layer code
 *                       (fn has no return value; results travel back through
 *                       shared-memory protocols the caller defines).
 *   worker (start.S) : raw-poll this core's slot fn != NULL → fence → priv →
 *                       a0 → jalr fn (runs on this core's stack; the stack is
 *                       a compile-time global array __ap_stacks — unlike zsbl,
 *                       the waker does not hand over a stack) → after return,
 *                       fence → clear fn → back to polling (fn==NULL = idle).
 *
 * RVWMO discipline (real 4-core run on real silicon; do not gamble on QEMU
 * TCG strong ordering): priv visible before fn (ap_wake-side fence), see fn
 * before reading priv (worker-side fence), fn's shared writes visible before
 * fn=0 (fence after the worker returns). Ordering for caller-managed
 * protocols is likewise the caller's responsibility.
 *
 * Busy = fn != NULL (ap_wake returns -2, ap_idle returns 0). A stale read can
 * only mistake busy for idle (the next ap_wake is blocked by -2); no task is
 * ever lost. The slot is indexed by mhartid; no magic validation (the worker
 * trusts the waker; v0 discipline: only core0 wakes; concurrent wakers racing
 * on the same core are unprotected).
 *
 * fw-internal protocols stay out of include/ (cross-component ABIs hold only
 * host/firmware-visible contracts).
 */
#ifndef FW_AP_WORKER_H
#define FW_AP_WORKER_H

#include <stdint.h>

#include "tndef.h"

/* AP core count: core0 = scheduler, core1..N-1 = workers.
 * ★ Kept in sync with the link.ld .stacks array (. += 4 × 16K) in two
 * places — change the core count in both. */
#define AP_NUM_CORES 4u

/* task entry: function-call semantics (single pointer argument, no return
   value; fw's own code, not the operator ABI) */
typedef void (*ap_fn_t)(void *priv);

/* per-core wakeup slot (shared .bss array ap_ctx[AP_NUM_CORES]; slot 0 unused —
   core0 is the waker. start.S indexes with a 16B slot stride; field offsets
   fn=0/priv=8) */
typedef struct {
    volatile ap_fn_t fn; /* wakeup gate: NULL=idle, non-NULL=task in flight (written last) */
    volatile void *priv; /* fn's only argument */
} ap_ctx_t;

TN_STATIC_ASSERT(sizeof(ap_ctx_t) == 16, "AP wake slot is 16B");

/* Wake one worker core to run fn(priv) (fire-and-forget, returns immediately).
 * Returns 0 = woken, -1 = invalid core number, -2 = busy (previous task not
 * returned yet). */
int ap_wake(uint32_t core, ap_fn_t fn, void *priv);

/* Core idle query: 1 = idle (fn==NULL), 0 = busy, -1 = invalid core number.
 * Read-only; the caller decides how to wait (spin, or go do other work and
 * re-check later, paired with a caller-managed completion protocol). */
int ap_idle(uint32_t core);

#endif /* FW_AP_WORKER_H */
