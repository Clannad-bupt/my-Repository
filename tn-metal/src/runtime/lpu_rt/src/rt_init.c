/* rt_init.c — init/device management + register programming (backend-agnostic shared code) */
#include "lpu/lpu_ioctl.h"
#include "rt_internal.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static lpu_ctx_t *g_ctx;

lpu_ctx_t *rt_ctx(void) { return g_ctx; }
lpu_ctx_t *rt_ctx_checked(void) { return g_ctx ? g_ctx : NULL; }

uint64_t rt_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* Device-side DRAM base placeholder (★ keep in sync with the driver's
 * LPU_DRAM_DEV_BASE; to be provided by GET_INFO once the spec adds the
 * BAR1 window) */
#define RT_DRAM_DEV_BASE_DEFAULT 0x80000000ull

/* Per-stream ring depth env knob LPU_RT_RING_DEPTH (power of 2, default 1024).
 * Without a BAR mmap shared region, vfio-user does one socket round trip per
 * BAR1 access: memset of a 1024-deep ring (256KB) ≈ 32K round trips, seconds
 * inside a VM — use 256 (64KB) when running E2E in the VM. */
static uint32_t ring_depth_from_env(void) {
    const char *s = getenv("LPU_RT_RING_DEPTH");
    if (!s || !*s)
        return LPU_RING_DEPTH_DEF;
    char *end;
    unsigned long v = strtoul(s, &end, 0);
    if (*end != '\0' || v == 0 || (v & (v - 1)) || v > 65536u)
        return LPU_RING_DEPTH_DEF; /* invalid: silently fall back to the default (rt_ring locates
                                      slots by bitmask) */
    return (uint32_t)v;
}

static lpuRtError backend_create(lpu_backend_t *be, uint32_t flags) {
    const char *env = getenv("LPU_RT_BACKEND");

    memset(be, 0, sizeof(*be));

    if (env && strcmp(env, "sim") == 0)
        return rt_backend_sim_register(be);
    if (env && strcmp(env, "device") == 0)
        return rt_backend_dev_register(be);
    if (flags == LPU_RT_INIT_SIM)
        return rt_backend_sim_register(be);
    if (flags == LPU_RT_INIT_DEVICE)
        return rt_backend_dev_register(be);
    /* auto: use device if /dev/lpu0 exists, else sim */
    if (access("/dev/lpu0", O_RDWR) == 0)
        return rt_backend_dev_register(be);
    return rt_backend_sim_register(be);
}

/* Scheduler register programming + shared-region setup (identical for sim/device) */
static lpuRtError hw_bring_up(lpu_ctx_t *c) {
    lpuRtError e;
    uint32_t ident;

    c->cq_depth = LPU_CQ_DEPTH_DEF;
    c->ring_depth = ring_depth_from_env();
    rt_log_("bring_up: cq_depth=%u ring_depth=%u", c->cq_depth, c->ring_depth);

    ident = rt_reg_read(&c->be, LPU_REG_IDENT);
    rt_log_("bring_up: IDENT read = %#x (want %#x)", ident, LPU_IDENT_MAGIC);
    if (ident != LPU_IDENT_MAGIC) {
        /* the device must return the IDENT magic (sim matches the real device/EP model) */
        rt_set_last_error(LPU_RT_ERROR_DEVICE_ERROR);
        return LPU_RT_ERROR_DEVICE_ERROR;
    }

    /* disable scheduling during configuration */
    rt_reg_write(&c->be, LPU_REG_CTRL, 0);
    rt_log_("bring_up: CTRL <= 0 (config window)");

    /* host coherent shared regions */
    e = c->be.ops->host_alloc(&c->be, c->cq_depth * LPU_CQ_ENTRY_SIZE, 4096, (void **)&c->cq_va,
                              &c->cq_bus);
    if (e != LPU_RT_SUCCESS)
        return e;
    rt_log_("bring_up: CQ %u B -> va=%p bus=%#llx", c->cq_depth * LPU_CQ_ENTRY_SIZE, c->cq_va,
            (unsigned long long)c->cq_bus);
    rt_dmemset(c->cq_va, 0, c->cq_depth * LPU_CQ_ENTRY_SIZE);

    e = c->be.ops->host_alloc(&c->be, LPU_RP_SHADOW_BYTES, 4096, (void **)&c->rp_sh_va,
                              &c->rp_sh_bus);
    if (e != LPU_RT_SUCCESS)
        return e;
    rt_log_("bring_up: RP shadow %u B -> va=%p bus=%#llx", (unsigned)LPU_RP_SHADOW_BYTES,
            c->rp_sh_va, (unsigned long long)c->rp_sh_bus);
    rt_dmemset(c->rp_sh_va, 0, LPU_RP_SHADOW_BYTES);

    e = c->be.ops->host_alloc(&c->be, LPU_EVT_SHADOW_BYTES, 4096, (void **)&c->evt_sh_va,
                              &c->evt_sh_bus);
    if (e != LPU_RT_SUCCESS)
        return e;
    rt_log_("bring_up: EVT shadow %u B -> va=%p bus=%#llx", (unsigned)LPU_EVT_SHADOW_BYTES,
            c->evt_sh_va, (unsigned long long)c->evt_sh_bus);
    rt_dmemset(c->evt_sh_va, 0, LPU_EVT_SHADOW_BYTES);

    c->cq_cp = 0;

    /* CQ configuration (pure polling: CQ_INT_THD = 0, interrupts not enabled) */
    rt_reg_write(&c->be, LPU_REG_CQ_BASE_LO, (uint32_t)(c->cq_bus & 0xffffffffu));
    rt_reg_write(&c->be, LPU_REG_CQ_BASE_HI, (uint32_t)(c->cq_bus >> 32));
    rt_reg_write(&c->be, LPU_REG_CQ_DEPTH, c->cq_depth);
    rt_reg_write(&c->be, LPU_REG_CQ_CP, 0);
    rt_reg_write(&c->be, LPU_REG_CQ_INT_THD, 0);
    rt_reg_write(&c->be, LPU_REG_INTR_ENABLE, 0);
    rt_log_("bring_up: CQ regs base=%#llx depth=%u", (unsigned long long)c->cq_bus, c->cq_depth);

    /* shadow array base addresses */
    rt_reg_write(&c->be, LPU_REG_Q_RP_SHADOW_BASE, (uint32_t)(c->rp_sh_bus & 0xffffffffu));
    rt_reg_write(&c->be, LPU_REG_EVT_SHADOW_ADDR, (uint32_t)(c->evt_sh_bus & 0xffffffffu));
    rt_reg_write(&c->be, LPU_REG_EVT_CTRL, 1); /* event unit enable */
    rt_log_("bring_up: shadows RP base=%#llx EVT addr=%#llx, EVT_CTRL<=1",
            (unsigned long long)c->rp_sh_bus, (unsigned long long)c->evt_sh_bus);

    /* device DRAM pool (BAR1 window / sim fake DRAM) */
    e = rt_pool_init(&c->pool, c->be.dram, c->be.dram_dev_base, c->be.dram_size);
    if (e != LPU_RT_SUCCESS)
        return e;
    rt_log_("bring_up: pool init win=%p dev_base=%#llx size=%#llx", c->pool.win,
            (unsigned long long)c->pool.dev_base, (unsigned long long)c->pool.size);

    /* enable scheduling and all LPU cores */
    rt_reg_write(&c->be, LPU_REG_CTRL, LPU_CTRL_ENABLE | (0xffu << 8));
    c->be.ops->flush(&c->be);
    rt_log_("bring_up: CTRL <= ENABLE|0xff<<8 + flush: bring-up done");
    return LPU_RT_SUCCESS;
}

static void hw_tear_down(lpu_ctx_t *c) {
    rt_log_("tear_down: CTRL <= 0");
    rt_reg_write(&c->be, LPU_REG_CTRL, 0);
    rt_pool_fini(&c->pool);
    if (c->cq_va)
        c->be.ops->host_free(&c->be, c->cq_va, c->cq_bus, c->cq_depth * LPU_CQ_ENTRY_SIZE);
    if (c->rp_sh_va)
        c->be.ops->host_free(&c->be, c->rp_sh_va, c->rp_sh_bus, LPU_RP_SHADOW_BYTES);
    if (c->evt_sh_va)
        c->be.ops->host_free(&c->be, c->evt_sh_va, c->evt_sh_bus, LPU_EVT_SHADOW_BYTES);
}

LPU_RT_API lpuRtError lpuRtInit(uint32_t flags) {
    lpuRtError e;

    if (g_ctx)
        return LPU_RT_SUCCESS; /* idempotent */

    rt_log_("lpuRtInit(flags=%#x)", flags);

    lpu_ctx_t *c = calloc(1, sizeof(*c));
    if (!c)
        return LPU_RT_ERROR_OUT_OF_MEMORY;

    e = backend_create(&c->be, flags);
    if (e != LPU_RT_SUCCESS)
        goto fail;
    rt_log_("init: backend=%s", c->be.ops->name);

    e = c->be.ops->init(&c->be);
    if (e != LPU_RT_SUCCESS)
        goto fail;
    rt_log_("init: backend init done regs=%p dram=%p dev_base=%#llx size=%#llx", c->be.regs,
            c->be.dram, (unsigned long long)c->be.dram_dev_base,
            (unsigned long long)c->be.dram_size);

    /* DRAM base fallback (device provides it via GET_INFO; sim sets it internally) */
    if (c->be.dram_dev_base == 0)
        c->be.dram_dev_base = RT_DRAM_DEV_BASE_DEFAULT;

    e = hw_bring_up(c);
    if (e != LPU_RT_SUCCESS)
        goto fail;

    c->inited = true;
    c->cur_dev = 0;
    g_ctx = c;
    return LPU_RT_SUCCESS;

fail:
    if (c->be.ops)
        c->be.ops->fini(&c->be);
    free(c);
    rt_set_last_error(e);
    return e;
}

LPU_RT_API lpuRtError lpuRtShutdown(void) {
    lpu_ctx_t *c = g_ctx;
    if (!c)
        return LPU_RT_SUCCESS;

    rt_log_("lpuRtShutdown");

    /* streams/events are destroyed by the caller; anything not destroyed is cleaned up here */
    for (int i = 0; i < LPU_NUM_STREAMS; i++)
        if (c->streams[i])
            lpuRtDestroyStream(c->streams[i]);
    for (int i = 0; i < LPU_NUM_EVENTS; i++)
        if (c->events[i])
            lpuRtDestroyEvent(c->events[i]);

    hw_tear_down(c);
    c->be.ops->fini(&c->be);
    free(c->fw_exports); /* fw export table (lpuRtSetFirmwareElf) */
    g_ctx = NULL;
    free(c);
    return LPU_RT_SUCCESS;
}

LPU_RT_API lpuRtError lpuRtDeviceSync(void) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }

    for (int i = 0; i < LPU_NUM_STREAMS; i++) {
        if (!c->streams[i])
            continue;
        lpuRtError e = rt_stream_drain(c->streams[i]);
        if (e != LPU_RT_SUCCESS) {
            rt_set_last_error(e);
            return e;
        }
    }
    return LPU_RT_SUCCESS;
}

LPU_RT_API lpuRtError lpuRtGetDeviceCount(int *count) {
    if (!count)
        return LPU_RT_ERROR_INVALID_VALUE;
    if (!rt_ctx_checked()) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    *count = 1; /* v1: single device */
    return LPU_RT_SUCCESS;
}

LPU_RT_API lpuRtError lpuRtSetDevice(int ordinal) {
    if (!rt_ctx_checked()) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    if (ordinal != 0) {
        rt_set_last_error(LPU_RT_ERROR_INVALID_DEVICE);
        return LPU_RT_ERROR_INVALID_DEVICE;
    }
    rt_ctx()->cur_dev = ordinal;
    return LPU_RT_SUCCESS;
}

LPU_RT_API lpuRtError lpuRtGetDevice(int *ordinal) {
    if (!ordinal)
        return LPU_RT_ERROR_INVALID_VALUE;
    if (!rt_ctx_checked()) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    *ordinal = rt_ctx()->cur_dev;
    return LPU_RT_SUCCESS;
}

LPU_RT_API lpuRtError lpuRtGetDeviceProperties(lpuRtDeviceProperties *props) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    if (!props)
        return LPU_RT_ERROR_INVALID_VALUE;

    memset(props, 0, sizeof(*props));
    snprintf(props->name, sizeof(props->name), "lpu-%s", c->be.ops->name);
    props->numCores = LPU_NUM_CORES;
    props->numStreams = LPU_NUM_STREAMS;
    props->numEvents = LPU_NUM_EVENTS;
    props->totalDramBytes = c->be.dram_size;
    props->identVersion = rt_reg_read(&c->be, LPU_REG_IDENT);
    snprintf(props->backend, sizeof(props->backend), "%s", c->be.ops->name);
    return LPU_RT_SUCCESS;
}
