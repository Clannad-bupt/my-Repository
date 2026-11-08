/*
 * ap/selftest.c — AP firmware local closed-loop selftest (CONFIG_AP_SELFTEST build, run with -smp
 * 4)
 *
 * No EP / no real LPU cores: selftest plays host (programming registers +
 * writing the ring + ringing doorbells, all through ep_if's faked host area);
 * sched_core_hook injects inline fake cores to run the operator; the engine
 * (ring/dispatch/event/cq) code is identical to the production build.
 *
 * Coverage: KERNEL cascade (task→cluster→block→2 roles) → CQ KERNEL_DONE;
 * EVENT SIGNAL + BARRIER consume closed loop (ev0 count back to zero);
 * cluster/task signal_mask (ev5/ev10); RP shadow and Q_CTX.RP mirror; the
 * vecadd dataflow; the SMP section wakes core1..3 to run real tasks via
 * ap_wake (fire-and-forget + caller-managed completion protocol — a wakeup is
 * a function call; waiting or not is the caller's business).
 */
#include "mini_libc.h"
#include <stdbool.h>
#include <stdint.h>

#include "ap_worker.h"
#include "ep_if.h"
#include "lpu/lpu_core_mb.h"
#include "lpu/lpu_desc.h"
#include "lpu/lpu_regs.h"
#include "sim_c906.h"

/* sched_dispatch.c */
extern void (*sched_core_hook)(volatile lpu_core_mb_t *mb);
/* sched_ring.c */
uint32_t sched_ring_poll(void);
/* sched_event.c */
void sched_event_reset(void);

/* ---- faked host area PA layout (relative to SCHED_LOCAL_HOST_PA_BASE, 16KB total) ---- */
#define HS_RING_OFF 0x0000u    /* submission ring 8 slots × 256B */
#define HS_CQ_OFF 0x0800u      /* CQ 16 × 16B */
#define HS_RP_SH_OFF 0x0900u   /* RP shadow 16 × 4B */
#define HS_EVT_SH_OFF 0x0940u  /* EVT shadow 64 × 4B */
#define HS_TASK_OFF 0x0B00u    /* task descriptors */
#define HS_CLUSTER_OFF 0x0B40u /* cluster descriptors */
#define HP(off) ((uint64_t)SCHED_LOCAL_HOST_PA_BASE + (off))

#define ST_RING_DEPTH 8u
#define ST_CQ_DEPTH 16u
#define ST_N 16u

/* inline fake-core operator: vecadd (param layout matches the runtime test {a,b,c,n}) */
static uint32_t st_op(const void *param) {
    const uint64_t *p = param;
    uint32_t *a = (uint32_t *)(uintptr_t)p[0];
    uint32_t *b = (uint32_t *)(uintptr_t)p[1];
    uint32_t *c = (uint32_t *)(uintptr_t)p[2];
    uint64_t n = p[3];
    if (n > 256)
        return 4;
    for (uint64_t i = 0; i < n; i++)
        c[i] = a[i] + b[i];
    return 0;
}

/* inline fake core: executes on dispatch (a real core runs the fw/scalar
   firmware polling its mailbox) */
static void st_core_hook(volatile lpu_core_mb_t *mb) {
    if (mb->magic != LPU_CORE_MB_MAGIC || mb->entry_pc == 0) {
        mb->status = 2;
    } else {
        __asm__ volatile("fence.i" ::: "memory");
        lpu_op_entry_fn fn = (lpu_op_entry_fn)(uintptr_t)mb->entry_pc;
        mb->status = fn((const void *)(uintptr_t)mb->param_ptr);
    }
    mb->done = 1;
}

static int st_check(const char *what, bool ok) {
    printf("  %-30s %s\n", what, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

/* SMP-section worker task (ap_wake function-call semantics: single pointer
 * argument, no return value). Completion protocol = caller-managed example:
 * result slot (high 32 bits magic|core number + low 32 bits this core's sp as
 * live evidence) → fence → done flag. Running on the right core is guaranteed
 * by the slot index (the slot is the mhartid); sp landing inside this core's
 * stack slot is the live evidence (each core's stack is a distinct range). */
static volatile uint32_t st_done[AP_NUM_CORES];
static volatile uint64_t st_result[AP_NUM_CORES];

static void st_smp_fn(void *priv) {
    uint32_t core = (uint32_t)(uintptr_t)priv;
    uint64_t sp;
    __asm__ volatile("mv %0, sp" : "=r"(sp));
    st_result[core] = (0x534D5000ull | core) << 32 | (sp & 0xffffffffull);
    __asm__ volatile("fence rw, rw" ::: "memory"); /* result visible before done */
    st_done[core] = 1;
}

/* SMP hello-section worker task: read mhartid on its own core and print
 * (exercising the multi-core printf lock + live evidence of running on the
 * right core — both the printed text and the handed-back value come from the
 * mhartid the worker itself read, not something core0 told it; slot number =
 * expected core, handed-back value != slot number means FAIL). */
static volatile uint32_t st_hello_done[AP_NUM_CORES];
static volatile uint32_t st_hello_hart[AP_NUM_CORES];

static void st_smp_hello_fn(void *priv) {
    uint32_t slot = (uint32_t)(uintptr_t)priv; /* slot number = expected core */
    uint32_t hart;
    __asm__ volatile("csrr %0, mhartid" : "=r"(hart));
    printf("  smp hello from core %u\n", hart);
    st_hello_hart[slot] = hart;                    /* hand back the mhartid it read itself */
    __asm__ volatile("fence rw, rw" ::: "memory"); /* print/writeback before done */
    st_hello_done[slot] = 1;
}

void ap_selftest(void) {
    static uint32_t buf_a[ST_N], buf_b[ST_N], buf_c[ST_N];
    static uint64_t param[4];
    int fails = 0;

    printf("sched selftest: local closed loop (fake host + inline cores)\n");
    sched_core_hook = st_core_hook;
    sched_event_reset();

    /* --- host role: program the scheduler registers (host writes these same offsets over BAR0) ---
     */
    memset((void *)sched_reg(0), 0, LPU_SCHED_REG_BYTES);
    *sched_reg(LPU_REG_Q_CTX(0) + LPU_Q_CTX_OFF_BASE_LO) = (uint32_t)HP(HS_RING_OFF);
    *sched_reg(LPU_REG_Q_CTX(0) + LPU_Q_CTX_OFF_BASE_HI) = 0;
    *sched_reg(LPU_REG_Q_CTX(0) + LPU_Q_CTX_OFF_DEPTH) =
        LPU_Q_MAKE(ST_RING_DEPTH, LPU_Q_POLICY_NORMAL);
    *sched_reg(LPU_REG_CQ_BASE_LO) = (uint32_t)HP(HS_CQ_OFF);
    *sched_reg(LPU_REG_CQ_BASE_HI) = 0;
    *sched_reg(LPU_REG_CQ_DEPTH) = ST_CQ_DEPTH;
    *sched_reg(LPU_REG_Q_RP_SHADOW_BASE) = (uint32_t)HP(HS_RP_SH_OFF);
    *sched_reg(LPU_REG_EVT_SHADOW_ADDR) = (uint32_t)HP(HS_EVT_SH_OFF);

    /* --- host role: parameter block + task/cluster/descriptors into the ring --- */
    param[0] = (uintptr_t)buf_a;
    param[1] = (uintptr_t)buf_b;
    param[2] = (uintptr_t)buf_c;
    param[3] = ST_N;
    for (uint32_t i = 0; i < ST_N; i++) {
        buf_a[i] = 3u * i + 1u;
        buf_b[i] = 100u - i;
        buf_c[i] = 0xdeadbeefu;
    }

    lpu_task_desc_t task = {
        .cluster_count = 1,
        .cluster_array_ptr = HP(HS_CLUSTER_OFF),
        .task_signal_mask = 1ull << 10,
    };
    lpu_cluster_desc_t cl = {
        .hdr = LPU_CLUSTER_MAKE(1, 0),
        .signal_mask = 1ull << 5,
        .block[0].n_rsvd = LPU_BLOCK_MAKE(2), /* gang N=2 */
        .block[0].entry_pc = (uintptr_t)st_op,
        /* param_ptr/entry_pc are [chip DRAM addresses] (the operator fetches
         * them directly) — they point at the fw-local parameter block; only the
         * task/cluster descriptors are host-side (fetched over the EP) */
        .block[0].param_ptr = (uintptr_t)param,
    };
    lpu_stream_desc_t d0 = {
        .dw0 = LPU_DW0_MAKE(LPU_DESC_KERNEL),
        .a = 1, /* task_count = 1 */
        .p0 = HP(HS_TASK_OFF),
    };
    lpu_stream_desc_t d1 = {
        .dw0 = LPU_DW0_MAKE_EVT(LPU_EVT_SIGNAL), .a = 0, /* SIGNAL ev0 */
    };
    lpu_stream_desc_t d2 = {
        .dw0 = LPU_DW0_MAKE_BARRIER(false),
        .p0 = 1ull << 0, /* wait on ev0 (satisfied by the SIGNAL earlier in the same stream) */
    };
    sched_ep_host_write(HP(HS_TASK_OFF), &task, sizeof(task));
    sched_ep_host_write(HP(HS_CLUSTER_OFF), &cl, sizeof(cl));
    sched_ep_host_write(HP(HS_RING_OFF) + 0u * LPU_RING_SLOT, &d0, sizeof(d0));
    sched_ep_host_write(HP(HS_RING_OFF) + 1u * LPU_RING_SLOT, &d1, sizeof(d1));
    sched_ep_host_write(HP(HS_RING_OFF) + 2u * LPU_RING_SLOT, &d2, sizeof(d2));

    /* --- ring the doorbell (absolute index 3) → run the engine to quiescence --- */
    *sched_reg(LPU_REG_Q_WP(0)) = 3;
    for (int i = 0; i < 1000; i++)
        sched_ring_poll();

    /* --- verify (read back from the host's viewpoint) --- */
    uint32_t rp_sh;
    sched_ep_host_read(HP(HS_RP_SH_OFF), &rp_sh, sizeof(rp_sh));
    fails += st_check("rp shadow == 3", rp_sh == 3);
    fails += st_check("q_ctx rp mirror == 3", *sched_reg(LPU_REG_Q_CTX(0) + LPU_Q_CTX_OFF_RP) == 3);

    uint32_t cq_wp = *sched_reg(LPU_REG_CQ_WP);
    fails += st_check("cq_wp == 1", cq_wp == 1);
    lpu_cq_entry_t e;
    sched_ep_host_read(HP(HS_CQ_OFF), &e, sizeof(e));
    fails += st_check("cq[0] KERNEL_DONE stream=0",
                      e.type == LPU_CQ_KERNEL_DONE && e.id == 0 && e.status == 0);

    uint32_t ev0, ev5, ev10;
    sched_ep_host_read(HP(HS_EVT_SH_OFF) + 0u * 4u, &ev0, sizeof(ev0));
    sched_ep_host_read(HP(HS_EVT_SH_OFF) + 5u * 4u, &ev5, sizeof(ev5));
    sched_ep_host_read(HP(HS_EVT_SH_OFF) + 10u * 4u, &ev10, sizeof(ev10));
    fails += st_check("ev0 signal->barrier consume (0)", ev0 == 0);
    fails += st_check("ev5 cluster signal (1)", ev5 == 1);
    fails += st_check("ev10 task signal (1)", ev10 == 1);

    bool vec_ok = true;
    for (uint32_t i = 0; i < ST_N; i++)
        vec_ok = vec_ok && (buf_c[i] == buf_a[i] + buf_b[i]);
    fails += st_check("vecadd result", vec_ok);

    volatile lpu_core_mb_t *mb0 = sched_mb(0);
    fails += st_check("mailbox drained (start=done=0)",
                      mb0->start == 0 && mb0->done == 0 && mb0->status == 0);

    /* --- SMP hello: three cores each print their own mhartid (printf lock + right core) ---
     * worker-side printf shows up between core0's output lines (the lock keeps
     * whole lines intact; line order is arbitrary — the real shape of parallel
     * wakeup); the Makefile counts 3 lines matching "smp hello from core". */
    for (uint32_t c = 1; c < AP_NUM_CORES; c++) {
        st_hello_done[c] = 0;
        st_hello_hart[c] = 0;
    }
    for (uint32_t c = 1; c < AP_NUM_CORES; c++)
        if (ap_wake(c, st_smp_hello_fn, (void *)(uintptr_t)c) != 0)
            printf("  ap_wake core%u failed\n", c); /* busy/bad core number (should not happen) */
    bool hello_ok = true;
    for (uint32_t c = 1; c < AP_NUM_CORES; c++) {
        while (st_hello_done[c] == 0)
            ;
        __asm__ volatile("fence rw, rw" ::: "memory"); /* see done before reading hart */
        hello_ok = hello_ok && st_hello_hart[c] == c;  /* handed-back mhartid == slot number */
    }
    fails += st_check("smp hello print (own mhartid)", hello_ok);

    /* --- SMP: wake all workers (returns immediately) → caller-managed
     * busy-wait completion → verify ---
     * ap_wake returns immediately for each core, one core at a time → three
     * tasks in flight in parallel (real SMP shape). Waiting = the caller's own
     * completion protocol (example: busy-wait on the done flag; equally valid:
     * go do other work and re-check ap_idle later). If workers don't run
     * (-smp 1), the busy-wait hangs → needs -smp 4.
     * Before waking, first ap_idle until the previous section's tasks have
     * truly returned (the done flag is set before fn returns; fn clearing
     * itself has a tiny window — not waiting could eat an ap_wake -2). */
    printf("  smp: waking cores 1..%u (needs -smp %u)\n", AP_NUM_CORES - 1, AP_NUM_CORES);
    for (uint32_t c = 1; c < AP_NUM_CORES; c++)
        while (!ap_idle(c))
            ;
    for (uint32_t c = 1; c < AP_NUM_CORES; c++) {
        st_done[c] = 0;
        st_result[c] = 0;
    }
    for (uint32_t c = 1; c < AP_NUM_CORES; c++)
        if (ap_wake(c, st_smp_fn, (void *)(uintptr_t)c) != 0)
            printf("  ap_wake core%u failed\n", c); /* busy/bad core number (should not happen) */

    for (uint32_t c = 1; c < AP_NUM_CORES; c++) {
        bool ok = true;
        while (st_done[c] == 0)
            ; /* caller-managed busy-wait (the "wait" of fire-and-forget is the caller's) */
        __asm__ volatile("fence rw, rw" ::: "memory"); /* see done before reading result */
        ok = (st_result[c] >> 32) == (0x534D5000ull | c);
        /* mini_libc has no sprintf → fixed-width inline label print (aligned with st_check) */
        printf("  smp dispatch core%u           %s (sp=0x%08x)\n", c, ok ? "PASS" : "FAIL",
               (uint32_t)st_result[c]);
        fails += ok ? 0 : 1;
    }

    printf("ap selftest: %s (%d fail)\n", fails ? "FAIL" : "PASS", fails);
}
