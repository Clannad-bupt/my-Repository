/* test_vecadd_dev.c — vecadd E2E (device backend + dual-QEMU rig):
 * VM runtime → lpu.ko → vfio-user → EP model → ap fw (descriptor fetch/dispatch) →
 * mailbox (shared file) → the 8 real cores of scalar fw execute sample_op.so (unified input
 * {addr, len}, each core computes only the one element at its own core_id offset) → CQ completion →
 * host reads back and verifies c = a + b (single-address contiguous region [a|b|c] = 3x8x u32).
 *
 * Prerequisites (full restart sequence; see X86-VM-OPSLOG.md §9 in the QEMU verification tree):
 *   scripts/run_vm.sh up; insmod lpu.ko inside the VM.
 * Host ctest has no /dev/lpu0 → exit(77) (ctest SKIP_RETURN_CODE skip).
 * Artifact paths come from compile-time macros; the LPU_TEST_FW_ELF / LPU_TEST_DSO environment
 * variables override them (runs from any cwd inside the VM; the fw ELF must be the card build — its
 * exported symbol addresses must match where the scalar instance actually has them, see
 * lpu_dram_map.h).
 *
 * fence.i is a no-op in QEMU TCG (no TB flush): load a module at most once per process (this test
 * does exactly that); a second load onto the same page steps on stale translations — rig
 * discipline.
 */
#include "lpu/lpu_dram_map.h"
#include "lpu_rt.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define N 8u /* 8-core sample: a/b/c are 8 x u32 each, each core computes exactly one element */

/* unified operator input ABI (same as sample_op.c): {data region address, total bytes of the data
 * region} */
typedef struct {
    uint64_t addr; /* → [a: N×u32 | b: N×u32 | c: N×u32] */
    uint64_t len;
} vecadd_param_t;

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

/* read the whole file in (small files: the fw ELF / DSO are a few KB each) */
static void *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("  FAIL: cannot open %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(f);
        return NULL;
    }
    void *buf = malloc((size_t)sz);
    if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = (size_t)sz;
    return buf;
}

static int verify(const uint32_t *h, const uint32_t *a, const uint32_t *b) {
    for (uint32_t i = 0; i < N; i++)
        if (h[i] != a[i] + b[i]) {
            printf("  first mismatch: c[%u] = %u, expected %u\n", i, h[i], a[i] + b[i]);
            return 0;
        }
    return 1;
}

int main(void) {
    /* host (no /dev/lpu0) → ctest skip code 77, not a failure */
    if (access("/dev/lpu0", O_RDWR) != 0) {
        printf("test_vecadd_dev: SKIP (no /dev/lpu0, not inside the VM)\n");
        return 77;
    }

    const char *fw_elf = getenv("LPU_TEST_FW_ELF");
    if (!fw_elf || !*fw_elf)
        fw_elf = TEST_VECADD_FW_ELF;
    const char *dso = getenv("LPU_TEST_DSO");
    if (!dso || !*dso)
        dso = TEST_VECADD_DSO;

    printf("== init (device backend) ==\n");
    printf("  fw ELF: %s\n  DSO:    %s\n", fw_elf, dso);
    CHECK(lpuRtInit(LPU_RT_INIT_DEVICE) == LPU_RT_SUCCESS, "Init(DEVICE)");

    lpuRtDeviceProperties props;
    CHECK(lpuRtGetDeviceProperties(&props) == LPU_RT_SUCCESS &&
              strcmp(props.backend, "device") == 0,
          "backend == device");
    printf("  device: %s, DRAM window 0x%llx B\n", props.name,
           (unsigned long long)props.totalDramBytes);

    printf("== fw ELF registration + operator load ==\n");
    size_t fw_sz = 0;
    void *fw = read_file(fw_elf, &fw_sz);
    CHECK(fw != NULL && lpuRtSetFirmwareElf(fw, fw_sz) == LPU_RT_SUCCESS,
          "SetFirmwareElf(card build)");
    free(fw);

    lpuRtModule mod = NULL;
    CHECK(lpuRtModuleLoadFile(dso, &mod) == LPU_RT_SUCCESS, "ModuleLoadFile");
    lpuRtFunction fn = NULL;
    CHECK(lpuRtModuleGetFunction(mod, "op_entry", &fn) == LPU_RT_SUCCESS, "GetFunction(op_entry)");
    CHECK(fn != NULL, "function handle non-NULL");

    printf("== device memory and H2D ==\n");
    /* single-address ABI: a/b/c three contiguous segments, one allocation */
    lpuRtDevicePtr dbuf = 0;
    CHECK(lpuRtMalloc(&dbuf, 3u * N * 4) == LPU_RT_SUCCESS,
          "Malloc [a|b|c] contiguous region (3x8x u32)");
    lpuRtDevicePtr da = dbuf, db = dbuf + N * 4, dc = dbuf + 2u * N * 4;
    /* pool base sanity: the device address must land inside the pool region (>= LPU_DRAM_POOL_BASE)
     */
    CHECK(dbuf >= LPU_DRAM_POOL_BASE, "device address inside the pool region (>= 0x82000000)");
    printf("  base=0x%llx a=+0x%llx b=+0x%llx c=+0x%llx\n", (unsigned long long)dbuf,
           (unsigned long long)N * 4, (unsigned long long)2u * N * 4,
           (unsigned long long)3u * N * 4);

    static uint32_t ha[N], hb[N], hc[N];
    for (uint32_t i = 0; i < N; i++) {
        ha[i] = i;
        hb[i] = 0x1000u + i * 7u;
    }
    memset(hc, 0, sizeof(hc));
    CHECK(lpuRtMemcpyHtoD(da, ha, N * 4, NULL) == LPU_RT_SUCCESS, "HtoD a");
    CHECK(lpuRtMemcpyHtoD(db, hb, N * 4, NULL) == LPU_RT_SUCCESS, "HtoD b");
    CHECK(lpuRtMemset(dc, 0, N * 4) == LPU_RT_SUCCESS, "Memset c=0");

    printf("== Launch(vecadd) → real-core execution ==\n");
    lpuRtStream st = NULL;
    CHECK(lpuRtCreateStream(&st) == LPU_RT_SUCCESS, "CreateStream");

    vecadd_param_t vp = {dbuf, 3u * N * 4};
    CHECK(lpuRtLaunchKernelAsync(mod, "op_entry", &vp, sizeof(vp), 1, 1, st) == LPU_RT_SUCCESS,
          "LaunchKernelAsync(1 block x 8-core SPMD)");
    CHECK(lpuRtStreamSync(st) == LPU_RT_SUCCESS, "StreamSync");
    CHECK(lpuRtMemcpyDtoH(hc, dc, N * 4, NULL) == LPU_RT_SUCCESS, "DtoH");
    CHECK(verify(hc, ha, hb), "c = a + b (per-core core_id slices stitched together)");

    printf("== cleanup ==\n");
    CHECK(lpuRtDestroyStream(st) == LPU_RT_SUCCESS, "DestroyStream");
    CHECK(lpuRtFree(dbuf) == LPU_RT_SUCCESS, "Free(dbuf)");
    CHECK(lpuRtShutdown() == LPU_RT_SUCCESS, "Shutdown");

    if (g_fail) {
        printf("== VECADD E2E FAIL == (%d checks failed)\n", g_fail);
        return 1;
    }
    printf("== VECADD E2E PASS ==\n");
    return 0;
}
