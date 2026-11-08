/*
 * lpu_prelink.h — prelinked operator image format (runtime tool rt_prelink → fw smoke run)
 *
 * The host tool rt_prelink uses the production runtime API (sim backend) to
 * load an operator DSO — all relocations (R_RISCV_RELATIVE / R_RISCV_64 /
 * R_RISCV_JUMP_SLOT) are already resolved on the host side into the load
 * window — then exports a "fixed-length header + loaded device-side image"
 * as a single file; the QEMU smoke run loads that file verbatim with
 * -device loader, and fw selftest jumps straight to entry per the header
 * (fw has no ELF loader, unchanged).
 *
 * Image layout (device-side DRAM):
 *   [LPU_OP_BASE-16, LPU_OP_BASE)  header (this file)
 *   [LPU_OP_BASE, +size)            module image (relocations resolved)
 *
 * LPU_OP_BASE is the fixed module base convention shared by the host load
 * side (sim fake DRAM) and the fw side (free fw RAM area); it must avoid the
 * fw .data/.bss/.stack/.heap/per-CPU regions.
 */
#ifndef LPU_PRELINK_H
#define LPU_PRELINK_H

#include <stdint.h>

/* Fixed operator module base (free fw RAM area; the sim backend's fake DRAM
 * and fw use the same base 0x80000000, both sides share this constant) */
#define LPU_OP_BASE 0x80080000u

#define LPU_PRELINK_MAGIC 0x4D50504Cu /* "LPPM" */

typedef struct {
    uint32_t magic;     /* LPU_PRELINK_MAGIC */
    uint32_t base;      /* image load address (= m->base + min_vaddr == LPU_OP_BASE) */
    uint32_t entry_off; /* entry offset relative to image start (e_entry - min_vaddr) */
    uint32_t size;      /* module image bytes (header not included) */
} lpu_prelink_hdr_t;    /* 16B, sits just before the image start */

#endif /* LPU_PRELINK_H */
