/*
 * rt_internal.h — liblpu_rt internal structures (not installed externally)
 *
 * Layering: public API (lpu_rt.h) → internal management (stream/event/module/launch) →
 *           submission ring/CQ/events (rt_ring/rt_cq, backend-agnostic) → backend (sim|device).
 * Ring, CQ, and shadow arrays all live in host coherent memory; device
 * registers are accessed via be->regs (already offset by LPU_SCHED_REG_BASE) —
 * which lets the sim backend impersonate the whole register file with one
 * plain block of memory.
 */
#ifndef RT_INTERNAL_H
#define RT_INTERNAL_H

#include "lpu/lpu_desc.h"
#include "lpu/lpu_regs.h"
#include "lpu_rt.h"

#include <pthread.h>
#include <stdbool.h>

#define LPU_RT_API __attribute__((visibility("default")))

/* Device DRAM allocation granularity and initial metadata */
#define LPU_POOL_ALIGN 256u

/* ------------------------------------------------------------------ */
/* Device DRAM pool (first-fit + free list over the BAR1 window)        */
/* ------------------------------------------------------------------ */
typedef struct lpu_memblk {
    struct lpu_memblk *next;
    uint64_t off;   /* offset within the window */
    uint64_t size;  /* requested size */
    uint64_t bsize; /* aligned block size */
    bool free;
} lpu_memblk_t;

typedef struct {
    uint8_t *win;      /* window host base (BAR1 mmap or sim fake DRAM) */
    uint64_t dev_base; /* device-side base (dram_dev_base) */
    uint64_t size;
    lpu_memblk_t *head; /* block list in address order */
    pthread_mutex_t lock;
} lpu_pool_t;

lpuRtError rt_pool_init(lpu_pool_t *p, uint8_t *win, uint64_t dev_base, uint64_t size);
void rt_pool_fini(lpu_pool_t *p);
/* Returns the device-side address (dev_base + off); the block descriptor is hung off *blk for the
 * free */
lpuRtError rt_pool_alloc(lpu_pool_t *p, uint64_t size, uint64_t align, lpu_memblk_t **blk,
                         uint64_t *dev_addr);
void rt_pool_free(lpu_pool_t *p, lpu_memblk_t *blk);

/* ------------------------------------------------------------------ */
/* Backend abstraction                                                   */
/* ------------------------------------------------------------------ */
typedef struct lpu_backend lpu_backend_t;

typedef struct {
    const char *name;
    lpuRtError (*init)(lpu_backend_t *b);
    void (*fini)(lpu_backend_t *b);
    /* host coherent memory (ring/CQ/shadow arrays) */
    lpuRtError (*host_alloc)(lpu_backend_t *b, size_t size, size_t align, void **va,
                             uint64_t *bus_addr);
    void (*host_free)(lpu_backend_t *b, void *va, uint64_t bus_addr, size_t size);
    /* doorbell: write Q_WP[s] (WC). The sim backend consumes descriptors synchronously here */
    void (*doorbell)(lpu_backend_t *b, int stream, uint32_t wp);
    /* let the device side re-evaluate outstanding work (real scheduler is
     * self-driving → no-op; sim only advances on doorbell, and polling paths
     * go through this so budget/barrier-blocked descriptors cannot wedge forever) */
    void (*kick)(lpu_backend_t *b);
    /* host manual event clear: write EVT_HOST_CLEAR (single-writer discipline) */
    void (*event_clear)(lpu_backend_t *b, int ev);
    /* posted-write flush: device = read IDENT back; sim = compiler barrier */
    void (*flush)(lpu_backend_t *b);
} lpu_backend_ops_t;

struct lpu_backend {
    const lpu_backend_ops_t *ops;
    volatile uint32_t *regs; /* scheduler register file (LPU_SCHED_REG_BASE already folded in) */
    uint8_t *dram;           /* chip DRAM window host base */
    uint64_t dram_dev_base;  /* device-side DRAM base */
    uint64_t dram_size;
    void *priv; /* backend private (sim / device respectively) */
};

/* backend_sim.c */
lpuRtError rt_backend_sim_register(lpu_backend_t *b);
/* backend_dev.c */
lpuRtError rt_backend_dev_register(lpu_backend_t *b);

/* device-side DRAM address ↔ window host pointer */
static inline uint8_t *rt_dram_ptr(lpu_backend_t *b, uint64_t dev_addr) {
    return b->dram + (size_t)(dev_addr - b->dram_dev_base);
}

/* ------------------------------------------------------------------ */
/* Safe memset/memcpy on device windows (BAR0/BAR1) — 8-byte narrow accesses */
/* ------------------------------------------------------------------ */
/* The device backend's BAR0/BAR1 are vfio-user MMIO (device pages mapped on
 * the guest side via io_remap_pfn_range). Once a guest access to MMIO traps
 * into KVM through an EPT violation, the kernel has to emulate that
 * instruction to construct the userspace MMIO exit, and the 5.15 kernel's
 * x86 emulator does not support 256-bit AVX accesses → emulation failure
 * injects #UD (proven in E2E on 2026-10-31: glibc's memset ifunc picks
 * __memset_avx2_unaligned_erms, and a vmovdqu %ymm0 onto a BAR1 arena page
 * → SIGILL; memsets ≥2KB survive because they take the rep stosb byte
 * path). Same discipline as RISC-V RVV being barred from Device-attribute
 * memory: no wide vector accesses on device windows. Always step by 8 bytes
 * (64-bit MMIO emulation is fully supported), volatile to keep the compiler
 * from vectorizing the stepping loop. The sim backend is plain RAM — the
 * same implementation is just slightly slower, behavior identical. */
static inline void rt_dmemset(void *dst, int c, size_t n) {
    uint8_t *p = dst;
    while (n && ((uintptr_t)p & 7u)) {
        *(volatile uint8_t *)p = (uint8_t)c;
        p++;
        n--;
    }
    uint64_t fill = (uint64_t)(uint8_t)c * 0x0101010101010101ull;
    for (; n >= 8; n -= 8, p += 8)
        *(volatile uint64_t *)p = fill;
    while (n) {
        *(volatile uint8_t *)p = (uint8_t)c;
        p++;
        n--;
    }
}

static inline void rt_dmemcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n && ((uintptr_t)d & 7u)) {
        *(volatile uint8_t *)d = *s;
        d++;
        s++;
        n--;
    }
    for (; n >= 8; n -= 8, d += 8, s += 8)
        *(volatile uint64_t *)d = *(const volatile uint64_t *)s;
    while (n) {
        *(volatile uint8_t *)d = *s;
        d++;
        s++;
        n--;
    }
}

/* DtoD overlapping copy (back-to-front when dst > src) */
static inline void rt_dmemmove(void *dst, const void *src, size_t n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d == s || n == 0)
        return;
    if (d < s || d >= s + n) {
        rt_dmemcpy(d, s, n);
        return;
    }
    uint8_t *de = d + n;
    const uint8_t *se = s + n;
    while (n && ((uintptr_t)de & 7u)) {
        *--de = *--se;
        n--;
    }
    while (n >= 8) {
        de -= 8;
        se -= 8;
        *(volatile uint64_t *)de = *(const volatile uint64_t *)se;
        n -= 8;
    }
    while (n) {
        *--de = *--se;
        n--;
    }
}

/* Register read/write helpers (32b) */
static inline void rt_reg_write(lpu_backend_t *b, uint32_t off, uint32_t val) {
    b->regs[off / 4u] = val;
}
static inline uint32_t rt_reg_read(lpu_backend_t *b, uint32_t off) { return b->regs[off / 4u]; }

/* ------------------------------------------------------------------ */
/* Kernel launch resources (a single device DRAM block freed on completion) */
/* ------------------------------------------------------------------ */
typedef struct {
    lpu_memblk_t *blk; /* one block holding [task_desc | cluster_array | param] */
} lpu_launch_t;

/* in-stream ordering ⇒ completions pop in submission order */
#define RT_LAUNCH_FIFO_DEPTH 64

typedef struct {
    lpu_launch_t item[RT_LAUNCH_FIFO_DEPTH];
    uint32_t head, tail, count;
} lpu_launch_fifo_t;

static inline bool rt_fifo_push(lpu_launch_fifo_t *f, const lpu_launch_t *l) {
    if (f->count == RT_LAUNCH_FIFO_DEPTH)
        return false;
    f->item[f->tail] = *l;
    f->tail = (f->tail + 1) % RT_LAUNCH_FIFO_DEPTH;
    f->count++;
    return true;
}
static inline bool rt_fifo_pop(lpu_launch_fifo_t *f, lpu_launch_t *l) {
    if (f->count == 0)
        return false;
    *l = f->item[f->head];
    f->head = (f->head + 1) % RT_LAUNCH_FIFO_DEPTH;
    f->count--;
    return true;
}

/* ------------------------------------------------------------------ */
/* stream / event / module / function                                    */
/* ------------------------------------------------------------------ */
struct lpu_stream {
    int idx; /* 0..LPU_NUM_STREAMS-1 */
    struct lpu_ctx *ctx;
    uint8_t *ring_va; /* host coherent ring base */
    uint64_t ring_bus;
    uint32_t wp;                  /* absolute write pointer (host-side copy) */
    uint32_t rp_cached;           /* RP shadow cache (for space reclamation) */
    volatile uint32_t *rp_shadow; /* points at ctx->rp_sh_va[idx] */
    int inflight;                 /* submitted but not done (kernel/copy/event/barrier) */
    bool errored;
    uint32_t err_status;
    lpu_launch_fifo_t launches;
};

struct lpu_event {
    int id; /* 0..LPU_NUM_EVENTS-1 */
    bool allocated;
    bool recorded;      /* SIGNAL submitted */
    bool latched;       /* SIGNAL fired and its consumption decided */
    bool host_consumed; /* consumed by the host via EVT_HOST_CLEAR */
    int waiter_stream;  /* -1 or the stream index waiting on it (the device BARRIER consumer) */
    int signal_stream;  /* stream index that recorded the SIGNAL */
    uint32_t signal_wp; /* absolute index of the SIGNAL descriptor (recording stream's RP passing it
                           = fired) */
    uint64_t done_ns;   /* latch time (host clock, used by ElapsedTime) */
};

struct lpu_function {
    struct lpu_module *mod;
    char name[64];
    uint64_t entry_off; /* relative to the module base (ELF vaddr) */
};

struct lpu_module {
    lpu_memblk_t *blk; /* load block (the whole PT_LOAD span) */
    uint64_t base;     /* device-side address where vaddr 0 lands (PIC base) */
    uint64_t min_vaddr;
    uint64_t span_vaddr;        /* [min_vaddr, min_vaddr + span) */
    uint64_t entry_vaddr;       /* e_entry (GetFunction(NULL)) */
    struct lpu_function *funcs; /* function table extracted from the image .symtab at load time */
    int nfuncs;
};

/* ------------------------------------------------------------------ */
/* fw export symbol table (extracted and cached by lpuRtSetFirmwareElf from the fw ELF .symtab) */
/* ------------------------------------------------------------------ */
#define RT_FW_MAX_EXPORTS 64

struct lpu_fw_export {
    char name[64];
    uint64_t addr; /* fw link address (absolute; fw single image → identical on every core) */
};

/* ------------------------------------------------------------------ */
/* Global context (v1 single device)                                    */
/* ------------------------------------------------------------------ */
typedef struct lpu_ctx {
    bool inited;
    int cur_dev;
    lpu_backend_t be;
    lpu_pool_t pool;

    /* host coherent shared regions */
    uint8_t *cq_va;
    uint64_t cq_bus;
    uint32_t cq_depth;
    uint32_t cq_cp;    /* host consume pointer (absolute) */
    uint8_t *rp_sh_va; /* 16×4B */
    uint64_t rp_sh_bus;
    uint8_t *evt_sh_va; /* 64×4B */
    uint64_t evt_sh_bus;

    uint32_t ring_depth;

    struct lpu_stream *streams[LPU_NUM_STREAMS];
    struct lpu_event *events[LPU_NUM_EVENTS];

    /* fw export table (parsed by lpuRtSetFirmwareElf in rt_module.c) */
    struct lpu_fw_export *fw_exports;
    int nfw_exports;
} lpu_ctx_t;

/* global singleton (v1 single-threaded convention; owned by rt_init.c) */
lpu_ctx_t *rt_ctx(void);
lpu_ctx_t *rt_ctx_checked(void); /* returns NULL when not initialized */

/* rt_error.c: TLS last error */
void rt_set_last_error(lpuRtError e);

/* rt_error.c: integration-debug log (stderr; LPU_RT_LOG=0 off, default on). Usage:
 * rt_log_("step: value=%#x", v); — drop breadcrumbs at every key step while debugging integration
 */
void rt_log_(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* rt_ring.c */
lpuRtError rt_ring_submit(struct lpu_stream *st, const lpu_stream_desc_t *d);
void rt_ring_refresh_rp(struct lpu_stream *st);
uint64_t rt_now_ns(void);

/* rt_cq.c: poll the CQ and dispatch completions; returns false = no progress this round */
void rt_cq_poll(lpu_ctx_t *c);
/* wait for a stream to drain */
lpuRtError rt_stream_drain(struct lpu_stream *st);

/* rt_stream.c */
lpuRtError rt_stream_setup(struct lpu_stream *st); /* allocate ring + program Q_CTX */

/* rt_launch.c */
lpuRtError rt_launch_build(struct lpu_ctx *c, struct lpu_function *fn, int group_count,
                           int block_count, const void *msg, size_t msg_bytes,
                           struct lpu_stream *st);

/* rt_event.c */
lpuRtError rt_event_signal_poll(struct lpu_ctx *c, int ev, bool *signaled);

#endif /* RT_INTERNAL_H */
