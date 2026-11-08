/* rt_cq.c — completion-queue polling and dispatch (spec golden path: host polls CQ_WP)
 *
 * The CQ lives in host coherent memory; completion entries are {type,
 * id=stream index (ASSUMED), status, timestamp_us}. Polling is on demand (no
 * background thread): every API entry point that might observe progress calls
 * rt_cq_poll.
 */
#include "rt_internal.h"

#define RT_DRAIN_WAIT_NS (5ull * 1000 * 1000 * 1000)

static void stream_complete(lpu_ctx_t *c, uint32_t id) {
    if (id >= LPU_NUM_STREAMS)
        return;
    struct lpu_stream *st = c->streams[id];
    if (!st)
        return;

    if (st->inflight > 0)
        st->inflight--;
}

static void stream_kernel_complete(lpu_ctx_t *c, uint32_t id) {
    if (id >= LPU_NUM_STREAMS)
        return;
    struct lpu_stream *st = c->streams[id];
    if (!st)
        return;

    /* KERNEL_DONE ⇒ pop the oldest launch and free its device DRAM block (in-stream ordering) */
    lpu_launch_t l;
    if (rt_fifo_pop(&st->launches, &l))
        rt_pool_free(&c->pool, l.blk);
    if (st->inflight > 0)
        st->inflight--;
}

static void stream_error(lpu_ctx_t *c, uint32_t id, uint32_t status) {
    if (id >= LPU_NUM_STREAMS)
        return;
    struct lpu_stream *st = c->streams[id];
    if (!st)
        return;
    st->errored = true;
    st->err_status = status;
    /* after an error the stream freezes; later syncs return DEVICE_ERROR */
}

void rt_cq_poll(lpu_ctx_t *c) {
    /* sim backend: advance descriptors blocked on CQ budget / cross-stream barriers (no-op on a
     * real device) */
    c->be.ops->kick(&c->be);

    uint32_t wp = rt_reg_read(&c->be, LPU_REG_CQ_WP);
    bool advanced = false;

    while (c->cq_cp != wp) {
        volatile lpu_cq_entry_t *e =
            (volatile lpu_cq_entry_t *)(c->cq_va +
                                        (size_t)(c->cq_cp & (c->cq_depth - 1)) * LPU_CQ_ENTRY_SIZE);

        rt_log_("cq_poll: entry type=%u id=%u status=%#x cp=%u", e->type, e->id, e->status,
                c->cq_cp);
        switch (e->type) {
        case LPU_CQ_KERNEL_DONE:
            stream_kernel_complete(c, e->id);
            break;
        case LPU_CQ_COPY_DONE:
        case LPU_CQ_CPU_OP_DONE:
            stream_complete(c, e->id);
            break;
        case LPU_CQ_ERR:
            stream_error(c, e->id, e->status);
            break;
        case LPU_CQ_CLUSTER_DONE:
            break; /* the runtime does not request cluster-level CQ */
        default:
            break;
        }
        c->cq_cp++;
        advanced = true;
        wp = rt_reg_read(&c->be, LPU_REG_CQ_WP); /* more completions may land while processing */
    }

    if (advanced)
        rt_reg_write(&c->be, LPU_REG_CQ_CP, c->cq_cp); /* write the consume pointer back */
}

lpuRtError rt_stream_drain(struct lpu_stream *st) {
    lpu_ctx_t *c = st->ctx;
    uint64_t deadline = rt_now_ns() + RT_DRAIN_WAIT_NS;
    rt_log_("drain: stream%d start (inflight=%d wp=%u rp=%u)", st->idx, st->inflight, st->wp,
            st->rp_cached);

    for (;;) {
        /* drained = no in-flight CQ-class work && the device has taken every
         * descriptor (EVENT/BARRIER produce no CQ entry; their completion is
         * judged by RP passing them) */
        rt_ring_refresh_rp(st);
        rt_cq_poll(c);
        if (st->errored) {
            rt_log_("drain: stream%d error status=%#x", st->idx, st->err_status);
            rt_set_last_error(LPU_RT_ERROR_DEVICE_ERROR);
            return LPU_RT_ERROR_DEVICE_ERROR;
        }
        if (st->inflight == 0 && st->rp_cached == st->wp) {
            rt_log_("drain: stream%d drained", st->idx);
            return LPU_RT_SUCCESS;
        }
        if (rt_now_ns() > deadline) {
            rt_log_("drain: stream%d timeout (inflight=%d wp=%u rp=%u)", st->idx, st->inflight,
                    st->wp, st->rp_cached);
            rt_set_last_error(LPU_RT_ERROR_TIMEOUT);
            return LPU_RT_ERROR_TIMEOUT;
        }
    }
}
