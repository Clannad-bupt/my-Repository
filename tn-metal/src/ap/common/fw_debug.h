/*
 * fw_debug.h — event-level DBG logging for integration debugging (shared by
 * the ap/scalar fw images)
 *
 * 2026-10-31 first integration run: one line per major step / branch event
 * (printf writes straight to the UART, character by character). Discipline
 * unchanged: nothing inside polling hot loops (one line per turn would drown
 * the UART and drag the scheduler to a crawl); log only at event boundaries
 * (doorbell / descriptor fetched / dispatch / reap / CQ / event). On by
 * default; for production / performance runs make NODEBUG=1 → the macro
 * expands to nothing, no code to delete.
 *
 * Concurrent printing: the ap fw's mini_libc has a line-level spinlock
 * (worker concurrent printing keeps whole lines intact); the scalar fw follows
 * the single-output-core discipline — DBG only on core0 paths (gated by
 * lpu_fw_core_id()==0), other cores never touch the UART (2026-10-30 lesson,
 * see the scalar/main.c file header).
 *
 * Usage: DBG("[ring] doorbell s=%u wp=%u\n", s, wp); — the component prefix
 * makes grepping easy. Log strings are ASCII only (no Chinese/arrows: the
 * UART writes them raw — avoids mojibake and stays greppable).
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
