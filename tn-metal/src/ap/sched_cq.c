/*
 * sched_cq.c — write CQ entries back to host DRAM + CQ_WP update (spec §6.8)
 *
 * CQ-full behavior is undecided in the spec (open item): consistent with the
 * sim backend, "drop without writing, do not advance WP" — if the host stops
 * consuming for a long time, completion info is lost; the behavior is recorded
 * in the docs/rt/lpu-sw-stack.md ASSUMED list. CQ_WP is RO from the host's
 * view and writable on the device side (the fw is the device).
 */
#include <stdint.h>

#include "ep_if.h"
#include "fw_debug.h"
#include "lpu/lpu_desc.h"
#include "lpu/lpu_regs.h"

static uint32_t cq_dropped;

void sched_cq_write(uint32_t type, uint32_t id, uint32_t status) {
    uint64_t base =
        (uint64_t)*sched_reg(LPU_REG_CQ_BASE_LO) | ((uint64_t)*sched_reg(LPU_REG_CQ_BASE_HI) << 32);
    uint32_t depth = LPU_Q_DEPTH(*sched_reg(LPU_REG_CQ_DEPTH));
    uint32_t wp = *sched_reg(LPU_REG_CQ_WP);
    uint32_t cp = *sched_reg(LPU_REG_CQ_CP);

    if (base == 0 || depth == 0) {
        DBG("[cq] NOT configured (base=%08llx depth=%u), drop type=%u id=%u status=%u\n",
            (unsigned long long)base, depth, type, id, status);
        return;
    }
    if (wp - cp >= depth) {
        cq_dropped++;
        DBG("[cq] full (wp=%u cp=%u), drop type=%u id=%u status=%u (total %u)\n", wp, cp, type, id,
            status, cq_dropped);
        return;
    }

    DBG("[cq] write type=%u id=%u status=%u @%08llx[%u] (wp=%u->%u)\n", type, id, status,
        (unsigned long long)base, wp % depth, wp, wp + 1);

    lpu_cq_entry_t e = {
        .type = type,
        .id = id, /* completion types = stream index (ASSUMED semantics) */
        .status = status,
        .timestamp_us = *sched_reg(LPU_REG_TICK_US),
    };
    sched_ep_host_write(base + (uint64_t)(wp % depth) * LPU_CQ_ENTRY_SIZE, &e, sizeof(e));

    *sched_reg(LPU_REG_CQ_WP) = wp + 1;
}

/* debug/selftest: CQ full-drop counter */
uint32_t sched_cq_dropped(void) { return cq_dropped; }
