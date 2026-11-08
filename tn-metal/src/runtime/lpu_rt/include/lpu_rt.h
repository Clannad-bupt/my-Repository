/*
 * lpu_rt.h — liblpu_rt public API (the only header exposed externally)
 *
 * ThinkNative LPU AI accelerator user-space runtime, a minimal CUDA-style
 * interface. Upper-layer inference frameworks / test programs include only
 * this file.
 *
 * Conventions:
 *  - every entry point returns lpuRtError; details via lpuRtGetLastError (TLS)
 *  - handles are opaque pointers (Module/Function/Stream/Event)
 *  - device memory handle lpuRtDevicePtr = chip DRAM device-side address
 *  - v1 is a single-threaded calling convention (multi-thread mutexes arrive
 *    in a later release)
 *  - backend selection: env var LPU_RT_BACKEND=sim|device, or lpuRtInit flags,
 *    default auto (use device if /dev/lpu0 exists, else sim)
 */
#ifndef LPU_RT_H
#define LPU_RT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Error codes                                                          */
/* ------------------------------------------------------------------ */
typedef enum {
    LPU_RT_SUCCESS = 0,
    LPU_RT_ERROR_NOT_INITIALIZED,
    LPU_RT_ERROR_NO_DEVICE,
    LPU_RT_ERROR_INVALID_DEVICE,
    LPU_RT_ERROR_INVALID_VALUE,
    LPU_RT_ERROR_INVALID_HANDLE,
    LPU_RT_ERROR_OUT_OF_MEMORY,
    LPU_RT_ERROR_TOO_MANY_STREAMS,
    LPU_RT_ERROR_TOO_MANY_EVENTS,
    LPU_RT_ERROR_RING_FULL,
    LPU_RT_ERROR_TIMEOUT,
    LPU_RT_ERROR_DEVICE_ERROR,   /* stream entered the ERR state (CQ ERR entry) */
    LPU_RT_ERROR_MODULE_INVALID, /* operator image corrupt / not a PIE ELF */
    LPU_RT_ERROR_UNSUPPORTED,
    LPU_RT_ERROR_IO,
    LPU_RT_ERROR_NOT_READY,        /* query-style APIs: not done yet */
    LPU_RT_ERROR_SYMBOL_NOT_FOUND, /* operator UND symbol not found in the fw export table */
} lpuRtError;

/* ------------------------------------------------------------------ */
/* Handles and basic types                                              */
/* ------------------------------------------------------------------ */
typedef struct lpu_module *lpuRtModule;
typedef struct lpu_function *lpuRtFunction;
typedef struct lpu_stream *lpuRtStream;
typedef struct lpu_event *lpuRtEvent;
typedef uint64_t lpuRtDevicePtr; /* chip DRAM device-side address */

typedef struct {
    char name[32];
    int numCores;            /* number of LPU cores */
    int numStreams;          /* number of submission streams (hardware rings) */
    int numEvents;           /* number of events (counting semaphores) */
    uint64_t totalDramBytes; /* chip DRAM window size */
    uint32_t identVersion;   /* IDENT register version */
    char backend[16];        /* "sim" | "device" */
} lpuRtDeviceProperties;

/* lpuRtInit flags */
#define LPU_RT_INIT_AUTO 0u /* device if /dev/lpu0 is usable, else sim */
#define LPU_RT_INIT_SIM 1u
#define LPU_RT_INIT_DEVICE 2u

/* ------------------------------------------------------------------ */
/* Init / device                                                        */
/* ------------------------------------------------------------------ */
/* Initialize the runtime and open the device; repeated calls return SUCCESS (idempotent) */
lpuRtError lpuRtInit(uint32_t flags);
/* Shut down the runtime (caller destroys streams/events/modules first; all internal resources
 * freed) */
lpuRtError lpuRtShutdown(void);
/* Block until every stream on the device is drained */
lpuRtError lpuRtDeviceSync(void);
lpuRtError lpuRtGetDeviceCount(int *count);
lpuRtError lpuRtSetDevice(int ordinal);
lpuRtError lpuRtGetDevice(int *ordinal);
lpuRtError lpuRtGetDeviceProperties(lpuRtDeviceProperties *props);

/* ------------------------------------------------------------------ */
/* Memory (allocator over the device DRAM window)                       */
/* ------------------------------------------------------------------ */
lpuRtError lpuRtMalloc(lpuRtDevicePtr *devPtr, size_t size);
lpuRtError lpuRtFree(lpuRtDevicePtr devPtr);
/* pool free/total statistics */
lpuRtError lpuRtMemGetInfo(uint64_t *freeBytes, uint64_t *totalBytes);
/* H2D/D2H copies. stream == NULL executes synchronously right away;
 * with a stream, outstanding work on the stream is drained first (v1
 * conservative semantics; switches to true async once spec §11-2 adds the
 * H2D/D2H COPY descriptor encodings) */
lpuRtError lpuRtMemcpyHtoD(lpuRtDevicePtr dst, const void *src, size_t bytes, lpuRtStream stream);
lpuRtError lpuRtMemcpyDtoH(void *dst, lpuRtDevicePtr src, size_t bytes, lpuRtStream stream);
/* device-memory byte fill / device-internal copy (synchronous: direct BAR1
 * window access; true async versions wait on the COPY descriptor DMA path) */
lpuRtError lpuRtMemset(lpuRtDevicePtr dst, uint8_t value, size_t bytes);
lpuRtError lpuRtMemcpyDtoD(lpuRtDevicePtr dst, lpuRtDevicePtr src, size_t bytes);

/* ------------------------------------------------------------------ */
/* Module / operator (operator image = RISC-V DSO ELF, see docs/rt/lpu-sw-stack.md) */
/* ------------------------------------------------------------------ */
/* Register a firmware ELF (the symbol source when an operator DSO references
 * fw export functions lpu_fw_*). Must be called after lpuRtInit and before
 * the first load of an operator with UND symbols; buffer style matches
 * lpuRtModuleLoad; repeatable (replaces the registry). The export table =
 * the lpu_fw_-prefixed STT_FUNC global symbols of the fw ELF .symtab (fw is
 * a single image with a fixed link address → addresses identical on every core). */
lpuRtError lpuRtSetFirmwareElf(const void *fw_elf_image, size_t size);
/* Parse an operator DSO ELF (host side applies R_RISCV_RELATIVE /
 * R_RISCV_64 (GOT) / R_RISCV_JUMP_SLOT (PLT) — every relocation), and load
 * it into device DRAM */
lpuRtError lpuRtModuleLoad(const void *image, size_t imageSize, lpuRtModule *module);
/* Read the image from a file, then go through lpuRtModuleLoad */
lpuRtError lpuRtModuleLoadFile(const char *path, lpuRtModule *module);
lpuRtError lpuRtModuleUnload(lpuRtModule module);
/* name == NULL returns the image entry (e_entry); otherwise looked up by symbol table (e.g.
 * "vecadd") */
lpuRtError lpuRtModuleGetFunction(lpuRtModule module, const char *name, lpuRtFunction *function);

/* ------------------------------------------------------------------ */
/* Kernel launch (CUDA style; the runtime internally builds the task/cluster/block arrays) */
/* ------------------------------------------------------------------ */
/* Launch on a stream: groupNum clusters × blockNum blocks per cluster, each
 * block a fixed 8-core SPMD gang (core count not exposed upward), all with
 * the same entry and parameters. name == NULL uses the image entry e_entry,
 * otherwise looked up by symbol table. msg lives in host memory with an
 * explicit size (≤ 128 bytes, LPU_MAX_PARAM_BYTES; msgSize == 0 is
 * equivalent to no parameters) and is copied into the launch block before
 * submission — the caller may reuse it immediately after the call returns. */
lpuRtError lpuRtLaunchKernelAsync(lpuRtModule module, const char *name, const void *msg,
                                  size_t msgSize, uint32_t groupNum, uint32_t blockNum,
                                  lpuRtStream stream);

/* ------------------------------------------------------------------ */
/* Streams (the 16 hardware submission rings)                            */
/* ------------------------------------------------------------------ */
lpuRtError lpuRtCreateStream(lpuRtStream *stream);
lpuRtError lpuRtDestroyStream(lpuRtStream stream);
lpuRtError lpuRtStreamSync(lpuRtStream stream);  /* poll CQ until the stream drains */
lpuRtError lpuRtStreamQuery(lpuRtStream stream); /* LPU_RT_ERROR_NOT_READY = busy */
/* cross-stream dependency: insert a wait on event into stream (consumed by the device-side BARRIER)
 */
lpuRtError lpuRtStreamWaitEvent(lpuRtStream stream, lpuRtEvent event);

/* ------------------------------------------------------------------ */
/* Events (64 counting semaphores)                                       */
/* ------------------------------------------------------------------ */
lpuRtError lpuRtCreateEvent(lpuRtEvent *event);
lpuRtError lpuRtDestroyEvent(lpuRtEvent event);
/* record an event on a stream = submit an EVENT(SIGNAL) descriptor (in-stream ordering ⇒ does not
 * fire until prior work completes) */
lpuRtError lpuRtRecordEvent(lpuRtEvent event, lpuRtStream stream);
/* unrecorded event: returns SUCCESS directly (CUDA semantics) */
lpuRtError lpuRtEventSync(lpuRtEvent event);
lpuRtError lpuRtEventQuery(lpuRtEvent event);
/* once both events have latched, the ms delta can be queried (host clock; device TICK_US wiring
 * comes later) */
lpuRtError lpuRtEventElapsedTime(float *ms, lpuRtEvent start, lpuRtEvent end);

/* ------------------------------------------------------------------ */
/* Error query                                                          */
/* ------------------------------------------------------------------ */
lpuRtError lpuRtGetLastError(void);
const char *lpuRtGetErrorString(lpuRtError err);

#ifdef __cplusplus
}
#endif
#endif /* LPU_RT_H */
