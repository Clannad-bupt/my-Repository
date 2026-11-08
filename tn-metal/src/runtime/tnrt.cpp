// Copyright (c) 2026 ThinkNative. All rights reserved.

#include "runtime/tnrt.h"

#include <new>
#include <string.h>

#include "lpu/lpu_regs.h"
#include "runtime/tnmsg.h"

#if TN_PSEUDO_CODE
#include <dlfcn.h>

#include <thread>
#include <vector>

struct tnrt_kernel {
    void *handle;
};
#endif

// placeholder until the driver-backed work queue lands; keeps stream
// handles non-null so downstream handle validation works
struct tnrt_stream {};

// ================================================
// global definitions
// ================================================

const char *tnrt_get_error_string(tn_status_t status) {
    switch (status) {
    case TN_SUCCESS:
        return "success";
    case TN_ERR_PARAM:
        return "invalid parameter";
    case TN_ERR_NODEV:
        return "device does not exist";
    case TN_ERR_NOMEM:
        return "resource allocation failed";
    case TN_ERR_STATE:
        return "invalid state";
    case TN_ERR_NOT_READY:
        return "async operation not yet complete";
    case TN_ERR_LOC:
        return "invalid layout/loc combination";
    case TN_ERR_LAYOUT:
        return "reserved layout value";
    case TN_ERR_ALIGN:
        return "HWALIGN h*w misaligned";
    case TN_ERR_BCAST:
        return "broadcast flag on a non-ZEROSTRIDE layout";
    case TN_ERR_BCAST_FULL:
        return "bcast 0xF ambiguous with CONSTANT";
    case TN_ERR_ADDR:
        return "nonzero URF address bits [47:32]";
    case TN_ERR_DTYPE:
        return "unsupported dtype";
    case TN_ERR_BCAST_EXT:
        return "invalid ZEROSTRIDE broadcast extent";
    default:
        return "unknown error";
    }
}

// ================================================
// device interfaces
// ================================================

int tnrt_dev_get_num(void) {
    // TODO: query the actual number of devices available
    return 1;
}

tn_status_t tnrt_dev_query(int dev_id) {
    (void)dev_id;
    return TN_SUCCESS;
}

tn_status_t tnrt_dev_request(int dev_id) {
    (void)dev_id;
    return TN_SUCCESS;
}

tn_status_t tnrt_dev_release(void) { return TN_SUCCESS; }

tn_status_t tnrt_dev_sync(void) { return TN_SUCCESS; }

// ================================================
// stream interfaces
// ================================================

tn_status_t tnrt_stream_create(tnrt_stream_t *stream) {
    if (stream == nullptr) {
        return TN_ERR_PARAM;
    }
    // TODO: real driver-backed work queue
    tnrt_stream_t instance = new (std::nothrow) tnrt_stream{};
    if (instance == nullptr) {
        return TN_ERR_NOMEM;
    }
    *stream = instance;
    return TN_SUCCESS;
}

tn_status_t tnrt_stream_destroy(tnrt_stream_t stream) {
    // TODO: release once the device has completed all work in the stream
    delete stream;
    return TN_SUCCESS;
}

tn_status_t tnrt_stream_sync(tnrt_stream_t stream) {
    (void)stream;
    return TN_SUCCESS;
}

tn_status_t tnrt_stream_wait_event(tnrt_stream_t stream, tnrt_event_t event) {
    (void)stream;
    (void)event;
    return TN_SUCCESS;
}

// ================================================
// event interfaces
// ================================================

tn_status_t tnrt_event_create(tnrt_event_t *event) {
    if (event == nullptr) {
        return TN_ERR_PARAM;
    }
    // TODO: real event object
    *event = nullptr;
    return TN_SUCCESS;
}

tn_status_t tnrt_event_destroy(tnrt_event_t event) {
    (void)event;
    return TN_SUCCESS;
}

tn_status_t tnrt_event_record(tnrt_event_t event, tnrt_stream_t stream) {
    (void)event;
    (void)stream;
    return TN_SUCCESS;
}

tn_status_t tnrt_event_query(tnrt_event_t event) {
    (void)event;
    return TN_SUCCESS;
}

tn_status_t tnrt_event_sync(tnrt_event_t event) {
    (void)event;
    return TN_SUCCESS;
}

// ================================================
// memory interfaces
// ================================================

tn_status_t tnrt_mem_alloc(tnrt_mem_t *ptr, uint64_t size) {
    if (ptr == nullptr) {
        return TN_ERR_PARAM;
    }
#if TN_PSEUDO_CODE
    *ptr = new (std::nothrow) uint8_t[size];
    if (*ptr == nullptr) {
        return TN_ERR_NOMEM;
    }
    return TN_SUCCESS;
#endif
    (void)size;
    // TODO: real device allocation
    *ptr = nullptr;
    return TN_SUCCESS;
}

tn_status_t tnrt_mem_free(tnrt_mem_t ptr) {
#if TN_PSEUDO_CODE
    delete[] static_cast<uint8_t *>(ptr);
    return TN_SUCCESS;
#endif
    (void)ptr;
    return TN_SUCCESS;
}

tn_status_t tnrt_tensor_alloc(tn_tensor_t *tensor, tn_dtype_t dtype, uint32_t *dims,
                              uint32_t num_dims) {
    if (tensor == nullptr) {
        return TN_ERR_PARAM;
    }
    *tensor = {};
    if (dims == nullptr || num_dims < 1 || num_dims > TN_MAX_DIMS) {
        return TN_ERR_PARAM;
    }
    if (tn_dtype_bits(dtype) == 0) {
        return TN_ERR_DTYPE; // reserved encoding; byte size is undefined
    }
    // reject zero extents and element-count overflow (the final
    // byte-size multiply by up to 4 must still fit in uint64)
    uint64_t numel = 1;
    for (uint32_t i = 0; i < num_dims; ++i) {
        if (dims[i] == 0 || (uint64_t)dims[i] > UINT64_MAX / numel) {
            return TN_ERR_PARAM;
        }
        numel *= (uint64_t)dims[i];
    }
    if (numel > UINT64_MAX / 4) {
        return TN_ERR_PARAM;
    }

#if TN_PSEUDO_CODE
    tnrt_mem_t ptr = nullptr;
    tn_status_t status = tnrt_mem_alloc(&ptr, tn_calc_bytes(dtype, dims, num_dims));
    if (status != TN_SUCCESS) {
        return status;
    }
    tensor->dtype = dtype;
    tensor->ndims = num_dims;
    for (uint32_t i = 0; i < TN_MAX_DIMS; ++i) {
        tensor->dims[i] = (i < num_dims) ? dims[i] : 1; // entries past ndims read as 1
    }
    // host-only pseudocode: the "device address" is the host allocation
    tensor->addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr));
    return TN_SUCCESS;
#else
    // TODO: real device allocation
    return TN_SUCCESS;
#endif
}

tn_status_t tnrt_tensor_alloc_4d(tn_tensor_t *tensor, tn_dtype_t dtype, uint32_t dim0,
                                 uint32_t dim1, uint32_t dim2, uint32_t dim3) {
    uint32_t dims[4] = {dim0, dim1, dim2, dim3};
    return tnrt_tensor_alloc(tensor, dtype, dims, 4);
}

tn_status_t tnrt_tensor_free(tn_tensor_t tensor) {
    // addr == 0 (default/empty tensor) is a no-op
    return tnrt_mem_free(reinterpret_cast<tnrt_mem_t>(static_cast<uintptr_t>(tensor.addr)));
}

tn_status_t tnrt_mem_get_info(uint64_t *free_bytes, uint64_t *total_bytes) {
    if (free_bytes == nullptr || total_bytes == nullptr) {
        return TN_ERR_PARAM;
    }
    // TODO: query the driver
    *free_bytes = 0;
    *total_bytes = 0;
    return TN_SUCCESS;
}

tn_status_t tnrt_mem_set(tnrt_mem_t dst, uint8_t value, uint64_t size) {
    (void)dst;
    (void)value;
    (void)size;
    return TN_SUCCESS;
}

tn_status_t tnrt_mem_s2d(tnrt_mem_t dst, const void *src, uint64_t size) {
#if TN_PSEUDO_CODE
    memcpy(dst, src, size);
    return TN_SUCCESS;
#endif
    (void)dst;
    (void)src;
    (void)size;
    return TN_SUCCESS;
}

tn_status_t tnrt_mem_d2s(void *dst, tnrt_mem_t src, uint64_t size) {
#if TN_PSEUDO_CODE
    memcpy(dst, src, size);
    return TN_SUCCESS;
#endif
    (void)dst;
    (void)src;
    (void)size;
    return TN_SUCCESS;
}

tn_status_t tnrt_mem_d2d(tnrt_mem_t dst, tnrt_mem_t src, uint64_t size) {
#if TN_PSEUDO_CODE
    memcpy(dst, src, size);
    return TN_SUCCESS;
#endif
    (void)dst;
    (void)src;
    (void)size;
    return TN_SUCCESS;
}

tn_status_t tnrt_mem_set_u8_async(tnrt_mem_t dst, uint8_t value, uint64_t count,
                                  tnrt_stream_t stream) {
#if TN_PSEUDO_CODE
    memset(dst, value, count);
    return TN_SUCCESS;
#endif
    (void)dst;
    (void)value;
    (void)count;
    (void)stream;
    return TN_SUCCESS;
}

tn_status_t tnrt_mem_set_u16_async(tnrt_mem_t dst, uint16_t value, uint64_t count,
                                   tnrt_stream_t stream) {
#if TN_PSEUDO_CODE
    memset(dst, value, count);
    return TN_SUCCESS;
#endif
    (void)dst;
    (void)value;
    (void)count;
    (void)stream;
    return TN_SUCCESS;
}

tn_status_t tnrt_mem_s2d_async(tnrt_mem_t dst, const void *src, uint64_t size,
                               tnrt_stream_t stream) {
#if TN_PSEUDO_CODE
    memcpy(dst, src, size);
    return TN_SUCCESS;
#endif
    (void)dst;
    (void)src;
    (void)size;
    (void)stream;
    return TN_SUCCESS;
}

tn_status_t tnrt_mem_d2s_async(void *dst, tnrt_mem_t src, uint64_t size, tnrt_stream_t stream) {
#if TN_PSEUDO_CODE
    memcpy(dst, src, size);
    return TN_SUCCESS;
#endif
    (void)dst;
    (void)src;
    (void)size;
    (void)stream;
    return TN_SUCCESS;
}

tn_status_t tnrt_mem_d2d_async(tnrt_mem_t dst, tnrt_mem_t src, uint64_t size,
                               tnrt_stream_t stream) {
#if TN_PSEUDO_CODE
    memcpy(dst, src, size);
    return TN_SUCCESS;
#endif
    (void)dst;
    (void)src;
    (void)size;
    (void)stream;
    return TN_SUCCESS;
}

// ================================================
// kernel module (scalar firmware) interfaces
// ================================================

tn_status_t tnrt_kernel_load_file(const char *path, tnrt_kernel_t *kernel) {
    if (kernel == nullptr) {
        return TN_ERR_PARAM;
    }
    *kernel = nullptr;
    if (path == nullptr || path[0] == '\0') {
        return TN_ERR_PARAM;
    }

#if TN_PSEUDO_CODE
    // Host-only pseudocode: emulate the scalar image with a local .so handle.
    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        return TN_ERR_PARAM;
    }

    tnrt_kernel_t instance = new (std::nothrow) tnrt_kernel{handle};
    if (instance == nullptr) {
        dlclose(handle);
        return TN_ERR_NOMEM;
    }
    *kernel = instance;
    return TN_SUCCESS;
#else
    // TODO: send the .so image to scalar through the driver. Scalar performs
    // relocation and registers the image's symbols; store its driver handle here.
    (void)path;
    return TN_ERR_STATE;
#endif
}

tn_status_t tnrt_kernel_unload(tnrt_kernel_t kernel, tnrt_stream_t stream) {
    if (kernel == nullptr) {
        return TN_ERR_PARAM;
    }
    (void)stream;
#if TN_PSEUDO_CODE
    dlclose(kernel->handle);
    delete kernel;
#else
    // TODO: enqueue a driver command to unload the image on scalar.
    (void)kernel;
#endif
    return TN_SUCCESS;
}

tn_status_t tnrt_kernel_launch(tnrt_kernel_t kernel, const char *func_name, const void *msg,
                               uint64_t size, uint32_t num_cores, tnrt_stream_t stream) {
    if (kernel == nullptr || func_name == nullptr || func_name[0] == '\0' || msg == nullptr ||
        num_cores == 0 || num_cores > LPU_NUM_CORES) {
        return TN_ERR_PARAM;
    }
    (void)stream;

#if TN_PSEUDO_CODE
    // Host-only pseudocode: emulate scalar symbol resolution and entry execution.
    dlerror();
    void *symbol = dlsym(kernel->handle, func_name);
    if (dlerror() != nullptr || symbol == nullptr) {
        return TN_ERR_PARAM;
    }
    // the image's core-identity hook: each emulated-core thread gets its
    // index programmed before the entry runs (one TLS slot per thread in
    // the image); on device the fw owns per-core identity instead
    dlerror();
    auto set_core_idx =
        reinterpret_cast<void (*)(uint32_t)>(dlsym(kernel->handle, TNMSG_HOOK_SET_CORE_IDX));
    if (dlerror() != nullptr || set_core_idx == nullptr) {
        return TN_ERR_PARAM;
    }

    using kernel_func_t = tn_status_t (*)(const void *, uint64_t);
    auto func = reinterpret_cast<kernel_func_t>(symbol);

    // SPMD multi-core emulation: one host thread per core; each thread
    // programs its own core index and runs the same entry, so the image
    // reads back this thread's index as core_idx
    std::vector<tn_status_t> results(num_cores, TN_SUCCESS);
    std::vector<std::thread> cores;
    cores.reserve(num_cores);
    uint32_t launched = 0;
    for (; launched < num_cores; ++launched) {
        try {
            cores.emplace_back([&, launched] {
                set_core_idx(launched);
                results[launched] = func(msg, size);
            });
        } catch (...) {
            break; // thread creation failed; join what started, report NOMEM
        }
    }
    for (auto &core : cores) {
        core.join();
    }
    if (launched != num_cores) {
        return TN_ERR_NOMEM;
    }
    // deterministic error reporting: lowest-indexed failing core wins
    for (uint32_t i = 0; i < num_cores; ++i) {
        if (results[i] != TN_SUCCESS) {
            return results[i];
        }
    }
    return TN_SUCCESS;
#else
    // TODO: pass the kernel handle, func_name, msg and num_cores to the
    // driver; the driver programs the launch width from num_cores. Scalar
    // resolves func_name to the entry address in this image and executes
    // it, consuming the num_cores copy in the graph/op payload
    // (runtime/tnmsg.h).
    (void)msg;
    (void)size;
    return TN_SUCCESS;
#endif
}
