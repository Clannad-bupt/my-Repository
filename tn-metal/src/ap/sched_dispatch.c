/*
 * sched_dispatch.c — KERNEL parsing and core dispatch (mailbox) → reaping → cascaded completion
 *
 * v1 execution model (the fw is a temporary stand-in for the scheduler; the
 * real scheduler RTL will pipeline it):
 *   task by task → cluster by cluster, in order. A cluster first validates
 *   all block entries, then dispatches all roles (core allocation = busy
 *   mask + free-spinning reap; gang N ≤ 8 = total core count, a single
 *   cluster can always be dispatched in full) → block and reap until the
 *   cluster completes → fire the cluster signal_mask → next cluster; task
 *   complete → fire the task_signal_mask; all tasks complete → CQ
 *   KERNEL_DONE (status = first non-zero operator return value).
 *   Every error path returns with "no unreaped cores", leaving no busy
 *   cores behind.
 *
 * cluster EVAL (wait_mask) semantics: it must be ready on arrival, otherwise
 * a bounded spin then CQ ERR (cross-stream dependency deadlock is backstopped
 * by the host timeout; v1 has no watchdog).
 *
 * inline fake cores: when sched_core_hook is non-NULL, "the core" runs
 * immediately after dispatch (selftest injection); production has it NULL —
 * real LPU cores run the fw/scalar firmware polling their mailbox.
 */
#include <stdbool.h>
#include <stdint.h>

#include "ep_if.h"
#include "fw_debug.h"
#include "lpu/lpu_core_mb.h"
#include "lpu/lpu_desc.h"
#include "lpu/lpu_regs.h"

/* inline fake-core hook (selftest injection; NULL in production) */
void (*sched_core_hook)(volatile lpu_core_mb_t *mb);

/* sched_event.c */
bool sched_event_mask_ready(uint64_t mask);
void sched_event_consume(uint64_t mask);
void sched_event_signal_mask(uint64_t mask);
/* sched_cq.c */
void sched_cq_write(uint32_t type, uint32_t id, uint32_t status);

static uint32_t core_busy;    /* bit per cid → occupied */
static uint32_t cluster_left; /* unreaped role count for the current cluster */
static uint32_t disp_seq;     /* dispatch sequence number (monotonically increasing) */
static uint32_t kern_status;  /* first non-zero operator return value */

/* cluster EVAL spin bound (timeout → CQ ERR status=5, guards against a host
   dependency that never becomes satisfied) */
#define EVAL_SPIN_MAX 20000000u

static void core_reap(void) {
    for (uint32_t cid = 0; cid < LPU_NUM_CORES; cid++) {
        if (!(core_busy & (1u << cid)))
            continue;
        volatile lpu_core_mb_t *mb = sched_mb(cid);
        if (!mb->done)
            continue;
        if (mb->status != 0 && kern_status == 0)
            kern_status = mb->status;
        DBG("[disp] reap core%u seq=%u c=%u b=%u r=%u status=%u\n", cid, mb->seq, mb->cluster_id,
            mb->block_id, mb->role_in_block, mb->status);
        mb->done = 0;
        mb->start = 0; /* v0 polling handshake close-out */
        core_busy &= ~(1u << cid);
        cluster_left--;
    }
}

static uint32_t core_acquire(void) {
    for (;;) {
        for (uint32_t cid = 0; cid < LPU_NUM_CORES; cid++) {
            if (!(core_busy & (1u << cid))) {
                core_busy |= 1u << cid;
                return cid;
            }
        }
        core_reap(); /* all busy → wait for the current cluster's cores to finish */
    }
}

static void core_dispatch(uint32_t cid, uint32_t cluster_id, uint32_t block_id, uint32_t role,
                          uint32_t n, const lpu_block_desc_t *blk, uint64_t cluster_param) {
    volatile lpu_core_mb_t *mb = sched_mb(cid);
    DBG("[disp] dispatch core%u seq=%u cluster=%u block=%u role=%u/%u "
        "entry=%08llx param=%08llx\n",
        cid, disp_seq + 1, cluster_id, block_id, role, n, (unsigned long long)blk->entry_pc,
        (unsigned long long)blk->param_ptr);
    mb->magic = LPU_CORE_MB_MAGIC;
    mb->version = LPU_CORE_MB_VERSION;
    mb->core_id = cid;
    mb->seq = ++disp_seq;
    mb->cluster_id = cluster_id;
    mb->block_id = block_id;
    mb->role_in_block = role;
    mb->block_n = n;
    mb->entry_pc = blk->entry_pc;
    mb->param_ptr = blk->param_ptr;
    mb->cluster_param_ptr = cluster_param;
    mb->status = 0;
    mb->done = 0;
    __asm__ volatile("fence rw,rw" ::: "memory");
    mb->start = 1; /* trigger (a real core runs upon polling 1) */
    __asm__ volatile("fence rw,rw" ::: "memory");

    if (sched_core_hook)
        sched_core_hook(mb); /* inline fake core: execute + write back done/status */
    cluster_left++;
}

void sched_dispatch_kernel(uint32_t stream, const lpu_stream_desc_t *d) {
    uint16_t ntasks = LPU_KERNEL_TASK_COUNT(d);
    kern_status = 0;

    DBG("[disp] KERNEL s=%u: task=%u task_array=%08llx\n", stream, ntasks,
        (unsigned long long)d->p0);

    if (ntasks == 0 || d->p0 == 0) {
        DBG("[disp] invalid desc (ntask=%u p0=%08llx) -> CQ ERR 3\n", ntasks,
            (unsigned long long)d->p0);
        sched_cq_write(LPU_CQ_ERR, stream, 3); /* invalid descriptor */
        return;
    }

    for (uint16_t t = 0; t < ntasks; t++) {
        lpu_task_desc_t task;
        sched_ep_host_read(d->p0 + (uint64_t)t * LPU_TASK_DESC_SIZE, &task, sizeof(task));
        if (task.cluster_count == 0 || task.cluster_count > LPU_MAX_CLUSTERS_TASK) {
            DBG("[disp] task%u cluster_count=%u invalid -> CQ ERR 3\n", t, task.cluster_count);
            sched_cq_write(LPU_CQ_ERR, stream, 3);
            return;
        }
        DBG("[disp] task%u: cluster=%u cluster_array=%08llx\n", t, task.cluster_count,
            (unsigned long long)task.cluster_array_ptr);

        for (uint16_t ci = 0; ci < task.cluster_count; ci++) {
            lpu_cluster_desc_t cl;
            sched_ep_host_read(task.cluster_array_ptr + (uint64_t)ci * LPU_CLUSTER_DESC_SIZE, &cl,
                               sizeof(cl));
            uint32_t nblocks = LPU_CLUSTER_BLOCKCNT(cl.hdr);
            if (nblocks == 0 || nblocks > LPU_MAX_BLOCKS_CLUSTER) {
                DBG("[disp] cluster%u nblocks=%u invalid -> CQ ERR 3\n", ci, nblocks);
                sched_cq_write(LPU_CQ_ERR, stream, 3);
                return;
            }
            /* validate every block's N first (before dispatch — error paths
               leave no busy cores) */
            for (uint32_t bi = 0; bi < nblocks; bi++) {
                uint32_t n = LPU_BLOCK_N(cl.block[bi].n_rsvd);
                if (n == 0 || n > LPU_NUM_CORES) {
                    DBG("[disp] cluster%u block%u N=%u invalid -> CQ ERR 3\n", ci, bi, n);
                    sched_cq_write(LPU_CQ_ERR, stream, 3);
                    return;
                }
            }
            DBG("[disp] cluster%u: %u block, wait=%08llx signal=%08llx\n", ci, nblocks,
                (unsigned long long)cl.wait_mask, (unsigned long long)cl.signal_mask);

            /* EVAL: only pass when the whole wait_mask is ready (bounded spin) */
            uint32_t spin = 0;
            while (!sched_event_mask_ready(cl.wait_mask)) {
                if (++spin >= EVAL_SPIN_MAX) {
                    DBG("[disp] cluster%u EVAL timeout mask=%08llx -> CQ ERR 5\n", ci,
                        (unsigned long long)cl.wait_mask);
                    sched_cq_write(LPU_CQ_ERR, stream, 5); /* timeout */
                    return;
                }
            }
            if (cl.wait_mask)
                DBG("[disp] cluster%u EVAL mask=%08llx ready, consume\n", ci,
                    (unsigned long long)cl.wait_mask);
            sched_event_consume(cl.wait_mask);

            /* dispatch all blocks × roles → reap until this cluster completes */
            cluster_left = 0;
            for (uint32_t bi = 0; bi < nblocks; bi++) {
                uint32_t n = LPU_BLOCK_N(cl.block[bi].n_rsvd);
                for (uint32_t r = 0; r < n; r++)
                    core_dispatch(core_acquire(), ci, bi, r, n, &cl.block[bi],
                                  cl.cluster_param_ptr);
            }
            while (cluster_left)
                core_reap();

            DBG("[disp] cluster%u done (kern_status=%u)\n", ci, kern_status);
            sched_event_signal_mask(cl.signal_mask); /* cluster complete */
        }
        sched_event_signal_mask(task.task_signal_mask); /* task complete */
        DBG("[disp] task%u done\n", t);
    }

    DBG("[disp] KERNEL s=%u done status=%u -> CQ KERNEL_DONE\n", stream, kern_status);
    sched_cq_write(LPU_CQ_KERNEL_DONE, stream, kern_status);
}
