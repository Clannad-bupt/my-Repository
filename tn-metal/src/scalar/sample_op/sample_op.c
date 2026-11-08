/*
 * sample_op.c — operator ABI sample (8-core SPMD vecadd, -shared DSO form)
 *
 * Operator image contract (docs/rt/lpu-sw-stack.md §4.6–§4.8):
 *   - freestanding DSO ELF (Xuantie Linux toolchain -fPIC -shared; the
 *     bare-metal toolchain does not support -shared → the standard path uses
 *     the Linux cross toolchain); libc calls (DT_NEEDED → rejected) and
 *     global writable state are forbidden; fw references go through UND
 *     function symbols only (R_RISCV_64 GOT / R_RISCV_JUMP_SLOT PLT,
 *     resolved by the host at load time against the fw export table and
 *     written into the card DRAM); the fw has no ELF loader, it jumps
 *     straight to entry_pc.
 *   - entry uint32_t op_entry(const void *param), returns 0 = success.
 *   - unified operator input ABI (all later operators the same): param
 *     points to {uint64_t addr; uint64_t len} — data region address + total
 *     bytes of the data region; metadata is not embedded in the data region;
 *     the data region's internal layout is up to each operator.
 *   - this sample's data region: [a: n×u32 | b: n×u32 | c: n×u32] (n = len/12).
 *     8-core SPMD, each core computes only the one element at its own
 *     core_id offset: fetch coreid first → load a/b at the coreid offset →
 *     add and write c (n ≤ 8; with n < 8, cores with cid ≥ n idle out and
 *     return). The sim-side vecadd_hook has the same semantics.
 */
#include "lpu/lpu_fw_api.h"
#include "lpu/lpu_regs.h"
#include <stdint.h>

uint32_t op_entry(const void *param) {
    uint32_t cid = lpu_fw_core_id(); /* fetch this core's number first (a real PLT call) */
    if (cid >= LPU_NUM_CORES)
        return 5;

    const uint64_t *p = param; /* unified input {addr, len} */
    uint32_t *seg = (uint32_t *)(uintptr_t)p[0];
    uint64_t len = p[1];

    if (len == 0 || len % (3u * sizeof(uint32_t)) != 0)
        return 4; /* not the shape of 3 equal-length u32 segments */
    uint64_t n = len / (3u * sizeof(uint32_t));
    if (n > LPU_NUM_CORES)
        return 4; /* one element per core in this sample → n ≤ 8 */
    if (cid >= n)
        return 0; /* with n < 8, high cores idle out */

    const uint32_t *a = seg; /* load a/b at the coreid offset */
    const uint32_t *b = seg + n;
    uint32_t av = a[cid], bv = b[cid];

    uint32_t *c = seg + 2 * n; /* compute c and write it back */
    c[cid] = av + bv;
    return 0;
}
