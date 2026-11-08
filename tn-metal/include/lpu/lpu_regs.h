/*
 * lpu_regs.h — LPU BAR0 window region map + scheduler register block offsets
 *
 * Contract source: LPU-SCHED-SPEC.md v0.4 §8 in the QEMU verification tree (offsets are draft).
 * All components in this repo (runtime / driver / fw / the future QEMU device
 * model) share this file; register offsets must not be scattered anywhere else.
 *
 * Model: BAR0 maps a window into the chip-internal address space (consistent
 * with the existing chip implementation). Within the window, regions carve up
 * SRAM / MSI interrupt registers / chip system registers / the scheduler
 * register block, etc. Register offsets in this file are offsets *inside* the
 * scheduler register block, folded into BAR0 window addresses through
 * LPU_SCHED_REG_BASE. Region bases are all placeholders (★), to be filled in
 * once the chip address space is finalized.
 */
#ifndef LPU_REGS_H
#define LPU_REGS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* BAR0 window region map (★ all placeholders, pending chip address
 * space finalization)                                                 */
/* ------------------------------------------------------------------ */
#define LPU_WIN_SRAM_OFF 0x00000000u   /* ★ SRAM region */
#define LPU_WIN_SRAM_SIZE 0x00000000u  /* ★ */
#define LPU_WIN_MSI_OFF 0x00000000u    /* ★ MSI interrupt register region */
#define LPU_WIN_MSI_SIZE 0x00000000u   /* ★ */
#define LPU_WIN_SYS_OFF 0x00000000u    /* ★ chip system register region */
#define LPU_WIN_SYS_SIZE 0x00000000u   /* ★ */
#define LPU_SCHED_REG_BASE 0x00000000u /* ★ scheduler register block base within the window */

/* register-block-internal offset → BAR0 window offset */
#define LPU_SCHED_REG(off) ((uint32_t)(LPU_SCHED_REG_BASE) + (uint32_t)(off))

/* ------------------------------------------------------------------ */
/* Scheduler register block (block-internal offsets, spec §8, draft)   */
/* ------------------------------------------------------------------ */
#define LPU_REG_IDENT 0x000u       /* RO  magic + version */
#define LPU_REG_CTRL 0x008u        /* RW  {enable, core_enable[7:0]} */
#define LPU_REG_STATUS 0x010u      /* RO  {state, err_code, err_stream} */
#define LPU_REG_INTR_STATUS 0x018u /* RW1C {CQ, ERR} */
#define LPU_REG_INTR_ENABLE 0x020u /* RW */
#define LPU_REG_TICK_US 0x028u     /* RO  µs timestamp counter */
/* --- events (64 counting semaphores) --- */
#define LPU_REG_EVT_CTRL 0x040u /* RW  {enable, saturation interrupt enable} */
#define LPU_REG_EVT_STATE(n) (0x048u + 2u * (uint32_t)(n)) /* RO 2B/entry {count, waiter} */
#define LPU_REG_EVT_SHADOW_ADDR                                                                    \
    0x0C8u                            /* RW  event count shadow array base (host memory, 64×4B)   \
                                       */
#define LPU_REG_EVT_HOST_CLEAR 0x0D0u /* WO  host manual event clear (feeds the command FIFO) */
/* --- completion queue (CQ, host memory) --- */
#define LPU_REG_CQ_BASE_LO 0x0E0u /* RW */
#define LPU_REG_CQ_BASE_HI 0x0E4u /* RW */
#define LPU_REG_CQ_DEPTH 0x0E8u   /* RW */
#define LPU_REG_CQ_WP 0x0ECu      /* RO  device write pointer (host polling = golden path) */
#define LPU_REG_CQ_CP 0x0F0u      /* WO  host consumer pointer writeback */
#define LPU_REG_CQ_INT_THD 0x0F4u /* RW  MSI threshold (0 = pure polling) */
/* --- scheduling / watchdog --- */
#define LPU_REG_AGE_TH 0x100u     /* RW  aging threshold */
#define LPU_REG_WDT_THD 0x104u    /* RW  per-stream timeout */
#define LPU_REG_WDT_STATUS 0x108u /* RO  per-stream overflow flags */
#define LPU_REG_WDT_DBG 0x10Cu    /* RO  [3] latch {stream, cluster_id, core_mask, ts} */
/* --- submission queues (16 streams) --- */
/* Q_CTX[s] 16B/set, per-set layout (★ DEPTH|policy bitfield split is ASSUMED):
 *   +0x00 BASE_LO   +0x04 BASE_HI   +0x08 DEPTH[29:0]|policy[31:30]   +0x0C RP(RO) */
#define LPU_REG_Q_CTX(s) (0x140u + 16u * (uint32_t)(s))
#define LPU_Q_CTX_OFF_BASE_LO 0x0u
#define LPU_Q_CTX_OFF_BASE_HI 0x4u
#define LPU_Q_CTX_OFF_DEPTH 0x8u
#define LPU_Q_CTX_OFF_RP 0xCu
#define LPU_REG_Q_WP(s)                                                                            \
    (0x240u + 4u * (uint32_t)(s))       /* WO doorbell ×16 (writes the absolute index) */
#define LPU_REG_Q_RP_SHADOW_BASE 0x280u /* RW  RP shadow array base (host memory, 16×4B) */
/* --- core interface / debug --- */
#define LPU_REG_CORE_STATUS(n)                                                                     \
    (0x300u + 16u * (uint32_t)(n))  /* RO 16B/core {state, cluster_id, block_id, role, pc} */
#define LPU_REG_STREAM_RESET 0x380u /* WO {stream} */
#define LPU_REG_CORE_RESET 0x384u   /* WO {core} */

/* ------------------------------------------------------------------ */
/* Field definitions                                                   */
/* ------------------------------------------------------------------ */
/* IDENT: [31:0]=magic, later version fields extend per spec finalization (ASSUMED) */
#define LPU_IDENT_MAGIC 0x4C505530u                    /* "LPU0" (ASSUMED, spec sets no value) */
#define LPU_IDENT_VER(ver) ((uint32_t)(ver) & 0xffffu) /* ★ pending spec finalization */

/* CTRL: [0]=enable, [8:15]=core_enable */
#define LPU_CTRL_ENABLE (1u << 0)
#define LPU_CTRL_CORE_EN(n) (1u << (8 + (n)))

/* Q_CTX DEPTH|policy word (★ ASSUMED: DEPTH = raw slot count [29:0], policy [31:30]) */
#define LPU_Q_DEPTH(depth) ((uint32_t)(depth) & 0x3fffffffu)
#define LPU_Q_POLICY_NORMAL 0x0u
#define LPU_Q_POLICY_REUSE 0x1u
#define LPU_Q_POLICY_SEQ 0x2u
#define LPU_Q_POLICY(p) (((uint32_t)(p) & 0x3u) << 30)
#define LPU_Q_MAKE(depth, p) (LPU_Q_DEPTH(depth) | LPU_Q_POLICY(p))

/* INTR_STATUS / INTR_ENABLE bits */
#define LPU_INTR_CQ (1u << 0)
#define LPU_INTR_ERR (1u << 1)

/* stream / event / core counts (spec appendix A defaults) */
#define LPU_NUM_STREAMS 16
#define LPU_NUM_EVENTS 64
#define LPU_NUM_CORES 8

/* Register block size (last offset CORE_RESET=0x384 → rounded up to 0x400;
 * the fw selftest / QEMU device model allocate the register file by this) */
#define LPU_SCHED_REG_BYTES 0x400u

/* Register access helper (32-bit register file) */
#define LPU_REG_U32(off) ((off) / 4u)

#ifdef __cplusplus
}
#endif
#endif /* LPU_REGS_H */
