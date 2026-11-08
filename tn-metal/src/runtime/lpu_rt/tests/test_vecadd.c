/* test_vecadd.c — E2E data flow verification (sim backend + exec hook doing a real data transform):
 * Malloc → MemcpyHtoD → ModuleLoad(hand-built PIE image) →
 * Launch(by name/e_entry; 1/4 clusters; 2x2 grid dispatch shape) → StreamSync/
 * DeviceSync → MemcpyDtoH → verify c = a + b.
 * Also covers ModuleLoadFile / Memset / DtoD / MemGetInfo and event ordering.
 */
#include "lpu_rt.h"
#include "lpu_rt_sim.h"
#include "test_image.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define N 8u /* 8-core sample: a/b/c are 8 x u32 each, each core computes exactly one element */

/* unified operator input ABI (same as sample_op.c): {data region address, total bytes of the data
 * region}; padded to 128B (the LPU_MAX_PARAM_BYTES limit); Launch explicitly copies msgSize bytes
 */
typedef struct {
    uint64_t addr; /* → [a: N×u32 | b: N×u32 | c: N×u32] */
    uint64_t len;
    uint8_t pad[128 - 16];
} vecadd_param_t;

/* dispatch shape bookkeeping: hook call counts per [cluster][block] (asserted by the 2x2 grid
 * case). 64 = LPU_MAX_CLUSTERS_TASK, 8 = LPU_MAX_BLOCKS_CLUSTER (lpu_desc.h not pulled in) */
static uint32_t g_calls[64][8];

/* exec hook: simulate an LPU core executing the operator (a real data transform, not
 * just protocol checking). Same semantics as the 8-core SPMD in sample_op.c: each core (role)
 * computes only its own one element; data region = single-address contiguous [a|b|c], three
 * equal-length u32 segments */
static void vecadd_hook(uint64_t entry_pc, uint64_t param_ptr, uint64_t cluster_param_ptr,
                        uint32_t cluster_id, uint32_t block_id, uint32_t role, uint32_t n_cores) {
    (void)entry_pc;
    (void)cluster_param_ptr;
    (void)n_cores;
    if (cluster_id < 64 && block_id < 8)
        g_calls[cluster_id][block_id]++;

    void *p;
    if (lpuRtSimPtrToHost(param_ptr, &p) != LPU_RT_SUCCESS)
        return;
    vecadd_param_t *vp = p;
    if (vp->len == 0 || vp->len % (3u * sizeof(uint32_t)) != 0)
        return;
    uint64_t n = vp->len / (3u * sizeof(uint32_t));
    if (n > N)
        n = N; /* the hook side only guards the host array bound */
    uint32_t *seg;
    if (lpuRtSimPtrToHost(vp->addr, (void **)&seg) != LPU_RT_SUCCESS)
        return;
    if (role < n) {
        const uint32_t *a = seg, *b = seg + n;
        seg[2 * n + role] = a[role] + b[role];
    }
}

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

static int verify(const uint32_t *h, const uint32_t *a, const uint32_t *b) {
    for (uint32_t i = 0; i < N; i++)
        if (h[i] != a[i] + b[i])
            return 0;
    return 1;
}

int main(void) {
    CHECK(lpuRtInit(LPU_RT_INIT_SIM) == LPU_RT_SUCCESS, "Init(SIM)");
    CHECK(lpuRtSimSetExecHook(vecadd_hook) == LPU_RT_SUCCESS, "set exec hook");

    printf("== device memory and H2D ==\n");
    /* single-address ABI: a/b/c three contiguous segments, one allocation */
    lpuRtDevicePtr dbuf = 0;
    CHECK(lpuRtMalloc(&dbuf, 3u * N * 4) == LPU_RT_SUCCESS, "Malloc [a|b|c]");
    lpuRtDevicePtr da = dbuf, db = dbuf + N * 4, dc = dbuf + 2u * N * 4;

    static uint32_t ha[N], hb[N], hc[N];
    for (uint32_t i = 0; i < N; i++) {
        ha[i] = i;
        hb[i] = 0x1000u + i * 3u;
    }
    CHECK(lpuRtMemcpyHtoD(da, ha, N * 4, NULL) == LPU_RT_SUCCESS, "HtoD a");
    CHECK(lpuRtMemcpyHtoD(db, hb, N * 4, NULL) == LPU_RT_SUCCESS, "HtoD b");

    printf("== module load ==\n");
    static uint8_t image[2048];
    size_t imgsz = lpu_test_image_build(image, sizeof(image), 0x80, "vecadd_op");
    CHECK(imgsz > 0, "build the hand-made PIE image");
    lpuRtModule mod = NULL;
    CHECK(lpuRtModuleLoad(image, imgsz, &mod) == LPU_RT_SUCCESS, "ModuleLoad(PIE)");
    lpuRtFunction fn = NULL, fentry = NULL;
    CHECK(lpuRtModuleGetFunction(mod, "vecadd_op", &fn) == LPU_RT_SUCCESS, "GetFunction(by name)");
    CHECK(lpuRtModuleGetFunction(mod, NULL, &fentry) == LPU_RT_SUCCESS, "GetFunction(e_entry)");
    CHECK(lpuRtModuleGetFunction(mod, "no_such_fn", &fn) == LPU_RT_ERROR_INVALID_VALUE,
          "unknown symbol lookup fails");
    CHECK(fn != NULL && fentry != NULL, "handles non-NULL");

    printf("== launch (1 cluster) ==\n");
    lpuRtStream st = NULL;
    CHECK(lpuRtCreateStream(&st) == LPU_RT_SUCCESS, "CreateStream");

    vecadd_param_t vp;
    memset(&vp, 0, sizeof(vp));
    vp.addr = dbuf;
    vp.len = 3u * N * 4;

    CHECK(lpuRtLaunchKernelAsync(mod, "vecadd_op", &vp, sizeof(vp), 1, 1, st) == LPU_RT_SUCCESS,
          "Launch(1 cluster)");
    CHECK(lpuRtStreamSync(st) == LPU_RT_SUCCESS, "StreamSync");
    CHECK(lpuRtMemcpyDtoH(hc, dc, N * 4, NULL) == LPU_RT_SUCCESS, "DtoH");
    CHECK(verify(hc, ha, hb), "c = a + b（cc=1）");

    printf("== launch (4 clusters) + event ordering ==\n");
    memset(hc, 0, sizeof(hc));
    lpuRtEvent ev = NULL;
    CHECK(lpuRtCreateEvent(&ev) == LPU_RT_SUCCESS, "CreateEvent");
    CHECK(lpuRtLaunchKernelAsync(mod, "vecadd_op", &vp, sizeof(vp), 4, 1, st) == LPU_RT_SUCCESS,
          "Launch(4 cluster)");
    CHECK(lpuRtRecordEvent(ev, st) == LPU_RT_SUCCESS, "RecordEvent");
    CHECK(lpuRtEventSync(ev) == LPU_RT_SUCCESS, "EventSync");
    CHECK(lpuRtStreamSync(st) == LPU_RT_SUCCESS, "StreamSync(cc=4)");
    CHECK(lpuRtMemcpyDtoH(hc, dc, N * 4, NULL) == LPU_RT_SUCCESS, "DtoH");
    CHECK(verify(hc, ha, hb), "c = a + b（cc=4）");

    printf("== launch (2 group x 2 block grid dispatch shape) ==\n");
    memset(hc, 0, sizeof(hc));
    memset(g_calls, 0, sizeof(g_calls));
    CHECK(lpuRtLaunchKernelAsync(mod, "vecadd_op", &vp, sizeof(vp), 2, 2, st) == LPU_RT_SUCCESS,
          "Launch(2×2)");
    CHECK(lpuRtDeviceSync() == LPU_RT_SUCCESS, "DeviceSync");
    bool shape_ok =
        g_calls[0][0] == 8 && g_calls[0][1] == 8 && g_calls[1][0] == 8 && g_calls[1][1] == 8;
    for (int ci = 0; ci < 64; ci++)
        for (int bi = 0; bi < 8; bi++)
            if (!(ci < 2 && bi < 2) && g_calls[ci][bi])
                shape_ok = false;
    CHECK(shape_ok, "dispatch shape: 2 clusters x 2 blocks x 8 roles");
    CHECK(lpuRtMemcpyDtoH(hc, dc, N * 4, NULL) == LPU_RT_SUCCESS, "DtoH");
    CHECK(verify(hc, ha, hb), "c = a + b（2×2）");

    printf("== launch (e_entry) / argument checks ==\n");
    memset(hc, 0, sizeof(hc));
    CHECK(lpuRtLaunchKernelAsync(mod, NULL, &vp, sizeof(vp), 1, 1, st) == LPU_RT_SUCCESS,
          "Launch(NULL name → e_entry)");
    CHECK(lpuRtStreamSync(st) == LPU_RT_SUCCESS, "StreamSync(e_entry)");
    CHECK(lpuRtMemcpyDtoH(hc, dc, N * 4, NULL) == LPU_RT_SUCCESS, "DtoH");
    CHECK(verify(hc, ha, hb), "c = a + b（e_entry）");
    CHECK(lpuRtLaunchKernelAsync(mod, "vecadd_op", &vp, sizeof(vp), 0, 1, st) ==
              LPU_RT_ERROR_INVALID_VALUE,
          "groupNum=0 rejected");
    CHECK(lpuRtLaunchKernelAsync(mod, "vecadd_op", &vp, sizeof(vp), 1, 9, st) ==
              LPU_RT_ERROR_INVALID_VALUE,
          "blockNum=9 rejected");
    CHECK(lpuRtLaunchKernelAsync(mod, "vecadd_op", &vp, 200, 1, 1, st) ==
              LPU_RT_ERROR_INVALID_VALUE,
          "msgSize>128 rejected");
    CHECK(lpuRtLaunchKernelAsync(mod, "vecadd_op", NULL, 16, 1, 1, st) ==
              LPU_RT_ERROR_INVALID_VALUE,
          "NULL msg with size rejected");
    CHECK(lpuRtLaunchKernelAsync(mod, "no_such_fn", &vp, sizeof(vp), 1, 1, st) ==
              LPU_RT_ERROR_INVALID_VALUE,
          "unknown name rejected");

    printf("== Memset / DtoD / MemGetInfo ==\n");
    CHECK(lpuRtMemset(dc, 0xAB, N * 4) == LPU_RT_SUCCESS, "Memset dc=0xAB");
    uint8_t *dch = NULL;
    CHECK(lpuRtSimPtrToHost(dc, (void **)&dch) == LPU_RT_SUCCESS && dch[0] == 0xAB &&
              dch[N * 4 - 1] == 0xAB,
          "Memset takes effect");
    CHECK(lpuRtMemcpyDtoD(dc, da, N * 4) == LPU_RT_SUCCESS, "DtoD da→dc");
    CHECK(lpuRtSimPtrToHost(dc, (void **)&dch) == LPU_RT_SUCCESS && memcmp(dch, ha, N * 4) == 0,
          "DtoD content = a");
    uint64_t freeb = 0, totalb = 0;
    CHECK(lpuRtMemGetInfo(&freeb, &totalb) == LPU_RT_SUCCESS && totalb > 0 && freeb > 0 &&
              freeb < totalb,
          "MemGetInfo");

    printf("== ModuleLoadFile ==\n");
    FILE *mf = fopen("test_mod_load.tmp", "wb");
    CHECK(mf != NULL && fwrite(image, 1, imgsz, mf) == imgsz && fclose(mf) == 0,
          "write the temporary image file");
    lpuRtModule mod2 = NULL;
    CHECK(lpuRtModuleLoadFile("test_mod_load.tmp", &mod2) == LPU_RT_SUCCESS, "ModuleLoadFile");
    CHECK(lpuRtLaunchKernelAsync(mod2, "vecadd_op", &vp, sizeof(vp), 1, 1, st) == LPU_RT_SUCCESS,
          "Launch(file image)");
    CHECK(lpuRtStreamSync(st) == LPU_RT_SUCCESS, "StreamSync(file image)");
    CHECK(lpuRtMemcpyDtoH(hc, dc, N * 4, NULL) == LPU_RT_SUCCESS, "DtoH");
    CHECK(verify(hc, ha, hb), "c = a + b (file image)");
    CHECK(lpuRtModuleUnload(mod2) == LPU_RT_SUCCESS, "Unload(file image)");
    remove("test_mod_load.tmp");
    CHECK(lpuRtModuleLoadFile("no_such_file.so", &mod2) == LPU_RT_ERROR_IO,
          "LoadFile on a missing file → IO error");

    printf("== cleanup ==\n");
    CHECK(lpuRtDestroyEvent(ev) == LPU_RT_SUCCESS, "DestroyEvent");
    CHECK(lpuRtDestroyStream(st) == LPU_RT_SUCCESS, "DestroyStream");
    CHECK(lpuRtModuleUnload(mod) == LPU_RT_SUCCESS, "ModuleUnload");
    CHECK(lpuRtFree(dbuf) == LPU_RT_SUCCESS, "Free(dbuf)");
    CHECK(lpuRtShutdown() == LPU_RT_SUCCESS, "Shutdown");

    if (g_fail) {
        printf("test_vecadd: %d checks failed\n", g_fail);
        return 1;
    }
    printf("test_vecadd: all passed\n");
    return 0;
}
