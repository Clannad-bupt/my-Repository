/* rt_stream.c — stream lifecycle (the 16 hardware submission rings) */
#include "rt_internal.h"

#include <stdlib.h>
#include <string.h>

lpuRtError rt_stream_setup(struct lpu_stream *st) {
    lpu_ctx_t *c = st->ctx;
    lpuRtError e;

    /* ring = ring_depth × 256B host coherent */
    e = c->be.ops->host_alloc(&c->be, (size_t)c->ring_depth * LPU_RING_SLOT, 4096,
                              (void **)&st->ring_va, &st->ring_bus);
    if (e != LPU_RT_SUCCESS)
        return e;
    rt_dmemset(st->ring_va, 0, (size_t)c->ring_depth * LPU_RING_SLOT);
    rt_log_("stream%d setup: ring %ux256B va=%p bus=%#llx", st->idx, c->ring_depth, st->ring_va,
            (unsigned long long)st->ring_bus);

    st->wp = 0;
    st->rp_cached = 0;
    st->rp_shadow = (volatile uint32_t *)(c->rp_sh_va + (size_t)st->idx * 4);

    /* Q_CTX[s]: BASE / DEPTH|policy(NORMAL) — a doorbell sent before Q_CTX is
     * configured is ignored by the device (spec §9.2-6); doorbells only go
     * out after this bring-up */
    uint32_t ctx = LPU_REG_Q_CTX((uint32_t)st->idx);
    rt_reg_write(&c->be, ctx + LPU_Q_CTX_OFF_BASE_LO, (uint32_t)(st->ring_bus & 0xffffffffu));
    rt_reg_write(&c->be, ctx + LPU_Q_CTX_OFF_BASE_HI, (uint32_t)(st->ring_bus >> 32));
    rt_reg_write(&c->be, ctx + LPU_Q_CTX_OFF_DEPTH, LPU_Q_MAKE(c->ring_depth, LPU_Q_POLICY_NORMAL));
    c->be.ops->flush(&c->be);
    rt_log_("stream%d setup: Q_CTX reg=%#x base=%#llx depth=%u: done", st->idx, ctx,
            (unsigned long long)st->ring_bus, c->ring_depth);
    return LPU_RT_SUCCESS;
}

LPU_RT_API lpuRtError lpuRtCreateStream(lpuRtStream *stream) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    if (!stream)
        return LPU_RT_ERROR_INVALID_VALUE;

    int idx = -1;
    for (int i = 0; i < LPU_NUM_STREAMS; i++) {
        if (!c->streams[i]) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        rt_set_last_error(LPU_RT_ERROR_TOO_MANY_STREAMS);
        return LPU_RT_ERROR_TOO_MANY_STREAMS;
    }

    struct lpu_stream *st = calloc(1, sizeof(*st));
    if (!st)
        return LPU_RT_ERROR_OUT_OF_MEMORY;
    rt_log_("CreateStream: idx=%d", idx);

    st->idx = idx;
    st->ctx = c;
    lpuRtError e = rt_stream_setup(st);
    if (e != LPU_RT_SUCCESS) {
        free(st);
        rt_set_last_error(e);
        return e;
    }
    c->streams[idx] = st;
    *stream = st;
    return LPU_RT_SUCCESS;
}

LPU_RT_API lpuRtError lpuRtDestroyStream(lpuRtStream stream) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    if (!stream)
        return LPU_RT_ERROR_INVALID_HANDLE;

    struct lpu_stream *st = stream;
    if (c->streams[st->idx] != st)
        return LPU_RT_ERROR_INVALID_HANDLE;

    /* drain, then free */
    rt_log_("DestroyStream: idx=%d (drain first)", st->idx);
    lpuRtError e = rt_stream_drain(st);
    if (e == LPU_RT_SUCCESS && st->ring_va)
        c->be.ops->host_free(&c->be, st->ring_va, st->ring_bus,
                             (size_t)c->ring_depth * LPU_RING_SLOT);
    c->streams[st->idx] = NULL;
    free(st);
    return e;
}

LPU_RT_API lpuRtError lpuRtStreamSync(lpuRtStream stream) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    if (!stream)
        return LPU_RT_ERROR_INVALID_HANDLE;
    return rt_stream_drain(stream);
}

LPU_RT_API lpuRtError lpuRtStreamQuery(lpuRtStream stream) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    if (!stream)
        return LPU_RT_ERROR_INVALID_HANDLE;

    rt_ring_refresh_rp(stream);
    rt_cq_poll(c);
    if (stream->errored) {
        rt_set_last_error(LPU_RT_ERROR_DEVICE_ERROR);
        return LPU_RT_ERROR_DEVICE_ERROR;
    }
    if (stream->inflight > 0 || stream->rp_cached != stream->wp) {
        rt_set_last_error(LPU_RT_ERROR_NOT_READY);
        return LPU_RT_ERROR_NOT_READY;
    }
    return LPU_RT_SUCCESS;
}

LPU_RT_API lpuRtError lpuRtStreamWaitEvent(lpuRtStream stream, lpuRtEvent event) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    if (!stream || !event)
        return LPU_RT_ERROR_INVALID_HANDLE;

    /* CUDA semantics: unrecorded / already-completed event → no-op */
    if (!event->recorded || event->latched)
        return LPU_RT_SUCCESS;

    /* BARRIER descriptor: wait_mask = bit(event). The device consumes that
     * event count on EVAL (-- per hit). Mark the consumer first so a
     * subsequent host EventSync cannot double-consume */
    event->waiter_stream = stream->idx;

    lpu_stream_desc_t d;
    memset(&d, 0, sizeof(d));
    d.dw0 = LPU_DW0_MAKE_BARRIER(false /*peek: consuming wait*/);
    d.p0 = 1ull << event->id;

    lpuRtError e = rt_ring_submit(stream, &d);
    if (e != LPU_RT_SUCCESS) {
        event->waiter_stream = -1;
        rt_set_last_error(e);
        return e;
    }
    return LPU_RT_SUCCESS;
}
