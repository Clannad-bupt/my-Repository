/*
 * lpu_desc.h — submission ring / task / cluster descriptor and CQ entry layouts
 *
 * Contract source: LPU-SCHED-SPEC.md v0.4 §3 in the QEMU verification tree (descriptor format
 * draft). Decision record (user confirmed): the descriptor format may change later; the submission
 * ring [slot] size is fixed at 256B while the [content] packed into a slot still follows the spec's
 * current 32B stream-level format — when the format evolves, only this file and the filling code
 * change; the framework does not. All sizes are expressed as constants; bare numbers are forbidden.
 *
 * Layout note: in the spec, CLUSTER's 64b fields land on 4mod8 offsets (+0x04/
 * +0x0C/+0x14), so the structs must be packed; for cross-side access, copy
 * the whole struct into an aligned local buffer first and only then read
 * fields (the fw side does exactly this).
 */
#ifndef LPU_DESC_H
#define LPU_DESC_H

#include <stddef.h>
#include <stdint.h>

#include "tndef.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Size / limit constants                                              */
/* ------------------------------------------------------------------ */
#define LPU_RING_SLOT                                                                              \
    256u /* submission ring slot (reserves room for format evolution; spec draft content is 32B)   \
          */
#define LPU_STREAM_DESC_SIZE 32u    /* stream-level descriptor content size (spec current draft) */
#define LPU_TASK_DESC_SIZE 32u      /* TASK descriptor */
#define LPU_CLUSTER_DESC_SIZE 256u  /* CLUSTER descriptor (8 blocks fully inlined) */
#define LPU_BLOCK_DESC_SIZE 24u     /* block entry inside a cluster */
#define LPU_CQ_ENTRY_SIZE 16u       /* CQ entry */
#define LPU_RING_DEPTH_DEF 1024u    /* default depth per stream (slots) */
#define LPU_CQ_DEPTH_DEF 256u       /* default CQ depth (runtime policy) */
#define LPU_MAX_CLUSTERS_TASK 64u   /* cluster limit per task */
#define LPU_MAX_BLOCKS_CLUSTER 8u   /* block limit per cluster */
#define LPU_MAX_TASKS_KERNEL 65535u /* KERNEL task_count field width limit (16b) */
#define LPU_MAX_PARAM_BYTES 128u    /* launch parameter block limit (runtime policy) */

/* stream-level descriptor type codes (3b, ★ ASSUMED — spec §3 lists names only, no numbering) */
enum lpu_desc_type {
    LPU_DESC_KERNEL = 0,
    LPU_DESC_COPY = 1,
    LPU_DESC_EVENT = 2,
    LPU_DESC_BARRIER = 3,
    LPU_DESC_CPU_OP = 4,
};

/* COPY directions (first three fixed by spec; H2D/D2H are ★ ASSUMED, pending §11-2 addition) */
enum lpu_copy_dir {
    LPU_COPY_DRAM2SRAM = 0,
    LPU_COPY_SRAM2DRAM = 1,
    LPU_COPY_DRAM2DRAM = 2,
    LPU_COPY_H2D = 3, /* ★ ASSUMED */
    LPU_COPY_D2H = 4, /* ★ ASSUMED */
};

/* EVENT actions */
enum lpu_event_action {
    LPU_EVT_SIGNAL = 0,
    LPU_EVT_CLEAR = 1,
};

/* dw0 bitfields (★ ASSUMED bit positions, spec does not define them):
 * [2:0] type  [3] peek(BARRIER)  [4] action(EVENT)  [5] intr
 * [7:4] copy.dir  [8] cq_done(CPU_OP/EVENT)                              */
#define LPU_DW0_TYPE(dw0) ((dw0) & 0x7u)
#define LPU_DW0_PEEK (1u << 3)
#define LPU_DW0_EVT_ACTION(dw0) (((dw0) >> 4) & 0x1u)
#define LPU_DW0_EVT_INTR (1u << 5)
#define LPU_DW0_CQ_DONE (1u << 8)
#define LPU_DW0_MAKE(type) ((uint32_t)(type) & 0x7u)
#define LPU_DW0_MAKE_COPY(dir) ((uint32_t)LPU_DESC_COPY | (((uint32_t)(dir) & 0xfu) << 4))
#define LPU_DW0_COPY_DIR(dw0) (((dw0) >> 4) & 0xfu)
#define LPU_DW0_MAKE_EVT(act) ((uint32_t)LPU_DESC_EVENT | (((uint32_t)(act) & 0x1u) << 4))
#define LPU_DW0_MAKE_BARRIER(peek) ((uint32_t)LPU_DESC_BARRIER | ((peek) ? LPU_DW0_PEEK : 0u))

/* ------------------------------------------------------------------ */
/* Stream-level descriptor (32B content, first 32B of the 256B slot).
 * Fields reused by type (spec §3.2 bit-level format still draft, ★ offsets are ASSUMED):
 *   KERNEL:  a=task_count(16b)          p0=task_array_ptr               */
/*   COPY:    a=len(32b)                 p0=src  p1=dst  p2=sgl_ptr      */
/*   EVENT:   a=event_id(16b)                                               */
/*   CPU_OP:  a=op_id(16b)               p0=op_cfg_ptr                    */
/*   BARRIER: p0=wait_mask                                                   */
/* ------------------------------------------------------------------ */
typedef struct __attribute__((packed)) {
    uint32_t dw0; /* [2:0] type | attr/dir/action                    */
    uint32_t a;   /* meaning depends on type (see above)             */
    uint64_t p0;
    uint64_t p1; /* COPY: dst, 0 otherwise                          */
    uint64_t p2; /* COPY: sgl_ptr (0 = linear, no SG), 0 otherwise  */
} lpu_stream_desc_t;

TN_STATIC_ASSERT(sizeof(lpu_stream_desc_t) == LPU_STREAM_DESC_SIZE,
                 "stream desc must be 32B per spec draft");

/* typed field access */
#define LPU_KERNEL_TASK_COUNT(d) ((d)->a & 0xffffu)
#define LPU_COPY_LEN(d) ((d)->a)
#define LPU_DESC_EVENT_ID(d) ((d)->a & 0xffffu)
#define LPU_DESC_CPU_OP_ID(d) ((d)->a & 0xffffu)

/* ------------------------------------------------------------------ */
/* TASK descriptor (32B)                                                */
/* ------------------------------------------------------------------ */
typedef struct __attribute__((packed)) {
    uint16_t cluster_count; /* ≤ LPU_MAX_CLUSTERS_TASK */
    uint16_t attr;
    uint32_t rsvd;
    uint64_t cluster_array_ptr;
    uint64_t task_signal_mask; /* SIGNALed when the task completes (all clusters done) */
    uint64_t rsvd2;            /* padding out to 32B (room for format evolution)      */
} lpu_task_desc_t;

TN_STATIC_ASSERT(sizeof(lpu_task_desc_t) == LPU_TASK_DESC_SIZE, "task desc must be 32B");

/* ------------------------------------------------------------------ */
/* CLUSTER descriptor (256B) and block entry (24B)                      */
/* ------------------------------------------------------------------ */
typedef struct __attribute__((packed)) {
    uint32_t n_rsvd;    /* [3:0] = N (1/2/4/8, SPMD gang core count)          */
    uint32_t pad;       /* padding: entry_pc lands at +0x08 (8B aligned)       */
    uint64_t entry_pc;  /* chip DRAM address                                   */
    uint64_t param_ptr; /* chip DRAM address                                   */
} lpu_block_desc_t;

TN_STATIC_ASSERT(sizeof(lpu_block_desc_t) == LPU_BLOCK_DESC_SIZE, "block entry must be 24B");

typedef struct __attribute__((packed)) {
    uint32_t hdr;               /* +0x00 [3:0] block_count, [4] need_sync,
                                 *        [5] intr, [6] cq_done               */
    uint64_t wait_mask;         /* +0x04 consumed by EVAL (-- per hit)        */
    uint64_t signal_mask;       /* +0x0C SIGNALed on cluster completion       */
    uint64_t cluster_param_ptr; /* +0x14 cluster-level shared params (chip DRAM) */
    uint32_t pad;               /* +0x1C */
    lpu_block_desc_t block[LPU_MAX_BLOCKS_CLUSTER]; /* +0x20..0xDF */
    uint8_t reserved[0x20]; /* +0xE0..0xFF reserved (room for format evolution) */
} lpu_cluster_desc_t;

TN_STATIC_ASSERT(sizeof(lpu_cluster_desc_t) == LPU_CLUSTER_DESC_SIZE, "cluster desc must be 256B");
TN_STATIC_ASSERT(offsetof(lpu_cluster_desc_t, block) == 0x20, "block[] at +0x20");
TN_STATIC_ASSERT(offsetof(lpu_cluster_desc_t, wait_mask) == 0x04, "wait_mask at +0x04");
TN_STATIC_ASSERT(offsetof(lpu_cluster_desc_t, cluster_param_ptr) == 0x14,
                 "cluster_param_ptr at +0x14");

/* cluster hdr bitfields */
#define LPU_CLUSTER_BLOCKCNT(hdr) ((hdr) & 0xfu)
#define LPU_CLUSTER_NEED_SYNC (1u << 4)
#define LPU_CLUSTER_INTR (1u << 5)
#define LPU_CLUSTER_CQ_DONE (1u << 6)
#define LPU_CLUSTER_MAKE(cnt, attr) (((uint32_t)(cnt) & 0xfu) | (uint32_t)(attr))

/* block N */
#define LPU_BLOCK_N(n_rsvd) ((n_rsvd) & 0xfu)
#define LPU_BLOCK_MAKE(n) ((uint32_t)(n) & 0xfu)

/* ------------------------------------------------------------------ */
/* CQ entry (16B, host memory)                                          */
/* ------------------------------------------------------------------ */
/* type codes (★ ASSUMED; KERNEL_DONE..ERR come from §6.8, CPU_OP_DONE from §6.9) */
enum lpu_cq_type {
    LPU_CQ_KERNEL_DONE = 1,
    LPU_CQ_CLUSTER_DONE = 2,
    LPU_CQ_COPY_DONE = 3,
    LPU_CQ_ERR = 4,
    LPU_CQ_CPU_OP_DONE = 5,
};

typedef struct {
    uint32_t type;   /* enum lpu_cq_type */
    uint32_t id;     /* ★ ASSUMED: completion types = stream index; ERR = faulting stream index */
    uint32_t status; /* 0 = OK; error code on ERR */
    uint32_t timestamp_us; /* TICK_US snapshot (32b, wraps in ~71.6min) */
} lpu_cq_entry_t;

TN_STATIC_ASSERT(sizeof(lpu_cq_entry_t) == LPU_CQ_ENTRY_SIZE, "CQ entry must be 16B");

/* ------------------------------------------------------------------ */
/* RP shadow array (host memory, 16 × 4B, absolute index) and event
 * shadow array (64 × 4B)                                              */
/* ------------------------------------------------------------------ */
#define LPU_RP_SHADOW_BYTES (LPU_NUM_STREAMS * 4u)
#define LPU_EVT_SHADOW_BYTES (LPU_NUM_EVENTS * 4u)

#ifdef __cplusplus
}
#endif
#endif /* LPU_DESC_H */
