/*
 * scalar fw layout — card variant (DRAM-resident, M5 2026-10-30)
 *
 * Difference from link.ld (th-c906 sim, split ROM/RAM): no ROM region, all
 * sections DRAM @ LPU_DRAM_SCALAR_FW_BASE (0x80400000,
 * include/lpu/lpu_dram_map.h); .data/.tdata carry no `AT> ROM` (LMA==VMA →
 * the start.S copy becomes a self-copy; the .tdata template sits directly
 * in the image VMA, start.S's per-core copy into the slot path unchanged).
 * Load method: QEMU tn-card machine
 * `-M ...,fw-load-addr=0x80400000 -bios scalar_fw.bin`; the machine loads
 * the bin into DRAM and riscv_setup_rom_reset_vec's standard jump ROM jumps
 * here. The rest of the layout (TLS local-exec + per-CPU slot array 8×32K)
 * is fully identical to link.ld. Operator modules do not land in the fw
 * region — in card form the runtime loads them into the pool
 * (LPU_DRAM_POOL_BASE).
 */
OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x80400000, LENGTH = 4M
}

/* per-core slot stride: TLS block + reserved + 16K stack (stack at the slot top, sp = slot base + stride) */
__percpu_stride = 32K;

SECTIONS
{
    .text : {
        KEEP(*(.text.init))
        KEEP(*(.text.lpu_fw_core_id))    /* fw export surface: kept alive through --gc-sections */
        *(.text .text.*)
    } > RAM

    .rodata : {
        . = ALIGN(8);
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        . = ALIGN(8);
    } > RAM

    .data : {
        . = ALIGN(8);
        _data_start = .;
        *(.data .data.*)
        *(.sdata .sdata.*)
        . = ALIGN(8);
        _data_end = .;
        __global_pointer$ = _data_start + 0x800;
    } > RAM
    _data_load_start = LOADADDR(.data);

    /* per-core TLS initialized-data template (LMA==VMA, start.S copies it
     * into each core's own slot). At runtime this VMA area is unused (the
     * template is only a link-time placeholder); the in-slot image is the
     * real thing. */
    .tdata : {
        . = ALIGN(8);
        _tdata_start = .;
        *(.tdata .tdata.*)
        . = ALIGN(8);
        _tdata_end = .;
    } > RAM
    _tdata_load_start = LOADADDR(.tdata);

    .tbss (NOLOAD) : {
        . = ALIGN(8);
        _tbss_start = .;
        *(.tbss .tbss.*)
        . = ALIGN(8);
        _tbss_end = .;
    } > RAM

    .bss (NOLOAD) : {
        . = ALIGN(8);
        _bss_start = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(8);
        _bss_end = .;
    } > RAM

    /* per-core slot: [TLS block (.tdata template image + .tbss zeroed) |
     * reserved | 16K stack at top], slot base 16K-aligned. tp = this core's
     * TLS block start = slot base (this toolchain's local-exec TPREL offset
     * is the in-block offset — objdump-verified, not the psABI +0x800
     * convention). Reserved-area convention: slot 0 + 0x400 = startup
     * release magic (start.S, "SCBOOT01"; written by core0 after copying
     * .data/clearing .bss, other cores poll it before entering main; the
     * offset must fit in the ld/sd 12-bit immediate ±2047). */
    .percpu (NOLOAD) : {
        . = ALIGN(16K);
        __percpu_base = .;
        __percpu_slots = 8;             /* 8 cores (LPU_NUM_CORES) */
        . += __percpu_slots * __percpu_stride;
        __percpu_end = .;
    } > RAM

    /DISCARD/ : {
        *(.eh_frame .eh_frame_hdr)
        *(.comment)
        *(.note .note.*)
    }
}
