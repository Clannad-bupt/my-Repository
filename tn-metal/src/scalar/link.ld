/*
 * scalar fw layout — single fw image + per-core per-CPU data (TLS local-exec + tp)
 *
 * Single fw image: all cores run the same ROM code (fw link address fixed →
 * operator GOT/PLT single value holds); each core's writable state goes in a
 * per-CPU slot (.tdata template + .tbss + stack), initialized by start.S
 * per core and accessed tp-relative — copy-independent, whichever core
 * executes reads its own slot.
 *
 * start.S-dependent symbols: _data_load_start/_data_start/_data_end (shared
 * .data), _bss_start/_bss_end (shared .bss), _tdata_load_start/
 * _tdata_start/_tdata_end/_tbss_end (TLS template),
 * __percpu_base/__percpu_stride/__percpu_slots (slot array; slots is also
 * the upper bound of the startup out-of-bounds guard).
 *
 * RAM layout (1M): .data/.tdata template VMA/.tbss/.bss → per-CPU slot
 * array 8×32K (ends at ~256K) → free after that; the operator module region
 * LPU_OP_BASE=0x80080000 sits at +512K, avoided. Shared .data must be
 * constant (8 cores startup-concurrently write the same value, accepted;
 * mutable state always __thread — discipline).
 *
 * The AP firmware (src/ap/link.ld) does not use this script — no per-CPU
 * slot array, just a per-core private stack.
 */
OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    ROM (rx)  : ORIGIN = 0x04400000, LENGTH = 64K
    RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 1M
}

/* per-core slot stride: TLS block + reserved + 16K stack (stack at the slot top, sp = slot base + stride) */
__percpu_stride = 32K;

SECTIONS
{
    .text : {
        KEEP(*(.text.init))
        KEEP(*(.text.lpu_fw_core_id))    /* fw export surface: kept alive through --gc-sections */
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

    /* per-core TLS initialized-data template (LMA in ROM, start.S copies it
     * into each core's own slot). At runtime this VMA area is unused (the
     * template is only a link-time placeholder); the in-slot image is the
     * real thing. */
    .tdata : {
        . = ALIGN(8);
        _tdata_start = .;
        *(.tdata .tdata.*)
        . = ALIGN(8);
        _tdata_end = .;
    } > RAM AT > ROM
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
