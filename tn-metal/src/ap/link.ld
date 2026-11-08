/*
 * AP fw layout — 4-core C906, private stacks, no per-CPU data
 *
 * Differences from the scalar fw (fw/scalar/link.ld): AP cores are cache
 * coherent and load no operator DSOs (COPY data movement goes through the
 * DMA engine, equally untouched by operator GOT/PLT; no "fw single image +
 * single-valued operator addresses" constraint) → no per-CPU slots/TLS; all
 * writable state is shared (one .data, one .bss; inter-core communication
 * goes straight through the shared .bss, coherence guaranteed by hardware,
 * order kept with fences under RVWMO), with only one private stack per core
 * (a 4×16K stack array).
 *
 * start.S depends on the symbols: _data_load_start/_data_start/_data_end
 * (shared .data), _bss_start/_bss_end (shared .bss), __ap_boot_release
 * (release gate), __ap_stacks/__ap_stack_size/__ap_stacks_end (stack array +
 * out-of-bounds guard upper bound).
 *
 * AP_NUM_CORES=4 / __ap_stack_size=16K are cross-referenced with ap_worker.h;
 * change the core count in both places.
 */
OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    ROM (rx)  : ORIGIN = 0x04400000, LENGTH = 64K
    RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 1M
}

/* Per-core stack depth: defined at the top level = an absolute symbol (defined
 * inside a section, ld would make it section-relative; start.S `la
 * __ap_stack_size` would fetch a RAM address instead of the 16K value → sp
 * out of bounds parks — already stepped on; __percpu_stride is likewise
 * defined at the top of scalar/link.ld) */
__ap_stack_size = 16K;

SECTIONS
{
    .text : {
        KEEP(*(.text.init))
        *(.text .text.*)
    } > ROM

    .rodata : {
        . = ALIGN(8);
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        . = ALIGN(8);
    } > ROM

    .data : {
        . = ALIGN(8);
        _data_start = .;
        *(.data .data.*)
        *(.sdata .sdata.*)
        . = ALIGN(8);
        _data_end = .;
        __global_pointer$ = _data_start + 0x800;
    } > RAM AT > ROM
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
       other cores poll for it before entering main. Its own tiny section
       (not swept by the .bss zeroing); start.S is its only reader/writer. */
    .bootflag (NOLOAD) : {
        . = ALIGN(8);
        __ap_boot_release = .;
        . += 8;
    } > RAM

    /* per-core private stack array: core n's stack = [__ap_stacks + n*size,
       (n+1)*size), growing down. 4 cores × 16K (AP_NUM_CORES, matching
       ap_worker.h; __ap_stack_size defined at the top level, see above). */
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
