/*
 * sched_ring.c — doorbell polling + submission ring fetch + RP writeback (spec §3.1/§4 device side)
 *
 * Per-stream state machine: read the Q_WP[s] doorbell (host writes the
 * absolute index) → fetch the 32B content from the host ring per Q_CTX (256B
 * slots; fetch into an aligned local buffer before reading fields) → dispatch
 * by type → advance the local RP → lazily write back (host RP shadow +
 * Q_CTX.RP register mirror; the host relies on the shadow to judge a stream
 * drained). A not-yet-ready BARRIER stalls in front of the descriptor
 * (stream-head blocking preserves ordering); the other streams keep going.
 */
#include <stdbool.h>
#include <stdint.h>

#include "ep_if.h"
#include "fw_debug.h"
#include "lpu/lpu_desc.h"
#include "lpu/lpu_regs.h"

/* sched_dispatch.c */
void sched_dispatch_kernel(uint32_t stream, const lpu_stream_desc_t *d);
/* sched_event.c */
bool sched_event_mask_ready(uint64_t mask);
void sched_event_consume(uint64_t mask);
void sched_event_signal(uint32_t id);
void sched_event_clear(uint32_t id);
void sched_event_poll_host_clear(void);
/* sched_cq.c */
void sched_cq_write(uint32_t type, uint32_t id, uint32_t status);

static uint32_t rp[LPU_NUM_STREAMS]; /* device-side authoritative RP (absolute index) */

static uint32_t ring_fetch(uint32_t s, uint32_t idx, lpu_stream_desc_t *d) {
    uint32_t ctx = LPU_REG_Q_CTX(s);
    uint64_t base = (uint64_t)*sched_reg(ctx + LPU_Q_CTX_OFF_BASE_LO) |
                    ((uint64_t)*sched_reg(ctx + LPU_Q_CTX_OFF_BASE_HI) << 32);
    uint32_t depth = LPU_Q_DEPTH(*sched_reg(ctx + LPU_Q_CTX_OFF_DEPTH));
    if (base == 0 || depth == 0) {
        DBG("[ring] s=%u NOT configured (base=%08llx depth=%u), skip fetch\n", s,
            (unsigned long long)base, depth);
        return 0; /* stream not configured (doorbell suspect) — do not advance blindly */
    }
    sched_ep_host_read(base + (uint64_t)(idx % depth) * LPU_RING_SLOT, d, LPU_STREAM_DESC_SIZE);
    return 1;
}

static void rp_publish(uint32_t s) {
    uint64_t sh = (uint64_t)*sched_reg(LPU_REG_Q_RP_SHADOW_BASE);
    DBG("[ring] rp_publish s=%u rp=%u shadow_base=%08llx\n", s, rp[s], (unsigned long long)sh);
    if (sh != 0) {
        uint32_t v = rp[s];
        sched_ep_host_write(sh + 4u * s, &v, sizeof(v));
    }
    *sched_reg(LPU_REG_Q_CTX(s) + LPU_Q_CTX_OFF_RP) = rp[s];
}

/* single descriptor handling. Returns 1=consumed, 0=blocked (BARRIER not ready, do not skip past)
 */
static int desc_handle(uint32_t s, const lpu_stream_desc_t *d) {
    switch (LPU_DW0_TYPE(d->dw0)) {
    case LPU_DESC_KERNEL:
        DBG("[ring] s=%u KERNEL desc: task=%u p0=%08llx\n", s, LPU_KERNEL_TASK_COUNT(d),
            (unsigned long long)d->p0);
        sched_dispatch_kernel(s, d);
        return 1;
    case LPU_DESC_EVENT:
        DBG("[ring] s=%u EVENT desc: id=%u %s\n", s, LPU_DESC_EVENT_ID(d),
            LPU_DW0_EVT_ACTION(d->dw0) == LPU_EVT_SIGNAL ? "signal" : "clear");
        if (LPU_DW0_EVT_ACTION(d->dw0) == LPU_EVT_SIGNAL)
            sched_event_signal(LPU_DESC_EVENT_ID(d));
        else
            sched_event_clear(LPU_DESC_EVENT_ID(d));
        return 1;
    case LPU_DESC_BARRIER:
        if (!sched_event_mask_ready(d->p0)) {
            DBG("[ring] s=%u BARRIER not ready mask=%08llx, stream head blocked\n", s,
                (unsigned long long)d->p0);
            return 0;
        }
        DBG("[ring] s=%u BARRIER mask=%08llx ready, consume\n", s, (unsigned long long)d->p0);
        sched_event_consume(d->p0);
        return 1;
    case LPU_DESC_COPY:
    case LPU_DESC_CPU_OP:
    default:
        /* not implemented in v1: COPY needs the DMA path (§11-2 pending
         * addition), CPU_OP needs the CPU channel. Consumed as an error and
         * written to CQ ERR; does not block the stream. */
        DBG("[ring] s=%u type=%u unimplemented -> CQ ERR\n", s, LPU_DW0_TYPE(d->dw0));
        sched_cq_write(LPU_CQ_ERR, s, 2);
        return 1;
    }
}

/* one full round of polling all streams; returns the number of descriptors
   consumed this round (selftest judges quiescence by progress) */
uint32_t sched_ring_poll(void) {
    uint32_t consumed = 0;

    sched_event_poll_host_clear();

    for (uint32_t s = 0; s < LPU_NUM_STREAMS; s++) {
        uint32_t qwp = *sched_reg(LPU_REG_Q_WP(s));
        if (qwp == rp[s])
            continue; /* nothing new submitted */
        if (qwp < rp[s])
            continue; /* host rollback unsupported (STREAM_RESET unimplemented in v1) */
        DBG("[ring] doorbell s=%u: wp=%u (rp=%u) fetching desc\n", s, qwp, rp[s]);

        uint32_t before = rp[s];
        while (rp[s] != qwp) {
            lpu_stream_desc_t d;
            if (!ring_fetch(s, rp[s], &d))
                break;
            if (!desc_handle(s, &d))
                break; /* BARRIER blocked: ordered stall at the stream head */
            rp[s]++;
            consumed++;
        }
        if (rp[s] != before)
            rp_publish(s);
    }
    return consumed;
}
