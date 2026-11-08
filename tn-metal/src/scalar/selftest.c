/*
 * selftest.c — core firmware selftest (CONFIG_FW_SELFTEST build)
 *
 * No scheduler core / QEMU card-side machine needed: the parameter block is
 * self-provisioned in local DRAM, directly exercising ① the operator ABI call
 * path (function pointer + parameter pointer); ② fw_core_exec through the
 * lpu_core_mb.h protocol fields (simulating the scheduler filling the
 * message); ③ a real DSO really executing (the rt_prelink-produced
 * sample_op prelinked image, all GOT/PLT relocations already resolved by
 * the host loader — this stage is the fw-side live check of plan b; skipped
 * if the header is absent).
 * On the simulator, banner + this selftest = full-link smoke of fw startup/
 * linking/UART/DRAM.
 */
#include "mini_libc.h"
#include <stdint.h>

#include "lpu/lpu_core_mb.h"
#include "lpu/lpu_fw_api.h"
#include "sim_c906.h"

/* op_exec.c */
uint32_t fw_core_exec(const volatile lpu_core_mb_t *mb);

/* selftest operator: single-core full vecadd reference implementation (sum check).
 * param follows the unified operator input ABI: {data region address, total
 * bytes of the data region}, data region = [a|b|c] three equal-length u32
 * segments (matching the sample_op.c convention) */
static uint32_t selftest_op(const void *param) {
    const uint64_t *p = param;
    uint32_t *seg = (uint32_t *)(uintptr_t)p[0];
    uint64_t len = p[1];

    if (len == 0 || len % (3u * sizeof(uint32_t)) != 0)
        return 4; /* bad length shape (not 3 equal-length u32 segments) */
    uint64_t n = len / (3u * sizeof(uint32_t));
    if (n > 256)
        return 4; /* n out of bounds (selftest-agreed limit) */
    uint32_t *a = seg, *b = seg + n, *c = seg + 2 * n;
    for (uint64_t i = 0; i < n; i++)
        c[i] = a[i] + b[i];
    return 0;
}

void fw_core_selftest(void) {
    static uint32_t buf[3][8]; /* single contiguous region [a|b|c] */
    static uint64_t param[2];
    uint32_t *buf_a = buf[0], *buf_b = buf[1], *buf_c = buf[2];

    /* fw exported API live check (also keeps it alive: unreferenced exported
       symbols would be dropped by --gc-sections) */
    printf("fw api: lpu_fw_core_id=%u\n", lpu_fw_core_id());

    printf("selftest: builtin op direct call\n");
    for (int i = 0; i < 8; i++) {
        buf_a[i] = (uint32_t)(i * 7u);
        buf_b[i] = (uint32_t)(0x100u - i);
        buf_c[i] = 0xdeadbeefu;
    }
    param[0] = (uintptr_t)buf;
    param[1] = sizeof(buf);

    /* ① direct call (operator ABI signature check) */
    uint32_t st = selftest_op((const void *)param);
    int pass = (st == 0);
    for (int i = 0; i < 8; i++)
        pass = pass && (buf_c[i] == buf_a[i] + buf_b[i]);
    printf("selftest[1] direct op call: status=%u %s\n", st, pass ? "PASS" : "FAIL");

    /* ② fw_core_exec through the mailbox protocol fields (simulating the
       scheduler filling the message) */
    volatile lpu_core_mb_t mb;
    memset((void *)&mb, 0, sizeof(mb));
    mb.magic = LPU_CORE_MB_MAGIC;
    mb.version = LPU_CORE_MB_VERSION;
    mb.core_id = 0;
    mb.seq = 1;
    mb.cluster_id = 5;
    mb.block_id = 3;
    mb.role_in_block = 2;
    mb.block_n = 8;
    mb.entry_pc = (uintptr_t)selftest_op;
    mb.param_ptr = (uintptr_t)param;

    for (int i = 0; i < 8; i++)
        buf_c[i] = 0xdeadbeefu;
    st = fw_core_exec(&mb);
    pass = (st == 0);
    for (int i = 0; i < 8; i++)
        pass = pass && (buf_c[i] == buf_a[i] + buf_b[i]);
    printf("selftest[2] dispatch exec path: status=%u %s\n", st, pass ? "PASS" : "FAIL");

    /* ③ real DSO direct call: the rt_prelink-produced image (16B header +
       load window) loaded by QEMU -device loader at FW_OP_BASE-16; skipped
       if the header is absent (keeps the smoke test self-contained).
       The real sample_op has 8-core SPMD semantics: running on a single core
       computes only the one element at this core's core_id offset, the other
       elements keep the sentinel — the assertion itself verifies semantics,
       and lpu_fw_core_id is really called through the PLT (wrong return
       value ⇒ wrong segment computed). */
    const volatile lpu_prelink_hdr_t *ph =
        (const volatile lpu_prelink_hdr_t *)(uintptr_t)(FW_OP_BASE - 16u);
    if (ph->magic != LPU_PRELINK_MAGIC) {
        printf("selftest[3] prelinked dso: skipped (no image at 0x%08x)\n",
               (uint32_t)(FW_OP_BASE - 16u));
    } else if (ph->base != FW_OP_BASE) {
        printf("selftest[3] prelinked dso: FAIL (base 0x%08x != 0x%08x)\n", ph->base,
               (uint32_t)FW_OP_BASE);
    } else if (ph->entry_off >= ph->size) {
        printf("selftest[3] prelinked dso: FAIL (entry_off %u >= size %u)\n", ph->entry_off,
               ph->size);
    } else {
        memset((void *)&mb, 0, sizeof(mb));
        mb.magic = LPU_CORE_MB_MAGIC;
        mb.version = LPU_CORE_MB_VERSION;
        mb.core_id = 0;
        mb.seq = 2;
        mb.block_n = 8;
        mb.entry_pc = (uint64_t)ph->base + ph->entry_off;
        mb.param_ptr = (uintptr_t)param;

        for (int i = 0; i < 8; i++)
            buf_c[i] = 0xdeadbeefu;
        st = fw_core_exec(&mb);
        uint32_t cid = lpu_fw_core_id();
        pass = (st == 0) && (buf_c[cid] == buf_a[cid] + buf_b[cid]);
        for (int i = 0; i < 8; i++)
            if (i != (int)cid)
                pass = pass && (buf_c[i] == 0xdeadbeefu);
        printf("selftest[3] prelinked dso exec(core %u): status=%u %s\n", cid, st,
               pass ? "PASS" : "FAIL");
    }

    printf("selftest complete\n");
}
