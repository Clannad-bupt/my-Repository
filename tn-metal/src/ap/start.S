/*
 * AP fw startup — 4-core C906, private stacks, no per-CPU variables
 *
 * Differences from the scalar fw (fw/scalar/start.S): AP cores are cache
 * coherent and do not carry the "fw single image + single-valued operator
 * GOT/PLT addresses" constraint (the AP does not load operator DSOs; COPY
 * data movement goes through DMA engine configuration, not the operator load
 * path) → no per-CPU slots/TLS/tp needed; all writable state is shared
 * (.data/.bss), with only one private stack per core.
 *
 * Startup flow (no interrupts, polling v0):
 *   all cores: gp → mhartid → sp = this core's stack top (stack array 4×16K,
 *             out-of-bounds cores park) → clear mie + mtvec→park
 *   core0: copy .data (LMA in ROM) → zero .bss (incl. the wakeup slot array
 *         ap_ctx) → fence → write the release magic → main(0) — the only core
 *         that enters main
 *   other cores: poll the release magic → fence → enter the wakeup polling
 *         loop (never enter main; wakeup model = zsbl vector.S
 *         secondary_core_poll, protocol in ap_worker.h)
 * Only core0 touches .data/.bss → shared state has no concurrent-init problem
 * (the scalar fw's "shared .data must be constants" discipline does not apply
 * here; after the release gate the shared .bss is the inter-core channel,
 * coherence is hardware-guaranteed, order with fences under RVWMO).
 */
    .section .text.init, "ax", @progbits
    .global _start
_start:
    /* Global pointer — must be set first (gp-relative addressing relaxation
     * must be disabled before gp is set; the lesson of running off with gp=0
     * is documented in the fw/scalar/start.S comments) */
.option push
.option norelax
    la      gp, __global_pointer$
.option pop

    /* this core's number (★ on real silicon, if AP core hartids have a base
     * offset, subtract it here) */
    csrr    a0, mhartid

    /* this core's stack: sp = __ap_stacks + (core_id+1) × __ap_stack_size
     * (stacks grow down, stack top = slot end; the array is 4×16K, see link.ld) */
    la      t0, __ap_stack_size
    la      t1, __ap_stacks
    addi    t2, a0, 1
    mul     t2, t2, t0
    add     sp, t1, t2
    /* out-of-bounds guard: core number beyond the stack array → park (with
     * -smp larger than the fw core count, surplus cores stop safely) */
    la      t3, __ap_stacks_end
    bgtu    sp, t3, park

    csrw    mie, zero            /* reset value is already 0, defensive */
    la      t0, park
    csrw    mtvec, t0            /* exceptions land in park's wfi, no spew at mtvec=0 */

    bnez    a0, secondary

    /* ---- core0: global init ---- */
    /* .data: LMA (ROM load image) → VMA (DRAM link address).
       8B main loop + byte-at-a-time tail (section size need not be a multiple of 8). */
    la      t0, _data_load_start
    la      t1, _data_start
    la      t2, _data_end
1:
    bgeu    t1, t2, 2f
    ld      t3, 0(t0)
    sd      t3, 0(t1)
    addi    t0, t0, 8
    addi    t1, t1, 8
    j       1b
2:
    addi    t2, t2, 7
    andi    t2, t2, -8      /* round _data_end down to an 8B boundary */
3:
    bgeu    t1, t2, 4f
    lbu     t3, 0(t0)
    sb      t3, 0(t1)
    addi    t0, t0, 1
    addi    t1, t1, 1
    j       3b
4:
    /* zero .bss (same 8B main loop + byte tail) */
    la      t1, _bss_start
    la      t2, _bss_end
5:
    bgeu    t1, t2, 6f
    sd      zero, 0(t1)
    addi    t1, t1, 8
    j       5b
6:
    addi    t2, t2, 7
    andi    t2, t2, -8
7:
    bgeu    t1, t2, 8f
    sb      zero, 0(t1)
    addi    t1, t1, 1
    j       7b
8:
    /* release gate: global init done → fence first (all of .data/.bss ordered
       before the magic), then write the magic. __ap_boot_release sits in its
       own .bootflag section (not swept by the .bss zeroing above; if power-on
       DRAM residue happens to equal the magic, treat it as negligible — QEMU
       zeroes DRAM; if real silicon doesn't guarantee that, switch to a
       bidirectional handshake). */
    fence   rw, rw
    la      t0, __ap_boot_release
    li      t1, 0x4150424F4F543031   /* "APBOOT01" */
    sd      t1, 0(t0)
    call    main                    /* a0 = 0 */
    j       park

    /* ---- other cores: wait for the release, enter wakeup polling ---- */
secondary:
    la      t0, __ap_boot_release
    li      t2, 0x4150424F4F543031
1:
    ld      t1, 0(t0)
    bne     t1, t2, 1b
    fence   rw, rw                  /* fetch gate: only touch .data/.bss after seeing the magic */

    /* ---- worker wakeup polling (the zsbl vector.S secondary_core_poll model) ----
     * Slot = ap_ctx + core_id × 16B (fn@0, priv@8). Seeing fn change is one
     * "ordinary function call, running on another core": priv → a0 → jalr.
     * The task runs on this core's 16K stack (from the top — the polling loop
     * uses no stack; the slot is reused after the task returns).
     * s0 holds the slot base (callee-saved, so tasks can't lose it). */
    la      s0, ap_ctx
    slli    t1, a0, 4
    add     s0, s0, t1
worker_poll:
    ld      t1, 0(s0)               /* fn: NULL=idle */
    beqz    t1, worker_poll
    fence   rw, rw                   /* see fn before reading priv */
    ld      a0, 8(s0)                /* priv → a0 (single-argument function call) */
    jalr    t1                       /* fn(priv), return address in ra */
    fence   rw, rw                   /* fn's shared writes visible before fn=0 */
    sd      zero, 0(s0)
    j       worker_poll

    .align 2                    /* mtvec DIRECT mode requires 4-byte alignment */
park:
    wfi
    j       park
