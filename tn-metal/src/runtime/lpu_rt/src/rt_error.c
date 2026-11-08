/* rt_error.c — error strings and TLS last error */
#include "rt_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static __thread lpuRtError g_last_error = LPU_RT_SUCCESS;

/* ------------------------------------------------------------------ */
/* Integration-debug log (since 2026-10-31): stderr, lock-free, the       */
/* on/off switch cached once per process. LPU_RT_LOG=0 disables, default  */
/* on — on by default through the first M5 integration debugging round, */
/* flip the default once in production.                                 */
/* ------------------------------------------------------------------ */
static int g_rt_log = -1;

void rt_log_(const char *fmt, ...) {
    va_list ap;

    if (g_rt_log < 0) {
        const char *s = getenv("LPU_RT_LOG");
        g_rt_log = (s && !strcmp(s, "0")) ? 0 : 1;
    }
    if (!g_rt_log)
        return;

    fputs("[rt] ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void rt_set_last_error(lpuRtError e) { g_last_error = e; }

LPU_RT_API lpuRtError lpuRtGetLastError(void) { return g_last_error; }

LPU_RT_API const char *lpuRtGetErrorString(lpuRtError err) {
    switch (err) {
    case LPU_RT_SUCCESS:
        return "success";
    case LPU_RT_ERROR_NOT_INITIALIZED:
        return "runtime not initialized";
    case LPU_RT_ERROR_NO_DEVICE:
        return "no LPU device";
    case LPU_RT_ERROR_INVALID_DEVICE:
        return "invalid device ordinal";
    case LPU_RT_ERROR_INVALID_VALUE:
        return "invalid value";
    case LPU_RT_ERROR_INVALID_HANDLE:
        return "invalid handle";
    case LPU_RT_ERROR_OUT_OF_MEMORY:
        return "out of memory";
    case LPU_RT_ERROR_TOO_MANY_STREAMS:
        return "too many streams (max 16 hw rings)";
    case LPU_RT_ERROR_TOO_MANY_EVENTS:
        return "too many events (max 64 semaphores)";
    case LPU_RT_ERROR_RING_FULL:
        return "submission ring full";
    case LPU_RT_ERROR_TIMEOUT:
        return "timeout";
    case LPU_RT_ERROR_DEVICE_ERROR:
        return "stream/device error (see CQ error entry)";
    case LPU_RT_ERROR_MODULE_INVALID:
        return "invalid operator image (need PIE ELF, EM_RISCV)";
    case LPU_RT_ERROR_UNSUPPORTED:
        return "unsupported feature";
    case LPU_RT_ERROR_IO:
        return "I/O error";
    case LPU_RT_ERROR_NOT_READY:
        return "not ready";
    case LPU_RT_ERROR_SYMBOL_NOT_FOUND:
        return "operator UND symbol not found in fw export table";
    }
    return "unknown error";
}
