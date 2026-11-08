/* rt_ring.c — submission ring management (backend-agnostic)
 *
 * The ring lives in host coherent memory (256B slots × ring_depth); the host
 * writes the 32B descriptor contents into a slot, then writes the doorbell
 * Q_WP[s] = absolute index (WC). Space reclamation relies on the device
 * lazily writing RP back into the RP shadow array (16×4B).
 */
#include "rt_internal.h"

#include <string.h>

/* ring-full wait cap (ns) — matches the StreamSync timeout */
#define RT_RING_WAIT_NS (5ull * 1000 * 1000 * 1000)

void rt_ring_refresh_rp(struct lpu_stream *st) {
    /* rp_shadow already points at this stream's slot (ctx->rp_sh_va + idx*4); read it directly */
    uint32_t rp = *st->rp_shadow;
    /* absolute index is monotonic; the shadow only moves forward (the int32 difference tolerates
     * 2^32 wraparound) */
    if ((int32_t)(rp - st->rp_cached) > 0) {
        rt_log_("stream%d: RP %u -> %u (device consumed)", st->idx, st->rp_cached, rp);
        st->rp_cached = rp;
    }
}

lpuRtError rt_ring_submit(struct lpu_stream *st, const lpu_stream_desc_t *d) {
    lpu_ctx_t *c = st->ctx;
    uint64_t deadline = rt_now_ns() + RT_RING_WAIT_NS;

    /* poll the CQ along the way (advance completions, reclaim ring space) */
    rt_cq_poll(c);

    while (st->wp - st->rp_cached >= c->ring_depth) {
        rt_ring_refresh_rp(st);
        rt_cq_poll(c);
        if (st->errored) {
            rt_set_last_error(LPU_RT_ERROR_DEVICE_ERROR);
            return LPU_RT_ERROR_DEVICE_ERROR;
        }
        if (rt_now_ns() > deadline) {
            rt_set_last_error(LPU_RT_ERROR_RING_FULL);
            return LPU_RT_ERROR_RING_FULL;
        }
    }

    /* write the slot (the first 32B of the 256B slot is the descriptor payload, the rest is
     * reserved for format evolution) */
    size_t slot = (size_t)(st->wp & (c->ring_depth - 1)) * LPU_RING_SLOT;
    rt_dmemcpy(st->ring_va + slot, d, LPU_STREAM_DESC_SIZE);
    st->wp++;
    rt_log_("submit: stream%d type=%u wp=%u p0=%#llx", st->idx, LPU_DW0_TYPE(d->dw0), st->wp,
            (unsigned long long)d->p0);

    /* flush posted writes + doorbell (absolute index) */
    c->be.ops->flush(&c->be);
    c->be.ops->doorbell(&c->be, st->idx, st->wp);
    return LPU_RT_SUCCESS;
}
