/* rt_event.c — events (64 counting semaphores)
 *
 * Completion judgment: the SIGNAL descriptor produces no CQ entry (dw0 does
 * not set CQ_DONE); its "fired" state is judged by the recording stream's RP
 * shadow passing the SIGNAL slot (descriptors are ordered in-stream ⇒ RP
 * past it means the device has evaluated that SIGNAL and the count has been
 * ++'d).
 *
 * Consumption discipline (spec §2.5 single-writer/single-consumer): one
 * SIGNAL is consumed by exactly one party —
 *   host: EventSync observes the fire, then writes EVT_HOST_CLEAR;
 *   device: the BARRIER submitted by StreamWaitEvent consumes it with a --
 *   on EVAL.
 * waiter_stream marks the consumer; the host does not CLEAR an event consumed
 * by the device; the state machine guarantees no double consumption.
 */
#include "rt_internal.h"

#include <stdlib.h>
#include <string.h>

#define RT_EVENT_WAIT_NS (5ull * 1000 * 1000 * 1000)

LPU_RT_API lpuRtError lpuRtCreateEvent(lpuRtEvent *event) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    if (!event)
        return LPU_RT_ERROR_INVALID_VALUE;

    int id = -1;
    for (int i = 0; i < LPU_NUM_EVENTS; i++) {
        if (!c->events[i]) {
            id = i;
            break;
        }
    }
    if (id < 0) {
        rt_set_last_error(LPU_RT_ERROR_TOO_MANY_EVENTS);
        return LPU_RT_ERROR_TOO_MANY_EVENTS;
    }

    struct lpu_event *ev = calloc(1, sizeof(*ev));
    if (!ev)
        return LPU_RT_ERROR_OUT_OF_MEMORY;
    ev->id = id;
    ev->allocated = true;
    ev->waiter_stream = -1;
    c->events[id] = ev;
    *event = ev;
    return LPU_RT_SUCCESS;
}

LPU_RT_API lpuRtError lpuRtDestroyEvent(lpuRtEvent event) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    if (!event)
        return LPU_RT_ERROR_INVALID_HANDLE;
    if (c->events[event->id] != event)
        return LPU_RT_ERROR_INVALID_HANDLE;
    c->events[event->id] = NULL;
    free(event);
    return LPU_RT_SUCCESS;
}

LPU_RT_API lpuRtError lpuRtRecordEvent(lpuRtEvent event, lpuRtStream stream) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    if (!event || !stream)
        return LPU_RT_ERROR_INVALID_HANDLE;

    /* in-stream ordering ⇒ the SIGNAL fires only after prior work completes
     * (produces no CQ entry, not counted in inflight; completion is judged by
     * the recording stream's RP passing it) */
    lpu_stream_desc_t d;
    memset(&d, 0, sizeof(d));
    d.dw0 = LPU_DW0_MAKE_EVT(LPU_EVT_SIGNAL);
    d.a = (uint32_t)event->id; /* event_id reuses the a field (spec §3.2) */

    lpuRtError e = rt_ring_submit(stream, &d);
    if (e != LPU_RT_SUCCESS) {
        rt_set_last_error(e);
        return e;
    }

    event->recorded = true;
    event->latched = false;
    event->host_consumed = false;
    event->waiter_stream = -1;
    event->signal_stream = stream->idx;
    event->signal_wp = stream->wp; /* absolute index including the SIGNAL (recording stream's RP
                                      passing it = fired) */
    return LPU_RT_SUCCESS;
}

/* is the device count > 0 (shadow array first, EVT_STATE register as fallback) */
lpuRtError rt_event_signal_poll(lpu_ctx_t *c, int id, bool *signaled) {
    volatile uint32_t *shadow = (volatile uint32_t *)(c->evt_sh_va + (size_t)id * 4);
    if (*shadow > 0) {
        *signaled = true;
        return LPU_RT_SUCCESS;
    }
    uint32_t state = rt_reg_read(&c->be, LPU_REG_EVT_STATE((uint32_t)id) & ~3u);
    /* EVT_STATE 2B/entry: count in the low 16 bits (read via the 4B-aligned register) */
    uint32_t half = (LPU_REG_EVT_STATE((uint32_t)id) & 2u) ? (state >> 16) : (state & 0xffffu);
    *signaled = half > 0;
    return LPU_RT_SUCCESS;
}

/* has the SIGNAL been evaluated by the device: the recording stream's RP shadow passes the SIGNAL
 * slot */
static bool event_fired(lpu_ctx_t *c, struct lpu_event *ev) {
    struct lpu_stream *st = c->streams[ev->signal_stream];
    if (!st)
        return true; /* recording stream destroyed (DestroyStream drains first ⇒ must have fired) */
    rt_ring_refresh_rp(st);
    return st->rp_cached >= ev->signal_wp;
}

/* observe the SIGNAL and latch per the consumer */
static lpuRtError event_observe(lpu_ctx_t *c, struct lpu_event *ev, bool *observed) {
    /* waiting stream destroyed (error path): fall back to host consumption to avoid leaking the
     * count */
    if (ev->waiter_stream >= 0 && !c->streams[ev->waiter_stream])
        ev->waiter_stream = -1;

    if (ev->latched) {
        *observed = true;
        return LPU_RT_SUCCESS;
    }

    if (event_fired(c, ev)) {
        ev->latched = true;
        ev->done_ns = rt_now_ns();
        if (ev->waiter_stream < 0) {
            /* host consumption (single writer: EVT_HOST_CLEAR) */
            c->be.ops->event_clear(&c->be, ev->id);
            ev->host_consumed = true;
        }
        /* waiter_stream >= 0: the device BARRIER consumes it on EVAL; the host does not CLEAR */
        *observed = true;
        return LPU_RT_SUCCESS;
    }
    *observed = false;
    return LPU_RT_SUCCESS;
}

LPU_RT_API lpuRtError lpuRtEventSync(lpuRtEvent event) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    if (!event)
        return LPU_RT_ERROR_INVALID_HANDLE;
    if (!event->recorded)
        return LPU_RT_SUCCESS; /* unrecorded: no-op (CUDA semantics) */

    uint64_t deadline = rt_now_ns() + RT_EVENT_WAIT_NS;
    for (;;) {
        rt_cq_poll(c);
        bool observed = false;
        lpuRtError e = event_observe(c, event, &observed);
        if (e != LPU_RT_SUCCESS)
            return e;
        if (observed)
            return LPU_RT_SUCCESS;
        if (rt_now_ns() > deadline) {
            rt_set_last_error(LPU_RT_ERROR_TIMEOUT);
            return LPU_RT_ERROR_TIMEOUT;
        }
    }
}

LPU_RT_API lpuRtError lpuRtEventQuery(lpuRtEvent event) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    if (!event)
        return LPU_RT_ERROR_INVALID_HANDLE;
    if (!event->recorded)
        return LPU_RT_SUCCESS;

    rt_cq_poll(c);
    bool observed = false;
    lpuRtError e = event_observe(c, event, &observed);
    if (e != LPU_RT_SUCCESS)
        return e;
    if (!observed) {
        rt_set_last_error(LPU_RT_ERROR_NOT_READY);
        return LPU_RT_ERROR_NOT_READY;
    }
    return LPU_RT_SUCCESS;
}

LPU_RT_API lpuRtError lpuRtEventElapsedTime(float *ms, lpuRtEvent start, lpuRtEvent end) {
    if (!ms || !start || !end)
        return LPU_RT_ERROR_INVALID_VALUE;
    if (!start->latched || !end->latched) {
        rt_set_last_error(LPU_RT_ERROR_NOT_READY);
        return LPU_RT_ERROR_NOT_READY;
    }
    uint64_t diff = end->done_ns > start->done_ns ? end->done_ns - start->done_ns : 0;
    *ms = (float)((double)diff / 1e6);
    return LPU_RT_SUCCESS;
}
