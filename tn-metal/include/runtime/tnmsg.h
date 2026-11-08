// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: include/runtime/tnmsg.h
 *
 * Launch-message ABI — the common message envelope for every
 * tnrt_kernel_launch(), plus the type-specific payloads
 * (docs/tndnn-execution.md §6, docs/tndnn-graph-format.md §11). Shared by
 * the host runtime (message assembly), the driver (transport) and the
 * scalar library (parsing, graph walk and opcode dispatch).
 *
 *   launch msg
 *     ├─ launch header          16 bytes (magic / type / msg_size)
 *     └─ payload by msg_type
 *          ├─ GRAPH: graph_addr + num_cores       (docs §11)
 *          └─ OP:    op header   36 bytes (incl. num_cores + split)
 *                   tensor descriptors  40 bytes × tensor_count
 *                   parameter payload   param_size bytes, 8-byte aligned
 *
 *   device graph (the GRAPH payload's graph_addr target)
 *     ├─ graph header           16 bytes (TNDG / header_size /
 *     │                                    node_count / nodes_offset)
 *     └─ node records           node_count complete op launch messages,
 *                               8-byte aligned; the walk advances by
 *                               align8(each node's msg_size)
 *
 * The launch header is pure transport: neither a version nor execution
 * properties live in the envelope. The launch width (num_cores) is an
 * execution property and lives in each payload for the scalar side; it
 * is also passed explicitly to tnrt_kernel_launch() for the driver —
 * the runtime does not cross-check the two copies, so consistency is
 * the caller's responsibility (tndnn fills both from the same
 * create-time binding). A graph node record is byte-identical to the
 * corresponding operator launch message: graph-mode recording and
 * operator-mode launching share one serialization path
 * (docs/tndnn-graph-format.md §7).
 *
 * All integers are fixed-width little-endian (host and device are both
 * LE); fields are accessed through the explicit read/write helpers below
 * at the documented offsets — the C structs here are host-side views only
 * and are never memcpy'd onto the wire as a whole. Reserved bytes and
 * padding are written as 0 and ignored on parse (no byte scanning on the
 * scalar side); only the launch header's single-word reserved field is
 * validated to be 0.
 *
 * Dependency: tndef.h + ops/params.h; no kernel-layer headers.
 * ========================================================================== */
#pragma once

#include <stdint.h>
#include <string.h>

#include "ops/params.h"
#include "tndef.h"

#ifdef __cplusplus
extern "C" {
#endif

// scalar entry symbols consuming launch messages
#define TNMSG_ENTRY_GRAPH "tn_scalar_run_graph"
#define TNMSG_ENTRY_OP "tn_scalar_run_operator"
// TN_PSEUDO_CODE only: per-thread core-identity hook; tnrt_kernel_launch
// spawns one thread per core and calls it with the thread's index before
// the entry runs. Device images have no such hook — there the core
// identity comes from the fw (lpu_fw_core_id)
#define TNMSG_HOOK_SET_CORE_IDX "tn_scalar_set_core_idx"

#define TNMSG_MAGIC "TNRT" // 4 bytes, no NUL terminator

// payload discriminator in the launch header, exactly one byte both on
// the wire and in host structures: C11 cannot fix an enum's underlying
// type, so the type is a uint8_t typedef and the values are plain
// enum constants (int, implicitly converted)
typedef uint8_t tnmsg_type_t;
enum {
    TNMSG_TYPE_GRAPH = 0, // payload: device graph address + launch width
    TNMSG_TYPE_OP = 1,    // payload: one operator invocation
};

// launch header (16 bytes); neither it nor the op payload carries a
// version — op-side evolution appends opcodes or redefines the reserved
// fields; device-graph evolution changes the graph magic below
#define TNMSG_HDR_MAGIC 0      // uint8_t[4]
#define TNMSG_HDR_TYPE 4       // uint8_t, tnmsg_type_t
#define TNMSG_HDR_RESERVED0 5  // uint8_t[3], 0
#define TNMSG_HDR_MSG_SIZE 8   // uint32_t, whole launch message
#define TNMSG_HDR_RESERVED1 12 // uint32_t, 0
#define TNMSG_HEADER_SIZE 16

// graph payload (12 bytes, right after the launch header)
#define TNMSG_GRAPH_ADDR 16      // uint64_t, build-time device graph address
#define TNMSG_GRAPH_NUM_CORES 24 // uint32_t, launch width, > 0
#define TNMSG_GRAPH_MSG_SIZE 28

// device graph (docs/tndnn-graph-format.md §5-§7): a 16-byte header plus
// node_count node records, each a complete op launch message (TNRT header
// + op payload); node records are 8-byte aligned — the byte range of one
// node is its launch-header msg_size rounded up to 8
#define TNMSG_GRAPH_MAGIC "TNDG" // 4 bytes, no NUL terminator

// device graph header (16 bytes)
#define TNMSG_GHDR_MAGIC 0         // uint8_t[4]
#define TNMSG_GHDR_HEADER_SIZE 4   // uint32_t, = TNMSG_GHDR_SIZE
#define TNMSG_GHDR_NODE_COUNT 8    // uint32_t
#define TNMSG_GHDR_NODES_OFFSET 12 // uint32_t, first node record; v1 = 16
#define TNMSG_GHDR_SIZE 16

// op payload header (36 bytes, right after the launch header)
#define TNMSG_OP_HDR_NUM_CORES 16      // uint32_t, launch width, > 0
#define TNMSG_OP_HDR_OPCODE 20         // uint32_t, tnop_opcode_t
#define TNMSG_OP_HDR_TENSOR_MASK 24    // uint32_t, bit i = operand slot i present
#define TNMSG_OP_HDR_TENSOR_COUNT 28   // uint32_t
#define TNMSG_OP_HDR_TENSORS_OFFSET 32 // uint32_t, absolute; v1 = 52
#define TNMSG_OP_HDR_PARAM_OFFSET 36   // uint32_t, absolute; 0 = no parameter
#define TNMSG_OP_HDR_PARAM_SIZE 40     // uint32_t, 0 = no parameter
#define TNMSG_OP_HDR_SPLIT                                                                         \
    44 // tnop_split_t (8 bytes): host-computed core-0
       // work share, consumed as-is by the scalar side
#define TNMSG_OP_HEADER_SIZE 36
#define TNMSG_OP_TENSORS_OFFSET 52 // descriptor array start (absolute)
#define TNMSG_TENSOR_DESC_SIZE 40
#define TNMSG_MAX_OP_TENSORS 16 // protocol cap; each opcode pins an exact count

// tensor descriptor field offsets within a 40-byte entry
#define TNMSG_TENSOR_ADDR 0   // uint64_t device address
#define TNMSG_TENSOR_DTYPE 8  // uint32_t, tn_dtype_t
#define TNMSG_TENSOR_NDIMS 12 // uint32_t
#define TNMSG_TENSOR_DIMS 16  // uint32_t[TN_MAX_DIMS]

// host-side view of the launch header; magic and reserved fields are
// checked against the constants above and are not carried
typedef struct tnmsg_header {
    tnmsg_type_t type;
    uint32_t msg_size;
} tnmsg_header_t;

// host-side view of the graph payload
typedef struct tnmsg_graph_payload {
    uint64_t graph_addr;
    uint32_t num_cores;
} tnmsg_graph_payload_t;

// host-side view of the device graph header; magic and header_size are
// checked against the constants above and are not carried
typedef struct tnmsg_graph_header {
    uint32_t node_count;
    uint32_t nodes_offset;
} tnmsg_graph_header_t;

// host-side view of the op payload header
typedef struct tnmsg_op_header {
    uint32_t num_cores;
    uint32_t opcode;
    uint32_t tensor_mask;
    uint32_t tensor_count;
    uint32_t tensors_offset;
    uint32_t param_offset;
    uint32_t param_size;
    tnop_split_t split;
} tnmsg_op_header_t;

static inline void tnmsg_store(uint8_t *dst, uint32_t offset, const void *src, uint32_t n) {
    memcpy(dst + offset, src, n);
}

static inline void tnmsg_load(const uint8_t *src, uint32_t offset, void *dst, uint32_t n) {
    memcpy(dst, src + offset, n);
}

// serialize the 16-byte launch header; every byte is written (reserved = 0)
static inline void tnmsg_write_header(uint8_t *dst, const tnmsg_header_t *hdr) {
    memset(dst, 0, TNMSG_HEADER_SIZE);
    tnmsg_store(dst, TNMSG_HDR_MAGIC, TNMSG_MAGIC, 4);
    tnmsg_store(dst, TNMSG_HDR_TYPE, &hdr->type, 1);
    tnmsg_store(dst, TNMSG_HDR_MSG_SIZE, &hdr->msg_size, 4);
}

// parse the 16-byte launch header; field extraction only, validation is
// the caller's job
static inline void tnmsg_read_header(const uint8_t *src, tnmsg_header_t *hdr) {
    tnmsg_load(src, TNMSG_HDR_TYPE, &hdr->type, 1);
    tnmsg_load(src, TNMSG_HDR_MSG_SIZE, &hdr->msg_size, 4);
}

// graph payload accessors
static inline void tnmsg_write_graph_payload(uint8_t *msg, const tnmsg_graph_payload_t *payload) {
    tnmsg_store(msg, TNMSG_GRAPH_ADDR, &payload->graph_addr, 8);
    tnmsg_store(msg, TNMSG_GRAPH_NUM_CORES, &payload->num_cores, 4);
}

static inline void tnmsg_read_graph_payload(const uint8_t *msg, tnmsg_graph_payload_t *payload) {
    tnmsg_load(msg, TNMSG_GRAPH_ADDR, &payload->graph_addr, 8);
    tnmsg_load(msg, TNMSG_GRAPH_NUM_CORES, &payload->num_cores, 4);
}

// serialize the 16-byte device graph header; every byte is written
static inline void tnmsg_write_graph_header(uint8_t *dst, const tnmsg_graph_header_t *gh) {
    memset(dst, 0, TNMSG_GHDR_SIZE);
    tnmsg_store(dst, TNMSG_GHDR_MAGIC, TNMSG_GRAPH_MAGIC, 4);
    const uint32_t header_size = TNMSG_GHDR_SIZE;
    tnmsg_store(dst, TNMSG_GHDR_HEADER_SIZE, &header_size, 4);
    tnmsg_store(dst, TNMSG_GHDR_NODE_COUNT, &gh->node_count, 4);
    tnmsg_store(dst, TNMSG_GHDR_NODES_OFFSET, &gh->nodes_offset, 4);
}

// parse the 16-byte device graph header; field extraction only
static inline void tnmsg_read_graph_header(const uint8_t *src, tnmsg_graph_header_t *gh) {
    tnmsg_load(src, TNMSG_GHDR_NODE_COUNT, &gh->node_count, 4);
    tnmsg_load(src, TNMSG_GHDR_NODES_OFFSET, &gh->nodes_offset, 4);
}

// serialize the 36-byte op payload header; every byte is written
static inline void tnmsg_write_op_header(uint8_t *msg, const tnmsg_op_header_t *op) {
    memset(msg + TNMSG_HEADER_SIZE, 0, TNMSG_OP_HEADER_SIZE);
    tnmsg_store(msg, TNMSG_OP_HDR_NUM_CORES, &op->num_cores, 4);
    tnmsg_store(msg, TNMSG_OP_HDR_OPCODE, &op->opcode, 4);
    tnmsg_store(msg, TNMSG_OP_HDR_TENSOR_MASK, &op->tensor_mask, 4);
    tnmsg_store(msg, TNMSG_OP_HDR_TENSOR_COUNT, &op->tensor_count, 4);
    tnmsg_store(msg, TNMSG_OP_HDR_TENSORS_OFFSET, &op->tensors_offset, 4);
    tnmsg_store(msg, TNMSG_OP_HDR_PARAM_OFFSET, &op->param_offset, 4);
    tnmsg_store(msg, TNMSG_OP_HDR_PARAM_SIZE, &op->param_size, 4);
    tnmsg_store(msg, TNMSG_OP_HDR_SPLIT, &op->split, sizeof(tnop_split_t));
}

// parse the 36-byte op payload header; field extraction only
static inline void tnmsg_read_op_header(const uint8_t *msg, tnmsg_op_header_t *op) {
    tnmsg_load(msg, TNMSG_OP_HDR_NUM_CORES, &op->num_cores, 4);
    tnmsg_load(msg, TNMSG_OP_HDR_OPCODE, &op->opcode, 4);
    tnmsg_load(msg, TNMSG_OP_HDR_TENSOR_MASK, &op->tensor_mask, 4);
    tnmsg_load(msg, TNMSG_OP_HDR_TENSOR_COUNT, &op->tensor_count, 4);
    tnmsg_load(msg, TNMSG_OP_HDR_TENSORS_OFFSET, &op->tensors_offset, 4);
    tnmsg_load(msg, TNMSG_OP_HDR_PARAM_OFFSET, &op->param_offset, 4);
    tnmsg_load(msg, TNMSG_OP_HDR_PARAM_SIZE, &op->param_size, 4);
    tnmsg_load(msg, TNMSG_OP_HDR_SPLIT, &op->split, sizeof(tnop_split_t));
}

// serialize one tensor descriptor; t == NULL writes an all-zero entry (an
// absent optional slot)
static inline void tnmsg_write_tensor(uint8_t *dst, const tn_tensor_t *t) {
    memset(dst, 0, TNMSG_TENSOR_DESC_SIZE);
    if (t == NULL) {
        return;
    }
    const uint32_t dtype = (uint32_t)t->dtype;
    tnmsg_store(dst, TNMSG_TENSOR_ADDR, &t->addr, 8);
    tnmsg_store(dst, TNMSG_TENSOR_DTYPE, &dtype, 4);
    tnmsg_store(dst, TNMSG_TENSOR_NDIMS, &t->ndims, 4);
    tnmsg_store(dst, TNMSG_TENSOR_DIMS, t->dims, 4 * TN_MAX_DIMS);
}

// decode one tensor descriptor
static inline void tnmsg_read_tensor(const uint8_t *src, tn_tensor_t *t) {
    uint32_t dtype = 0;
    tnmsg_load(src, TNMSG_TENSOR_ADDR, &t->addr, 8);
    tnmsg_load(src, TNMSG_TENSOR_DTYPE, &dtype, 4);
    tnmsg_load(src, TNMSG_TENSOR_NDIMS, &t->ndims, 4);
    tnmsg_load(src, TNMSG_TENSOR_DIMS, t->dims, 4 * TN_MAX_DIMS);
    t->dtype = (tn_dtype_t)dtype;
}

#ifdef __cplusplus
}
#endif
