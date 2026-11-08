/* backend_dev.c — device backend: /dev/lpu0 + ioctl/mmap (ABI in lpu_ioctl.h)
 *
 * The driver is a thin driver (no scheduler semantics): this backend does
 * three things —
 *   1. mmap BAR0 (scheduler register window; regs = bar0 + LPU_SCHED_REG_BASE)
 *   2. mmap BAR1 (chip DRAM window; the runtime pool starts at
 *      LPU_DRAM_POOL_BASE, skipping the fw image / host arena reserved
 *      regions — one layout reference: lpu_dram_map.h)
 *   3. the "host coherent" shared regions (ring/CQ/shadow arrays): the card
 *      side has no host window, so they are carved from the reserved region
 *      inside the BAR1 window starting at LPU_HOST_ARENA_BASE (the spec's
 *      intent was host-coherent memory + an EP outbound window; the deviation
 *      is recorded in docs/rt/lpu-sw-stack.md §6). The addresses are card
 *      DRAM addresses (<4G, naturally satisfying the 32-bit shadow base
 *      registers); ap fw reads/writes them directly through the ep_if
 *      identity mapping — zero changes to the fw engines. The HOST_ALLOC
 *      ioctl is no longer used.
 * Doorbell / event clear = register writes; posted-write flush = IDENT
 * readback (drains WC). The real scheduler is self-driving; kick is a no-op.
 */
#include "lpu/lpu_dram_map.h"
#include "lpu/lpu_ioctl.h"
#include "rt_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define DEV_NODE_DEFAULT "/dev/lpu0"
#define DEV_MAX_COHERENT_MAPS 64

typedef struct {
    void *va;
    uint64_t bus; /* card DRAM address (= LPU_HOST_ARENA_BASE + offset) */
    size_t size;  /* allocation size (for reuse / bounds checks) */
    bool freed;
} dev_map_t;

typedef struct {
    int fd;
    void *bar0;
    size_t bar0_size;
    void *bar1;
    size_t bar1_size;
    dev_map_t maps[DEV_MAX_COHERENT_MAPS];
    int nmaps;
    /* host arena: reserved region inside the BAR1 window (lpu_dram_map.h; bump + first-fit reuse)
     */
    uint8_t *arena_va;
    size_t bump;
} dev_priv_t;

static lpuRtError dev_init(lpu_backend_t *b) {
    dev_priv_t *p = calloc(1, sizeof(*p));
    if (!p)
        return LPU_RT_ERROR_OUT_OF_MEMORY;

    p->fd = open(DEV_NODE_DEFAULT, O_RDWR | O_CLOEXEC);
    if (p->fd < 0) {
        free(p);
        rt_set_last_error(LPU_RT_ERROR_IO);
        return LPU_RT_ERROR_IO;
    }
    rt_log_("dev_init: open %s fd=%d", DEV_NODE_DEFAULT, p->fd);

    struct lpu_ioctl_info info;
    if (ioctl(p->fd, LPU_IOCTL_GET_INFO, &info) != 0 || info.version != LPU_IOCTL_ABI_VERSION ||
        info.bar0_size == 0) {
        rt_log_("dev_init: GET_INFO failed (errno=%d ver=%#x bar0_size=%#llx)", errno, info.version,
                (unsigned long long)info.bar0_size);
        close(p->fd);
        free(p);
        rt_set_last_error(LPU_RT_ERROR_DEVICE_ERROR);
        return LPU_RT_ERROR_DEVICE_ERROR;
    }
    rt_log_("dev_init: GET_INFO ver=%#x bar0_size=%#llx bar1_size=%#llx dram_dev_base=%#llx",
            info.version, (unsigned long long)info.bar0_size, (unsigned long long)info.bar1_size,
            (unsigned long long)info.dram_dev_base);

    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    p->bar0_size = info.bar0_size;
    p->bar0 = mmap(NULL, p->bar0_size, PROT_READ | PROT_WRITE, MAP_SHARED, p->fd,
                   (off_t)LPU_MMAP_PGOFF_BAR0 * (off_t)page);
    if (p->bar0 == MAP_FAILED) {
        rt_log_("dev_init: BAR0 mmap %#llx failed", (unsigned long long)p->bar0_size);
        close(p->fd);
        free(p);
        rt_set_last_error(LPU_RT_ERROR_IO);
        return LPU_RT_ERROR_IO;
    }
    rt_log_("dev_init: BAR0 mmap %p size=%#llx", p->bar0, (unsigned long long)p->bar0_size);

    /* BAR1 = chip DRAM window; it must cover the host arena reserved region
       and leave pool space (rigs with a window smaller than 8G also work —
       the pool takes whatever remains) */
    if (info.dram_dev_base != LPU_DRAM_BASE || info.bar1_size == 0 ||
        info.bar1_size < (LPU_HOST_ARENA_BASE - LPU_DRAM_BASE) + LPU_HOST_ARENA_SIZE ||
        info.bar1_size <= (LPU_DRAM_POOL_BASE - LPU_DRAM_BASE)) {
        rt_log_("dev_init: BAR1 window check failed (dram_dev_base=%#llx bar1_size=%#llx)",
                (unsigned long long)info.dram_dev_base, (unsigned long long)info.bar1_size);
        munmap(p->bar0, p->bar0_size);
        close(p->fd);
        free(p);
        rt_set_last_error(LPU_RT_ERROR_DEVICE_ERROR);
        return LPU_RT_ERROR_DEVICE_ERROR;
    }
    p->bar1_size = info.bar1_size;
    p->bar1 = mmap(NULL, p->bar1_size, PROT_READ | PROT_WRITE, MAP_SHARED, p->fd,
                   (off_t)LPU_MMAP_PGOFF_BAR1 * (off_t)page);
    if (p->bar1 == MAP_FAILED) {
        rt_log_("dev_init: BAR1 mmap %#llx failed", (unsigned long long)p->bar1_size);
        munmap(p->bar0, p->bar0_size);
        close(p->fd);
        free(p);
        rt_set_last_error(LPU_RT_ERROR_IO);
        return LPU_RT_ERROR_IO;
    }
    rt_log_("dev_init: BAR1 mmap %p size=%#llx", p->bar1, (unsigned long long)p->bar1_size);

    b->regs = (volatile uint32_t *)((uint8_t *)p->bar0 + LPU_SCHED_REG_BASE);
    rt_log_("dev_init: regs=%p(SCHED_REG_BASE=%#x) dram=%p dev_base=%#llx size=%#llx arena=%p",
            (void *)b->regs, (unsigned)LPU_SCHED_REG_BASE,
            (void *)((uint8_t *)p->bar1 + (LPU_DRAM_POOL_BASE - LPU_DRAM_BASE)),
            (unsigned long long)LPU_DRAM_POOL_BASE,
            (unsigned long long)(p->bar1_size - (LPU_DRAM_POOL_BASE - LPU_DRAM_BASE)),
            (void *)p->arena_va);

    /* the runtime pool starts at LPU_DRAM_POOL_BASE (after the fw image /
       arena reserved regions); every later device address lands correctly
       via the dram_dev_base/dram conversion */
    b->dram = (uint8_t *)p->bar1 + (LPU_DRAM_POOL_BASE - LPU_DRAM_BASE);
    b->dram_dev_base = LPU_DRAM_POOL_BASE;
    b->dram_size = p->bar1_size - (LPU_DRAM_POOL_BASE - LPU_DRAM_BASE);

    /* the host arena (ring/CQ/shadow) is carved from the reserved region */
    p->arena_va = (uint8_t *)p->bar1 + (LPU_HOST_ARENA_BASE - LPU_DRAM_BASE);

    b->priv = p;
    return LPU_RT_SUCCESS;
}

static void dev_fini(lpu_backend_t *b) {
    dev_priv_t *p = b->priv;
    if (!p)
        return;
    /* arena allocations all live inside the BAR1 mapping; no separate mappings to tear down */
    if (p->bar1)
        munmap(p->bar1, p->bar1_size);
    if (p->bar0)
        munmap(p->bar0, p->bar0_size);
    close(p->fd);
    free(p);
    b->priv = NULL;
    b->regs = NULL;
    b->dram = NULL;
}

/* Carve a piece of the card DRAM reserved region for a "host coherent" shared
 * region (ring/CQ/shadow arrays).
 * The bus address = LPU_HOST_ARENA_BASE + offset — a card DRAM address that
 * ap fw reads/writes directly through the ep_if identity mapping. The arena
 * lives as long as the process; free only marks the record for reuse. */
static lpuRtError dev_host_alloc(lpu_backend_t *b, size_t size, size_t align, void **va,
                                 uint64_t *bus_addr) {
    dev_priv_t *p = b->priv;

    if (align == 0)
        align = 1;
    if ((align & (align - 1)) || size == 0)
        return LPU_RT_ERROR_INVALID_VALUE;

    /* first fit against freed records first (all current callers are 4096-aligned, so first-fit is
     * safe) */
    for (int i = 0; i < p->nmaps; i++) {
        if (p->maps[i].freed && p->maps[i].size >= size) {
            p->maps[i].freed = false;
            *va = p->maps[i].va;
            *bus_addr = p->maps[i].bus;
            rt_log_("host_alloc: reuse slot%d size=%zu -> va=%p bus=%#llx", i, size, *va,
                    (unsigned long long)*bus_addr);
            return LPU_RT_SUCCESS;
        }
    }

    if (p->nmaps >= DEV_MAX_COHERENT_MAPS)
        return LPU_RT_ERROR_OUT_OF_MEMORY;

    size_t off = (p->bump + align - 1) & ~(align - 1);
    if (size > LPU_HOST_ARENA_SIZE || off > LPU_HOST_ARENA_SIZE - size)
        return LPU_RT_ERROR_OUT_OF_MEMORY;

    p->maps[p->nmaps].va = p->arena_va + off;
    p->maps[p->nmaps].bus = LPU_HOST_ARENA_BASE + (uint64_t)off;
    p->maps[p->nmaps].size = size;
    p->maps[p->nmaps].freed = false;
    p->nmaps++;
    p->bump = off + size;

    *va = p->arena_va + off;
    *bus_addr = LPU_HOST_ARENA_BASE + (uint64_t)off;
    rt_log_("host_alloc: carve size=%zu align=%zu off=%#zx -> va=%p bus=%#llx (bump=%#zx)", size,
            align, off, *va, (unsigned long long)*bus_addr, p->bump);
    return LPU_RT_SUCCESS;
}

static void dev_host_free(lpu_backend_t *b, void *va, uint64_t bus_addr, size_t size) {
    dev_priv_t *p = b->priv;
    (void)bus_addr;
    (void)size; /* kept in the record; the arena never returns memory to the system */

    for (int i = 0; i < p->nmaps; i++) {
        if (p->maps[i].va == va && !p->maps[i].freed) {
            p->maps[i].freed = true;
            return;
        }
    }
}

static void dev_doorbell(lpu_backend_t *b, int stream, uint32_t wp) {
    /* WC register write (absolute index) */
    rt_log_("doorbell: stream=%d wp=%u", stream, wp);
    b->regs[LPU_REG_Q_WP((uint32_t)stream) / 4u] = wp;
}

static void dev_kick(lpu_backend_t *b) { (void)b; /* the real scheduler is self-driving */ }

static void dev_event_clear(lpu_backend_t *b, int ev) {
    rt_log_("event_clear: ev=%d", ev);
    b->regs[LPU_REG_EVT_HOST_CLEAR / 4u] = (uint32_t)ev;
}

static void dev_flush(lpu_backend_t *b) {
    /* drain WC posted writes: read IDENT back */
    (void)b->regs[LPU_REG_IDENT / 4u];
}

lpuRtError rt_backend_dev_register(lpu_backend_t *b) {
    static const lpu_backend_ops_t ops = {
        .name = "device",
        .init = dev_init,
        .fini = dev_fini,
        .host_alloc = dev_host_alloc,
        .host_free = dev_host_free,
        .doorbell = dev_doorbell,
        .kick = dev_kick,
        .event_clear = dev_event_clear,
        .flush = dev_flush,
    };
    b->ops = &ops;
    return LPU_RT_SUCCESS;
}
