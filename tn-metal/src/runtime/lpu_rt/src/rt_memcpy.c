/* rt_memcpy.c — H2D/D2H copies + device-memory fill / device-internal copy (synchronous)
 *
 * v1 conservative semantics (noted in lpu_rt.h):
 *  - stream == NULL → executes synchronously right away (direct BAR1 window copy)
 *  - with a stream → the stream is drained first, then a direct copy (correct
 *    ordering, no async benefit yet)
 * The true COPY-descriptor DMA path lands once spec §11-2 adds the H2D/D2H
 * encodings: then H2D/D2H become COPY descriptors on the ring, completing
 * asynchronously in stream order (the encodings are already reserved in
 * lpu_desc.h as ★ ASSUMED values: LPU_COPY_H2D/D2H + CQ COPY_DONE).
 * Memset/DtoD likewise use the direct-window synchronous version; their true
 * async versions follow the same path.
 */
#include "rt_internal.h"

#include <string.h>

static lpuRtError memcpy_common(lpu_ctx_t *c, lpuRtDevicePtr dev, void *host, size_t bytes,
                                bool to_device, lpuRtStream stream) {
    if (bytes == 0)
        return LPU_RT_SUCCESS;
    if (!host)
        return LPU_RT_ERROR_INVALID_VALUE;
    if (dev < c->pool.dev_base || dev >= c->pool.dev_base + c->pool.size ||
        bytes > c->pool.dev_base + c->pool.size - dev)
        return LPU_RT_ERROR_INVALID_VALUE;

    if (stream) {
        lpuRtError e = rt_stream_drain(stream);
        if (e != LPU_RT_SUCCESS)
            return e;
    }

    uint8_t *dram = rt_dram_ptr(&c->be, dev);
    if (to_device) {
        rt_dmemcpy(
            dram, host,
            bytes); /* narrow accesses on the device window (KVM MMIO emulation has no AVX) */
        c->be.ops->flush(&c->be); /* flush WC posted writes so the device sees them */
    } else {
        rt_dmemcpy(host, dram, bytes);
    }
    return LPU_RT_SUCCESS;
}

LPU_RT_API lpuRtError lpuRtMemcpyHtoD(lpuRtDevicePtr dst, const void *src, size_t bytes,
                                      lpuRtStream stream) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    return memcpy_common(c, dst, (void *)(uintptr_t)src, bytes, true, stream);
}

LPU_RT_API lpuRtError lpuRtMemcpyDtoH(void *dst, lpuRtDevicePtr src, size_t bytes,
                                      lpuRtStream stream) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    return memcpy_common(c, src, dst, bytes, false, stream);
}

/* device-memory range check (inside the window, no overflow) */
static bool dev_range_ok(lpu_ctx_t *c, lpuRtDevicePtr dev, uint64_t bytes) {
    return dev >= c->pool.dev_base && dev < c->pool.dev_base + c->pool.size &&
           bytes <= c->pool.dev_base + c->pool.size - dev;
}

LPU_RT_API lpuRtError lpuRtMemset(lpuRtDevicePtr dst, uint8_t value, size_t bytes) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    if (bytes == 0)
        return LPU_RT_SUCCESS;
    if (!dev_range_ok(c, dst, bytes))
        return LPU_RT_ERROR_INVALID_VALUE;

    rt_dmemset(rt_dram_ptr(&c->be, dst), value, bytes);
    c->be.ops->flush(&c->be); /* flush WC posted writes so the device sees them */
    return LPU_RT_SUCCESS;
}

LPU_RT_API lpuRtError lpuRtMemcpyDtoD(lpuRtDevicePtr dst, lpuRtDevicePtr src, size_t bytes) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    if (bytes == 0)
        return LPU_RT_SUCCESS;
    if (!dev_range_ok(c, src, bytes) || !dev_range_ok(c, dst, bytes))
        return LPU_RT_ERROR_INVALID_VALUE;

    /* device-internal copy: src/dst may overlap → narrow-access memmove (back-to-front when dst >
     * src) */
    rt_dmemmove(rt_dram_ptr(&c->be, dst), rt_dram_ptr(&c->be, src), bytes);
    c->be.ops->flush(&c->be);
    return LPU_RT_SUCCESS;
}
