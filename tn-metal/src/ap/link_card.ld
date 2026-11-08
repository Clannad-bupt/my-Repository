/*
 * AP fw layout — card variant (DRAM-resident, M5 2026-10-30)
 *
 * Differences from link.ld (th-c906 sim, split ROM/RAM): no ROM region; all
 * sections sit in DRAM @ LPU_DRAM_AP_FW_BASE (0x80000000, include/lpu/lpu_dram_map.h);
 * .data has no `AT> ROM` (LMA==VMA → start.S's copy loop becomes a self-copy,
 * zero changes to start.S).
 * Loading: QEMU tn-card machine `-M ...,fw-load-addr=0x80000000 -bios ap_fw.bin`;
 * the machine loads the bin into DRAM, and riscv_setup_rom_reset_vec's standard
 * jump ROM jumps over.
 * The rest of the layout (shared .data/.bss + release gate + 4×16K stack
 * array) is identical to link.ld.
 */
OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 4M
}

/* Per-core stack depth: defined at the top level = an absolute symbol (defined
 * inside a section, ld would make it section-relative; start.S `la
 * __ap_stack_size` would fetch a RAM address instead of the 16K value → sp
 * out of bounds parks — already stepped on; same as link.ld) */
__ap_stack_size = 16K;

SECTIONS
{
    .text : {
        KEEP(*(.text.init))
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

    .bss (NOLOAD) : {
        . = ALIGN(8);
        _bss_start = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(8);
        _bss_end = .;
    } > RAM

    /* startup release gate: core0 writes the magic once global init is done;
       other cores poll for it before entering main. */
    .bootflag (NOLOAD) : {
        . = ALIGN(8);
        __ap_boot_release = .;
        . += 8;
    } > RAM

    /* per-core private stack array: core n's stack = [__ap_stacks + n*size,
       (n+1)*size), growing down. 4 cores × 16K (AP_NUM_CORES, matching
       ap_worker.h). */
    .stacks (NOLOAD) : {
        . = ALIGN(16);
        __ap_stacks = .;
        . += 4 * __ap_stack_size;
        __ap_stacks_end = .;
    } > RAM

    /DISCARD/ : {
        *(.eh_frame .eh_frame_hdr)
        *(.comment)
        *(.note .note.*)
    }
}
