// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: include/runtime/tnrt.h
 *
 * Host runtime — the SDK's external interface: device / stream / event /
 * device-memory / kernel-module (scalar firmware) management, on top of
 * the driver.
 *
 * Execution model:
 *   - Single device per process (v1): tnrt_dev_request() binds the process
 *     to one device; every subsequent call operates on it implicitly.
 *     Multi-device is reserved (see TODO in the device section) and will
 *     introduce a device handle — do not rely on the implicit binding.
 *   - Calls taking a stream enqueue work asynchronously, ordered within
 *     that stream; calls without a stream parameter are synchronous.
 *   - Events mark a point in a stream: cross-stream dependencies via
 *     tnrt_stream_wait_event(), host synchronization via tnrt_event_sync().
 *
 * Handle conventions:
 *   - tnrt_stream_t / tnrt_event_t / tnrt_kernel_t are opaque
 *     runtime objects, created/destroyed in pairs; on create failure the
 *     out-handle is set to NULL.
 *   - tnrt_mem_t is a device address — never dereference it on the host.
 *
 * Threading: the current device is process-global. A handle may be used
 * from any thread, but concurrent use of the same handle requires external
 * synchronization.
 *
 * Dependency: tndef.h only. The runtime ABI must not expose kernel-layer
 * headers (chip.h and below belong to the device-side stack).
 * ========================================================================== */
#pragma once

#include <stdint.h>

#include "tndef.h"

#ifdef __cplusplus
extern "C" {
#endif

// ================================================
// global definitions
// ================================================

// opaque runtime objects
typedef struct tnrt_stream *tnrt_stream_t;
typedef struct tnrt_event *tnrt_event_t;
typedef struct tnrt_kernel *tnrt_kernel_t; // loaded module image

// device memory address (not a host pointer)
typedef void *tnrt_mem_t;

// human-readable description of a status code; never NULL
const char *tnrt_get_error_string(tn_status_t status);

// ================================================
// device interfaces
// ================================================

// number of available devices (>= 0)
int tnrt_dev_get_num(void);

// check whether dev_id exists: TN_SUCCESS / TN_ERR_NODEV
tn_status_t tnrt_dev_query(int dev_id);

// request dev_id for this process; all subsequent runtime calls bind to it
// implicitly (TN_ERR_STATE if a device is already requested)
tn_status_t tnrt_dev_request(int dev_id);

// release the current device; all runtime objects (streams / events /
// memory / kernels) must be destroyed before this call
tn_status_t tnrt_dev_release(void);

// block until all work on the current device completes
tn_status_t tnrt_dev_sync(void);

// TODO: multi-device operations

// ================================================
// stream interfaces
// ================================================

// create an ordered work queue
tn_status_t tnrt_stream_create(tnrt_stream_t *stream);

// destroy a stream; in-flight work keeps running and its resources are
// released once the device has completed all work in the stream
tn_status_t tnrt_stream_destroy(tnrt_stream_t stream);

// block until all work in the stream completes
tn_status_t tnrt_stream_sync(tnrt_stream_t stream);

// stream waits for an event, asynchronously
tn_status_t tnrt_stream_wait_event(tnrt_stream_t stream, tnrt_event_t event);

// ================================================
// event interfaces
// ================================================

tn_status_t tnrt_event_create(tnrt_event_t *event);

// destroy an event; destruction is deferred until the event has completed
tn_status_t tnrt_event_destroy(tnrt_event_t event);

// record an event on a stream; it completes when all preceding work in the
// stream completes
tn_status_t tnrt_event_record(tnrt_event_t event, tnrt_stream_t stream);

// TN_SUCCESS if the event has completed, TN_ERR_NOT_READY if still pending
tn_status_t tnrt_event_query(tnrt_event_t event);

// block until the event completes
tn_status_t tnrt_event_sync(tnrt_event_t event);

// ================================================
// memory interfaces
// ================================================

// allocate size bytes of device memory, aligned to the chip's TDR
// base-address (HWALIGN) granularity
tn_status_t tnrt_mem_alloc(tnrt_mem_t *ptr, uint64_t size);

// free device memory
tn_status_t tnrt_mem_free(tnrt_mem_t ptr);

// allocate a dense contiguous tensor on the device and fill the descriptor;
// dims[0] is the innermost dim, entries past num_dims are stored as 1;
// on failure *tensor is zeroed (addr == 0)
tn_status_t tnrt_tensor_alloc(tn_tensor_t *tensor, tn_dtype_t dtype, uint32_t *dims,
                              uint32_t num_dims);

// 4D convenience wrapper over tnrt_tensor_alloc; dim0 is the innermost dim
tn_status_t tnrt_tensor_alloc_4d(tn_tensor_t *tensor, tn_dtype_t dtype, uint32_t dim0,
                                 uint32_t dim1, uint32_t dim2, uint32_t dim3);

// free the tensor's device memory; addr == 0 (default/empty tensor) is a no-op
tn_status_t tnrt_tensor_free(tn_tensor_t tensor);

// query device memory usage
tn_status_t tnrt_mem_get_info(uint64_t *free_bytes, uint64_t *total_bytes);

// synchronous variants: block the host until the operation completes

// set device memory to a byte value
tn_status_t tnrt_mem_set(tnrt_mem_t dst, uint8_t value, uint64_t size);

// copy memory from system to device
tn_status_t tnrt_mem_s2d(tnrt_mem_t dst, const void *src, uint64_t size);

// copy memory from device to system
tn_status_t tnrt_mem_d2s(void *dst, tnrt_mem_t src, uint64_t size);

// copy memory from device to device
tn_status_t tnrt_mem_d2d(tnrt_mem_t dst, tnrt_mem_t src, uint64_t size);

// async variants: enqueued on a stream, ordered with respect to other work
// in that stream; host buffers (s2d src / d2s dst) must remain valid until
// the operation completes

tn_status_t tnrt_mem_set_u8_async(tnrt_mem_t dst, uint8_t value, uint64_t count,
                                  tnrt_stream_t stream);
tn_status_t tnrt_mem_set_u16_async(tnrt_mem_t dst, uint16_t value, uint64_t count,
                                   tnrt_stream_t stream);
tn_status_t tnrt_mem_s2d_async(tnrt_mem_t dst, const void *src, uint64_t size,
                               tnrt_stream_t stream);
tn_status_t tnrt_mem_d2s_async(void *dst, tnrt_mem_t src, uint64_t size, tnrt_stream_t stream);
tn_status_t tnrt_mem_d2d_async(tnrt_mem_t dst, tnrt_mem_t src, uint64_t size, tnrt_stream_t stream);

// ================================================
// kernel module (scalar firmware) interfaces
// ================================================

// synchronously send a shared object (.so) to scalar through the driver;
// scalar loads the image and registers its symbols; on failure *kernel is NULL
tn_status_t tnrt_kernel_load_file(const char *path, tnrt_kernel_t *kernel);

// enqueue an unload on a stream: the handle becomes invalid immediately and
// the scalar image is released once the stream passes this point
tn_status_t tnrt_kernel_unload(tnrt_kernel_t kernel, tnrt_stream_t stream);

// enqueue a launch; the msg starts with the common launch header
// (runtime/tnmsg.h: magic, graph/op type, msg_size) followed by the
// type-specific payload, which carries its own num_cores copy for
// scalar; num_cores is the launch width passed to the driver and must
// be non-zero (TN_ERR_PARAM otherwise) — the runtime does not parse the
// msg, so keeping the two copies consistent is the caller's
// responsibility; scalar resolves func_name in the loaded image to its
// entry address and executes it; the msg block is copied before return,
// so the caller may reuse it immediately
tn_status_t tnrt_kernel_launch(tnrt_kernel_t kernel, const char *func_name, const void *msg,
                               uint64_t size, uint32_t num_cores, tnrt_stream_t stream);

#ifdef __cplusplus
}
#endif
