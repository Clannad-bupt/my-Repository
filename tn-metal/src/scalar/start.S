/*
 * scalar fw startup — single fw image + per-core per-CPU data (TLS local-exec + tp)
 *
 * Layout: .text/.rodata resident; .data load image to run address (card
 * variant LMA==VMA self-copy harmless), .bss cleared; each core's writable
 * state goes in that core's slot (.tdata template image + .tbss + stack),
 * accessed tp-relative.
 *
 * Single fw image: all cores run the same code (fw link address fixed →
 * operator GOT/PLT single value holds); every core walks startup in full —
 * first set gp (ld relaxes loads of symbols near __global_pointer$ into
 * gp-relative; gp=0 runs away), copy this core's TLS template, set
 * sp = this core's slot top / tp = this core's slot base (this toolchain's
 * TPREL offset is the in-block offset, objdump-verified:
 * fw_this_core@block+0 → 0(tp)), install mtvec→park, main(core_id).
 *
 * Shared .data/.bss is copied/cleared by core0 only (release gate, same as
 * the AP fw src/ap/start.S):
 *   core0:   copy .data → clear .bss → fence → write release magic → main
 *   others:  poll the release magic → fence → main (enter only after the shared region is ready)
 * It used to be 8 cores concurrently clearing .bss — a lagging core zeroed
 * core0's uart8250_dev (.bss) mid-print → get_reg computed address 0x5 →
 * load access fault → park silent death (2026-10-30 live: only 1 of 8 cores
 * survived). The fix = release gate (this file) + collapsing output to
 * core0 alone (src/scalar/main.c — other cores call no output function,
 * lock-free). The magic sits in slot 0's reserved area (slot+0x400,
 * .percpu NOLOAD stays out of the image; rig-prep truncates every round,
 * clearing stale values — a temp restart without prep reading a stale
 * magic is also harmless: non-core0 cores never touch shared .data/.bss,
 * the gate only keeps them out of main before the shared region is ready).
 */
    .section .text.init, "ax", @progbits
    .global _start
_start:
    /* Global pointer — must be set first: ld relaxes loads/stores of symbols
     * within __global_pointer$ ±2KB into gp-relative (verified: _bss_end →
     * addi t2,gp,-1920); with gp=0 the .bss clear end turns into
     * 0xfff..f880, the clear loop wipes past the whole DRAM then store
     * faults (matching the bootrom ap2sp startup register setup on the same
     * machine). norelax: the la gp itself must not be relaxed (it would
     * become a no-op mv gp,gp). */
.option push
.option norelax
    la      gp, __global_pointer$
.option pop

    /* This core's number: read hartid (simulator hartid-base=0 → hartid ==
     * scalar id. ★ on real silicon the scalar id goes through IC-specific
     * registers/EFUSE, or hartid has a base offset — when landing, change
     * the read/subtract the base here; the old placeholder li a0,0 was
     * deleted). */
    csrr    a0, mhartid

    /* Out-of-bounds guard: core id beyond the slot array (__percpu_slots=8)
     * → park — when the slot array is smaller than the machine's core
     * count, extra cores halt safely without stomping on other cores' data
     * past their slots. */
    la      t0, __percpu_slots
    bgeu    a0, t0, park

    /* This core's slot base = __percpu_base + core_id × stride */
    la      t0, __percpu_base
    la      t1, __percpu_stride
    mul     t1, a0, t1
    add     t5, t0, t1

    /* ---- shared .data/.bss: core0 only (release gate, see file header; other cores → 5f) ---- */
    bnez    a0, 5f

    /* shared .data: LMA(ROM) → VMA(DRAM) (card variant LMA==VMA, self-copy harmless) */
    la      t0, _data_load_start
    la      t1, _data_start
    la      t2, _data_end
1:
    bgeu    t1, t2, 2f
    lbu     t3, 0(t0)
    sb      t3, 0(t1)
    addi    t0, t0, 1
    addi    t1, t1, 1
    j       1b
2:
    /* clear shared .bss (core0 only → no concurrent-clearing window on the printing core's .bss state) */
    la      t1, _bss_start
    la      t2, _bss_end
3:
    bgeu    t1, t2, 4f
    sb      zero, 0(t1)
    addi    t1, t1, 1
    j       3b
4:
    /* core0: release magic (fence keeps .data/.bss visible before the
       magic; slot 0 reserved area +0x400 — the offset must fit in the
       ld/sd 12-bit immediate ±2047, 0x800 is out of bounds) */
    fence   rw, rw
    la      t0, __percpu_base
    li      t1, 0x5343424F4F543031   /* "SCBOOT01" */
    sd      t1, 0x400(t0)

    /* ---- per-core per-CPU slot init (all cores concurrent, each touches only its own slot) ---- */
5:
    /* copy this core's TLS template: .tdata LMA(ROM) → this core's slot start (tdata byte count is small, byte by byte) */
    la      t0, _tdata_load_start
    mv      t1, t5
    la      t2, _tdata_end
    la      t3, _tdata_start
    sub     t2, t2, t3              /* template byte count */
6:
    beqz    t2, 7f
    lbu     t4, 0(t0)
    sb      t4, 0(t1)
    addi    t0, t0, 1
    addi    t1, t1, 1
    addi    t2, t2, -1
    j       6b
7:
    /* clear this core's .tbss (incl. the .tdata→.tbss alignment gap): in-slot [tdata size, tbss end offset) */
    la      t2, _tbss_end
    la      t3, _tdata_start
    sub     t2, t2, t3              /* tbss end offset relative to slot base */
    add     t2, t5, t2              /* clear end address */
8:
    bgeu    t1, t2, 9f
    sb      zero, 0(t1)
    addi    t1, t1, 1
    j       8b
9:
    fence   rw, rw

    la      t0, __percpu_stride
    add     sp, t5, t0              /* stack top = slot base + stride (stack in the slot's top 16K) */
    mv      tp, t5                  /* tp = this core's slot base = TLS block start */

    /* trap vector → park (modeled on the bootrom ap2sp: mtvec installed after
     * sp is set). Exceptions no longer flood the screen with endless fetch
     * faults at mtvec=0; they pend in wfi (★ full dump framework, see
     * bootrom ap2sp §13, hook up later as needed). mie cleared first
     * (reset value is 0, defensive). */
    csrw    mie, zero
    la      t0, park
    csrw    mtvec, t0

    /* ---- release gate (non-core0): core0's magic = shared .data/.bss ready ---- */
    beqz    a0, 11f
    la      t0, __percpu_base
    li      t1, 0x5343424F4F543031   /* "SCBOOT01" */
10:
    ld      t2, 0x400(t0)
    bne     t2, t1, 10b
    fence   rw, rw                  /* fetch/read-order gate: read the shared region only after seeing the magic */

11:
    call    main                    /* a0 = core_id */
    j       park

    .align 2                    /* mtvec DIRECT mode requires 4-byte alignment */
park:
    wfi
    j       park
