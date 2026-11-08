/* backend_sim.c — sim backend: impersonate the scheduler register file / chip
 * DRAM / host coherent regions with plain host memory. Descriptors are
 * evaluated synchronously at doorbell time — going through exactly the same
 * register / shared-region paths as the real scheduler (Q_CTX read for the
 * ring base, lazy RP writeback to the shadow, CQ written back via CQ_BASE,
 * event counts shadowed via EVT_SHADOW_ADDR), so the runtime's programming
 * sequence is exercised for real.
 *
 * Model boundary (explicitly not simulated): QoS/aging/watchdog, MSI
 * interrupts, CORE_STATUS, SRAM (COPY's DRAM2SRAM/SRAM2DRAM reports ERR),
 * concurrent core execution ordering.
 * KERNEL calls back into a host function via the exec hook (lpu_rt_sim.h) to
 * emulate operator execution.
 *
 * Advance timing: the real scheduler is self-driving; sim only advances on
 * doorbell/kick. kick is called by rt_cq_poll every round, so descriptors
 * blocked on cross-stream barriers or CQ budget cannot wedge forever on the
 * pure-polling path.
 */
#include "lpu_rt_sim.h"
#include "rt_internal.h"

#include <stdlib.h>
#include <string.h>

#define SIM_REGS_BYTES 0x400u /* covers CORE_RESET(0x384) */
#define SIM_DRAM_BYTES (64ull * 1024 * 1024)
#define SIM_DRAM_BASE 0x80000000ull
/* fake bus addresses must stay < 4GB: Q_RP_SHADOW_BASE / EVT_SHADOW_ADDR are 32b registers */
#define SIM_HOST_BUS_BASE 0x40000000ull
#define SIM_MAX_REGIONS 32

typedef struct {
    void *va;
    uint64_t bus;
    size_t size;
} sim_region_t;

typedef struct {
    uint32_t evt_count[LPU_NUM_EVENTS];
    sim_region_t regions[SIM_MAX_REGIONS];
    int nregions;
    uint64_t bus_next;
    lpuRtSimExecFn hook;
    uint32_t tick_us;
} sim_priv_t;

/* ---------------- fake bus / device addresses ---------------- */

static void *sim_bus_to_va(sim_priv_t *p, uint64_t bus) {
    for (int i = 0; i < p->nregions; i++)
        if (bus >= p->regions[i].bus && bus < p->regions[i].bus + p->regions[i].size)
            return (uint8_t *)p->regions[i].va + (bus - p->regions[i].bus);
    return NULL;
}

static uint8_t *sim_dev_ptr(lpu_backend_t *b, uint64_t dev) {
    if (dev < b->dram_dev_base || dev >= b->dram_dev_base + b->dram_size)
        return NULL;
    return b->dram + (size_t)(dev - b->dram_dev_base);
}

/* ---------------- CQ / event shadow writes ---------------- */

static uint32_t sim_cq_free(lpu_backend_t *b) {
    uint32_t depth = b->regs[LPU_REG_CQ_DEPTH / 4u];
    if (depth == 0)
        return 0;
    return depth - (b->regs[LPU_REG_CQ_WP / 4u] - b->regs[LPU_REG_CQ_CP / 4u]);
}

static void sim_cq_write(lpu_backend_t *b, uint32_t type, uint32_t id, uint32_t status) {
    sim_priv_t *p = b->priv;
    uint32_t depth = b->regs[LPU_REG_CQ_DEPTH / 4u];
    uint32_t wp = b->regs[LPU_REG_CQ_WP / 4u];
    if (depth == 0 || wp - b->regs[LPU_REG_CQ_CP / 4u] >= depth)
        return; /* CQ full: drop (in the normal flow the host polls on every submit, never hits
                   this) */

    uint64_t base = (uint64_t)b->regs[LPU_REG_CQ_BASE_LO / 4u] |
                    ((uint64_t)b->regs[LPU_REG_CQ_BASE_HI / 4u] << 32);
    volatile lpu_cq_entry_t *e = sim_bus_to_va(p, base);
    if (!e)
        return;
    e = (volatile lpu_cq_entry_t *)((uint8_t *)e + (size_t)(wp & (depth - 1)) * LPU_CQ_ENTRY_SIZE);
    e->type = type;
    e->id = id;
    e->status = status;
    e->timestamp_us = ++p->tick_us;
    b->regs[LPU_REG_CQ_WP / 4u] = wp + 1;
}

static void sim_evt_write(lpu_backend_t *b, int id, uint32_t val) {
    sim_priv_t *p = b->priv;
    p->evt_count[id] = val;
    volatile uint32_t *sh = sim_bus_to_va(p, b->regs[LPU_REG_EVT_SHADOW_ADDR / 4u]);
    if (sh)
        sh[id] = val;
}

/* ---------------- descriptor evaluation ---------------- */

static void sim_process_kernel(lpu_backend_t *b, int sid, const lpu_stream_desc_t *d) {
    sim_priv_t *p = b->priv;
    uint32_t status = 0;
    uint16_t ntasks = LPU_KERNEL_TASK_COUNT(d);

    for (uint16_t t = 0; t < ntasks && status == 0; t++) {
        const lpu_task_desc_t *tp =
            (const lpu_task_desc_t *)sim_dev_ptr(b, d->p0 + (uint64_t)t * LPU_TASK_DESC_SIZE);
        if (!tp) {
            status = 1;
            break;
        }
        lpu_task_desc_t task;
        memcpy(&task, tp, sizeof(task)); /* packed → aligned local buffer */

        for (uint16_t ci = 0; ci < task.cluster_count && status == 0; ci++) {
            const lpu_cluster_desc_t *cp = (const lpu_cluster_desc_t *)sim_dev_ptr(
                b, task.cluster_array_ptr + (uint64_t)ci * LPU_CLUSTER_DESC_SIZE);
            if (!cp) {
                status = 1;
                break;
            }
            lpu_cluster_desc_t cl;
            memcpy(&cl, cp, sizeof(cl));

            /* wait_mask: consumed on EVAL. In this synchronous model an
             * unmet mask fails the stream (runtime-generated kernels always
             * have this mask 0; cross-stream ordering is guaranteed beforehand
             * by the stream-level BARRIER) */
            for (int e = 0; e < LPU_NUM_EVENTS; e++) {
                if ((cl.wait_mask >> e) & 1u) {
                    if (p->evt_count[e] == 0) {
                        status = 1;
                        break;
                    }
                    sim_evt_write(b, e, p->evt_count[e] - 1);
                }
            }
            if (status)
                break;

            /* each block × N cores → exec hook (emulating core execution) */
            for (uint32_t bi = 0; bi < LPU_CLUSTER_BLOCKCNT(cl.hdr); bi++) {
                uint32_t n = LPU_BLOCK_N(cl.block[bi].n_rsvd);
                for (uint32_t role = 0; role < n; role++) {
                    if (p->hook)
                        p->hook(cl.block[bi].entry_pc, cl.block[bi].param_ptr, cl.cluster_param_ptr,
                                ci, bi, role, n);
                }
            }

            for (int e = 0; e < LPU_NUM_EVENTS; e++)
                if ((cl.signal_mask >> e) & 1u)
                    sim_evt_write(b, e, p->evt_count[e] + 1);
        }

        for (int e = 0; e < LPU_NUM_EVENTS; e++)
            if ((task.task_signal_mask >> e) & 1u)
                sim_evt_write(b, e, p->evt_count[e] + 1);
    }

    if (status)
        sim_cq_write(b, LPU_CQ_ERR, (uint32_t)sid, status);
    else
        sim_cq_write(b, LPU_CQ_KERNEL_DONE, (uint32_t)sid, 0);
}

static void sim_process_copy(lpu_backend_t *b, int sid, const lpu_stream_desc_t *d) {
    sim_priv_t *p = b->priv;
    uint8_t *src = NULL, *dst = NULL;

    switch (LPU_DW0_COPY_DIR(d->dw0)) {
    case LPU_COPY_DRAM2DRAM:
        src = sim_dev_ptr(b, d->p0);
        dst = sim_dev_ptr(b, d->p1);
        break;
    case LPU_COPY_H2D:
        src = sim_bus_to_va(p, d->p0);
        dst = sim_dev_ptr(b, d->p1);
        break;
    case LPU_COPY_D2H:
        src = sim_dev_ptr(b, d->p0);
        dst = sim_bus_to_va(p, d->p1);
        break;
    default:
        sim_cq_write(b, LPU_CQ_ERR, (uint32_t)sid, 2); /* SRAM not modeled */
        return;
    }
    if (!src || !dst) {
        sim_cq_write(b, LPU_CQ_ERR, (uint32_t)sid, 1);
        return;
    }
    memmove(dst, src, d->a);
    sim_cq_write(b, LPU_CQ_COPY_DONE, (uint32_t)sid, 0);
}

static bool sim_barrier_ready(lpu_backend_t *b, const lpu_stream_desc_t *d) {
    sim_priv_t *p = b->priv;
    for (int e = 0; e < LPU_NUM_EVENTS; e++)
        if ((d->p0 >> e) & 1u && p->evt_count[e] == 0)
            return false;
    return true;
}

static void sim_barrier_consume(lpu_backend_t *b, const lpu_stream_desc_t *d) {
    sim_priv_t *p = b->priv;
    for (int e = 0; e < LPU_NUM_EVENTS; e++)
        if ((d->p0 >> e) & 1u)
            sim_evt_write(b, e, p->evt_count[e] - 1);
}

/* ---------------- advance engine (shared by doorbell / kick) ---------------- */

static void sim_engine(lpu_backend_t *b) {
    sim_priv_t *p = b->priv;
    bool progress = true;

    while (progress) {
        progress = false;
        for (int s = 0; s < LPU_NUM_STREAMS; s++) {
            uint32_t ctx = LPU_REG_Q_CTX((uint32_t)s);
            uint64_t base = (uint64_t)b->regs[(ctx + LPU_Q_CTX_OFF_BASE_LO) / 4u] |
                            ((uint64_t)b->regs[(ctx + LPU_Q_CTX_OFF_BASE_HI) / 4u] << 32);
            uint32_t depth = LPU_Q_DEPTH(b->regs[(ctx + LPU_Q_CTX_OFF_DEPTH) / 4u]);
            uint8_t *ring = sim_bus_to_va(p, base);
            if (!ring || depth == 0)
                continue;

            uint32_t qwp = b->regs[LPU_REG_Q_WP((uint32_t)s) / 4u];
            uint32_t rp = b->regs[(ctx + LPU_Q_CTX_OFF_RP) / 4u];

            while (rp < qwp) {
                if (sim_cq_free(b) == 0)
                    break; /* CQ budget exhausted: wait for the host to poll, then kick again */

                lpu_stream_desc_t d;
                memcpy(&d, ring + (size_t)(rp & (depth - 1)) * LPU_RING_SLOT, LPU_STREAM_DESC_SIZE);

                uint32_t type = LPU_DW0_TYPE(d.dw0);
                if (type == LPU_DESC_BARRIER && !sim_barrier_ready(b, &d))
                    break; /* stream blocked on BARRIER: wait for another stream's SIGNAL (outer
                              loop retries) */

                switch (type) {
                case LPU_DESC_KERNEL:
                    sim_process_kernel(b, s, &d);
                    break;
                case LPU_DESC_COPY:
                    sim_process_copy(b, s, &d);
                    break;
                case LPU_DESC_EVENT: {
                    int eid = (int)LPU_DESC_EVENT_ID(&d);
                    if (LPU_DW0_EVT_ACTION(d.dw0) == LPU_EVT_SIGNAL)
                        sim_evt_write(b, eid, p->evt_count[eid] + 1);
                    else
                        sim_evt_write(b, eid, 0);
                    break;
                }
                case LPU_DESC_BARRIER:
                    sim_barrier_consume(b, &d);
                    break;
                default:
                    sim_cq_write(b, LPU_CQ_ERR, (uint32_t)s, 2);
                    break;
                }
                rp++;
                progress = true;
            }

            /* lazy RP writeback: Q_CTX.RP mirror + host shadow array (absolute index) */
            if (rp != b->regs[(ctx + LPU_Q_CTX_OFF_RP) / 4u]) {
                b->regs[(ctx + LPU_Q_CTX_OFF_RP) / 4u] = rp;
                volatile uint32_t *sh = sim_bus_to_va(p, b->regs[LPU_REG_Q_RP_SHADOW_BASE / 4u]);
                if (sh)
                    sh[s] = rp;
            }
        }
    }
}

/* ---------------- backend vtable ---------------- */

static lpuRtError sim_init(lpu_backend_t *b) {
    sim_priv_t *p = calloc(1, sizeof(*p));
    if (!p)
        return LPU_RT_ERROR_OUT_OF_MEMORY;

    b->regs = calloc(1, SIM_REGS_BYTES);
    b->dram = malloc(SIM_DRAM_BYTES);
    if (!b->regs || !b->dram) {
        free((void *)b->regs);
        free(b->dram);
        free(p);
        return LPU_RT_ERROR_OUT_OF_MEMORY;
    }
    memset(b->dram, 0, SIM_DRAM_BYTES);

    b->regs[LPU_REG_IDENT / 4u] = LPU_IDENT_MAGIC;
    b->dram_dev_base = SIM_DRAM_BASE;
    b->dram_size = SIM_DRAM_BYTES;
    p->bus_next = SIM_HOST_BUS_BASE;
    b->priv = p;
    return LPU_RT_SUCCESS;
}

static void sim_fini(lpu_backend_t *b) {
    sim_priv_t *p = b->priv;
    if (p) {
        for (int i = 0; i < p->nregions; i++)
            free(p->regions[i].va);
        free(p);
    }
    free((void *)b->regs);
    free(b->dram);
    b->regs = NULL;
    b->dram = NULL;
    b->priv = NULL;
}

static lpuRtError sim_host_alloc(lpu_backend_t *b, size_t size, size_t align, void **va,
                                 uint64_t *bus_addr) {
    sim_priv_t *p = b->priv;
    if (align == 0)
        align = 4096;
    if (align & (align - 1))
        return LPU_RT_ERROR_INVALID_VALUE;
    if (p->nregions >= SIM_MAX_REGIONS)
        return LPU_RT_ERROR_OUT_OF_MEMORY;

    void *m = NULL;
    if (posix_memalign(&m, align < sizeof(void *) ? sizeof(void *) : align, size) != 0 || !m)
        return LPU_RT_ERROR_OUT_OF_MEMORY;
    memset(m, 0, size);

    /* fake bus address: page-granular increments, staying < 4GB (32b base register constraint) */
    p->regions[p->nregions].va = m;
    p->regions[p->nregions].bus = p->bus_next;
    p->regions[p->nregions].size = size;
    *bus_addr = p->bus_next;
    p->bus_next += (size + 0xfff) & ~0xfffull;
    p->nregions++;
    *va = m;
    return LPU_RT_SUCCESS;
}

static void sim_host_free(lpu_backend_t *b, void *va, uint64_t bus_addr, size_t size) {
    sim_priv_t *p = b->priv;
    (void)bus_addr;
    (void)size;
    for (int i = 0; i < p->nregions; i++) {
        if (p->regions[i].va == va) {
            free(va);
            p->regions[i] = p->regions[p->nregions - 1];
            p->nregions--;
            return;
        }
    }
}

static void sim_doorbell(lpu_backend_t *b, int stream, uint32_t wp) {
    /* doorbell = write Q_WP[s] (absolute index) + trigger fetch */
    b->regs[LPU_REG_Q_WP((uint32_t)stream) / 4u] = wp;
    sim_engine(b);
}

static void sim_kick(lpu_backend_t *b) { sim_engine(b); }

static void sim_event_clear(lpu_backend_t *b, int ev) { sim_evt_write(b, ev, 0); }

static void sim_flush(lpu_backend_t *b) {
    /* sim has no WC buffering: a compiler/memory barrier is enough */
    __asm__ __volatile__("" ::: "memory");
    (void)b;
}

lpuRtError rt_backend_sim_register(lpu_backend_t *b) {
    static const lpu_backend_ops_t ops = {
        .name = "sim",
        .init = sim_init,
        .fini = sim_fini,
        .host_alloc = sim_host_alloc,
        .host_free = sim_host_free,
        .doorbell = sim_doorbell,
        .kick = sim_kick,
        .event_clear = sim_event_clear,
        .flush = sim_flush,
    };
    b->ops = &ops;
    return LPU_RT_SUCCESS;
}

/* ---------------- sim debug helpers (lpu_rt_sim.h) ---------------- */

static bool backend_is_sim(lpu_ctx_t *c) { return strcmp(c->be.ops->name, "sim") == 0; }

LPU_RT_API lpuRtError lpuRtSimSetExecHook(lpuRtSimExecFn fn) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    if (!backend_is_sim(c)) {
        rt_set_last_error(LPU_RT_ERROR_UNSUPPORTED);
        return LPU_RT_ERROR_UNSUPPORTED;
    }
    ((sim_priv_t *)c->be.priv)->hook = fn;
    return LPU_RT_SUCCESS;
}

LPU_RT_API lpuRtError lpuRtSimPtrToHost(lpuRtDevicePtr devPtr, void **hostPtr) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    if (!hostPtr)
        return LPU_RT_ERROR_INVALID_VALUE;
    if (!backend_is_sim(c)) {
        rt_set_last_error(LPU_RT_ERROR_UNSUPPORTED);
        return LPU_RT_ERROR_UNSUPPORTED;
    }
    if (devPtr < c->be.dram_dev_base || devPtr >= c->be.dram_dev_base + c->be.dram_size)
        return LPU_RT_ERROR_INVALID_VALUE;
    *hostPtr = rt_dram_ptr(&c->be, devPtr);
    return LPU_RT_SUCCESS;
}

LPU_RT_API lpuRtError lpuRtSimModuleBase(lpuRtModule module, uint64_t *dev_base) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    if (!module || !module->blk || !dev_base)
        return LPU_RT_ERROR_INVALID_HANDLE;
    if (!backend_is_sim(c)) {
        rt_set_last_error(LPU_RT_ERROR_UNSUPPORTED);
        return LPU_RT_ERROR_UNSUPPORTED;
    }
    *dev_base = module->base;
    return LPU_RT_SUCCESS;
}
