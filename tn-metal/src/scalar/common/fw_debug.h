/*
 * fw_debug.h — integration-phase event-level DBG logging (shared by the ap/scalar fws)
 *
 * First integration debugging 2026-10-31: one line per major-step/branch
 * event (printf writes straight to the UART, char by char). Discipline
 * unchanged: nothing inside polling hot loops (one line per turn would drown
 * the UART + drag the scheduler down); log only at event boundaries
 * (doorbell/descriptor fetched/dispatch/reap/CQ/event). On by default; for
 * production/perf testing make NODEBUG=1 → the macros expand to nothing, no
 * code needs deleting.
 *
 * Concurrent printing: the ap fw's mini_libc has a line-level spinlock
 * (workers printing concurrently keep whole lines); the scalar fw follows the
 * single-output-core discipline — DBG only on the core0 path (gated by
 * lpu_fw_core_id()==0), other cores never touch the UART (2026-10-30 lesson,
 * see the scalar/main.c header).
 *
 * Usage: DBG("[ring] doorbell s=%u wp=%u\n", s, wp); — the component prefix
 * makes grepping easy. Log strings are ASCII only (no Chinese/arrows:
 * straight out the UART to avoid mojibake and ease grep).
 */
#ifndef FW_DEBUG_H
#define FW_DEBUG_H

#include "mini_libc.h"

#ifdef TN_FW_NODEBUG
#define DBG(...)                                                                                   \
    do {                                                                                           \
    } while (0)
#else
#define DBG(...) printf(__VA_ARGS__)
#endif

#endif /* FW_DEBUG_H */
