/* test_api.c — API smoke test: init/device/memory/streams/events/error paths (sim backend) */
#include "lpu_rt.h"

#include <stdio.h>
#include <string.h>

static int g_fail;

#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (cond) {                                                                                \
            printf("  ok: %s\n", msg);                                                             \
        } else {                                                                                   \
            printf("  FAIL: %s (line %d)\n", msg, __LINE__);                                       \
            g_fail++;                                                                              \
        }                                                                                          \
    } while (0)

int main(void) {
    printf("== uninit path ==\n");
    lpuRtStream s0 = NULL;
    CHECK(lpuRtCreateStream(&s0) == LPU_RT_ERROR_NOT_INITIALIZED,
          "uninitialized CreateStream → NOT_INITIALIZED");
    CHECK(lpuRtDeviceSync() == LPU_RT_ERROR_NOT_INITIALIZED,
          "uninitialized DeviceSync → NOT_INITIALIZED");
    CHECK(lpuRtGetLastError() == LPU_RT_ERROR_NOT_INITIALIZED,
          "GetLastError reflects the most recent error");
    CHECK(lpuRtGetErrorString(LPU_RT_ERROR_RING_FULL) != NULL &&
              lpuRtGetErrorString((lpuRtError)-1) != NULL,
          "GetErrorString non-NULL for any code");

    printf("== init / device ==\n");
    CHECK(lpuRtInit(LPU_RT_INIT_AUTO) == LPU_RT_SUCCESS, "Init(AUTO) → sim");
    CHECK(lpuRtInit(LPU_RT_INIT_SIM) == LPU_RT_SUCCESS, "Init idempotent");

    int nd = -1;
    CHECK(lpuRtGetDeviceCount(&nd) == LPU_RT_SUCCESS && nd == 1,
          "GetDeviceCount = 1 (v1 single device)");
    CHECK(lpuRtSetDevice(0) == LPU_RT_SUCCESS && lpuRtGetDevice(&nd) == LPU_RT_SUCCESS && nd == 0,
          "SetDevice/GetDevice(0)");
    CHECK(lpuRtSetDevice(3) == LPU_RT_ERROR_INVALID_DEVICE, "SetDevice invalid index rejected");

    lpuRtDeviceProperties props;
    CHECK(lpuRtGetDeviceProperties(&props) == LPU_RT_SUCCESS && props.numCores == 8 &&
              props.numStreams == 16 && props.numEvents == 64 && props.totalDramBytes > 0,
          "DeviceProperties count fields");
    CHECK(strcmp(props.backend, "sim") == 0, "backend = sim");

    printf("== memory ==\n");
    lpuRtDevicePtr a = 0, b = 0;
    CHECK(lpuRtMalloc(&a, 4096) == LPU_RT_SUCCESS, "Malloc(4096)");
    CHECK(lpuRtMalloc(&b, 128) == LPU_RT_SUCCESS, "Malloc(128)");
    CHECK(a >= 0x80000000ull && b > a, "device addresses inside the window and monotonic");
    CHECK(lpuRtFree(a) == LPU_RT_SUCCESS, "Free");
    CHECK(lpuRtFree(a) == LPU_RT_ERROR_INVALID_VALUE, "double Free rejected");
    CHECK(lpuRtMalloc(NULL, 16) == LPU_RT_ERROR_INVALID_VALUE, "Malloc(NULL) rejected");

    uint64_t freeb = 0, totalb = 0;
    CHECK(lpuRtMemGetInfo(&freeb, &totalb) == LPU_RT_SUCCESS && totalb > 0 && freeb > 0 &&
              freeb < totalb,
          "MemGetInfo after an allocation: free < total");
    lpuRtDevicePtr m2 = 0;
    CHECK(lpuRtMalloc(&m2, 8192) == LPU_RT_SUCCESS, "Malloc(8192)");
    uint64_t freeb2 = 0;
    CHECK(lpuRtMemGetInfo(&freeb2, &totalb) == LPU_RT_SUCCESS && freeb2 <= freeb - 8192,
          "MemGetInfo free shrinks with allocations");
    CHECK(lpuRtFree(m2) == LPU_RT_SUCCESS, "Free m2");

    printf("== streams ==\n");
    lpuRtStream st = NULL;
    CHECK(lpuRtCreateStream(&st) == LPU_RT_SUCCESS, "CreateStream");
    CHECK(lpuRtStreamSync(st) == LPU_RT_SUCCESS, "empty-stream Sync");
    CHECK(lpuRtStreamQuery(st) == LPU_RT_SUCCESS, "empty-stream Query");
    CHECK(lpuRtDestroyStream(st) == LPU_RT_SUCCESS, "DestroyStream");

    printf("== events ==\n");
    lpuRtStream s1 = NULL, s2 = NULL;
    lpuRtEvent ev = NULL;
    CHECK(lpuRtCreateStream(&s1) == LPU_RT_SUCCESS, "stream 1");
    CHECK(lpuRtCreateStream(&s2) == LPU_RT_SUCCESS, "stream 2");
    CHECK(lpuRtCreateEvent(&ev) == LPU_RT_SUCCESS, "CreateEvent");
    CHECK(lpuRtEventSync(ev) == LPU_RT_SUCCESS, "unrecorded event Sync no-op");
    CHECK(lpuRtRecordEvent(ev, s1) == LPU_RT_SUCCESS, "RecordEvent(s1)");
    CHECK(lpuRtEventQuery(ev) == LPU_RT_SUCCESS, "EventQuery (sim is synchronous)");
    CHECK(lpuRtEventSync(ev) == LPU_RT_SUCCESS, "EventSync consumes");
    CHECK(lpuRtStreamWaitEvent(s2, ev) == LPU_RT_SUCCESS,
          "WaitEvent on an already-completed event no-op");
    CHECK(lpuRtDestroyEvent(ev) == LPU_RT_SUCCESS, "DestroyEvent");

    /* stream drain after RecordEvent: SIGNAL produces no CQ entry; completion
       is judged by RP passing it */
    lpuRtEvent ev2 = NULL;
    CHECK(lpuRtCreateEvent(&ev2) == LPU_RT_SUCCESS, "CreateEvent 2");
    CHECK(lpuRtRecordEvent(ev2, s1) == LPU_RT_SUCCESS, "RecordEvent 2(s1)");
    CHECK(lpuRtStreamSync(s1) == LPU_RT_SUCCESS, "Sync(s1) includes the SIGNAL descriptor");
    CHECK(lpuRtDestroyEvent(ev2) == LPU_RT_SUCCESS, "DestroyEvent 2");

    /* WaitEvent before Record: not recorded → no-op (CUDA semantics) */
    lpuRtEvent ev3 = NULL;
    CHECK(lpuRtCreateEvent(&ev3) == LPU_RT_SUCCESS, "CreateEvent 3");
    CHECK(lpuRtStreamWaitEvent(s2, ev3) == LPU_RT_SUCCESS,
          "WaitEvent on an unrecorded event is a no-op");
    CHECK(lpuRtDestroyEvent(ev3) == LPU_RT_SUCCESS, "DestroyEvent 3");

    printf("== error handles ==\n");
    CHECK(lpuRtLaunchKernelAsync(NULL, "op", NULL, 0, 1, 1, s1) == LPU_RT_ERROR_INVALID_HANDLE,
          "Launch(NULL mod) rejected");
    CHECK(lpuRtDeviceSync() == LPU_RT_SUCCESS, "DeviceSync (incl. streams with recorded events)");
    CHECK(lpuRtDestroyStream(s1) == LPU_RT_SUCCESS, "destroy s1");
    CHECK(lpuRtDestroyStream(s2) == LPU_RT_SUCCESS, "destroy s2");

    printf("== shutdown ==\n");
    CHECK(lpuRtShutdown() == LPU_RT_SUCCESS, "Shutdown");
    CHECK(lpuRtShutdown() == LPU_RT_SUCCESS, "Shutdown is idempotent");

    if (g_fail) {
        printf("test_api: %d checks failed\n", g_fail);
        return 1;
    }
    printf("test_api: all passed\n");
    return 0;
}
