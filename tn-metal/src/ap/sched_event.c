/*
 * sched_event.c — 64 counting semaphores (device-side authoritative counts) +
 * host shadow mirror (spec §5)
 *
 * One SIGNAL is consumed by exactly one side: the device-side BARRIER /
 * cluster EVAL (consume), or the host-side EventSync (EVT_HOST_CLEAR register,
 * polling version v1 — host writes the event_id, fw consumes and zeroes the
 * register; multiple clears within one polling interval get overwritten by
 * the last write; the real RTL is a FIFO; a known v1 limitation, recorded in
 * the docs/rt/lpu-sw-stack.md ASSUMED list).
 */
#include "fw_debug.h"
#include "mini_libc.h"
#include <stdbool.h>
#include <stdint.h>

#include "ep_if.h"
#include "lpu/lpu_desc.h"
#include "lpu/lpu_regs.h"

static uint32_t evt_count[LPU_NUM_EVENTS];

/* shadow array base (host PA; 32-bit register → host bus address <4GB) */
static void evt_mirror(uint32_t id) {
    uint64_t base = (uint64_t)*sched_reg(LPU_REG_EVT_SHADOW_ADDR);
    if (base == 0)
        return;
    uint32_t v = evt_count[id];
    sched_ep_host_write(base + 4u * id, &v, sizeof(v));
}

void sched_event_signal(uint32_t id) {
    if (id >= LPU_NUM_EVENTS)
        return;
    evt_count[id]++;
    DBG("[evt] signal %u -> count %u\n", id, evt_count[id]);
    evt_mirror(id);
}

void sched_event_signal_mask(uint64_t mask) {
    for (uint32_t id = 0; id < LPU_NUM_EVENTS; id++)
        if (mask & (1ull << id))
            sched_event_signal(id);
}

void sched_event_clear(uint32_t id) {
    if (id >= LPU_NUM_EVENTS)
        return;
    DBG("[evt] clear %u (was %u)\n", id, evt_count[id]);
    evt_count[id] = 0;
    evt_mirror(id);
}

bool sched_event_mask_ready(uint64_t mask) {
    for (uint32_t id = 0; id < LPU_NUM_EVENTS; id++)
        if ((mask & (1ull << id)) && evt_count[id] == 0)
            return false;
    return true;
}

/* consume (only call after mask_ready): -- per hit bit, shadow writeback included */
void sched_event_consume(uint64_t mask) {
    for (uint32_t id = 0; id < LPU_NUM_EVENTS; id++) {
        if (mask & (1ull << id)) {
            if (evt_count[id] > 0)
                evt_count[id]--;
            evt_mirror(id);
        }
    }
    DBG("[evt] consume mask=%08llx\n", (unsigned long long)mask);
}

/* host manual event clear (consumed by polling the EVT_HOST_CLEAR register) */
void sched_event_poll_host_clear(void) {
    uint32_t v = *sched_reg(LPU_REG_EVT_HOST_CLEAR);
    if (v == 0)
        return;
    DBG("[evt] host_clear %u (was %u)\n", v, evt_count[v < LPU_NUM_EVENTS ? v : 0]);
    if (v < LPU_NUM_EVENTS)
        sched_event_clear(v);
    *sched_reg(LPU_REG_EVT_HOST_CLEAR) = 0;
}

/* clear all (for STREAM_RESET / selftest) */
void sched_event_reset(void) {
    memset(evt_count, 0, sizeof(evt_count));
    for (uint32_t id = 0; id < LPU_NUM_EVENTS; id++)
        evt_mirror(id);
}
