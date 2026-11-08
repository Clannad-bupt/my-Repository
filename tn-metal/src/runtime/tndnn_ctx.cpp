// Copyright (c) 2026 ThinkNative. All rights reserved.

#include "tndnn_ctx.h"

#include <string.h>

#include "ops/ops.h"
#include "runtime/tnmsg.h"

namespace {

// largest op launch message: launch header + op header + the maximum
// descriptor array + the largest v1 payload (rope/conv2d, 36 bytes padded
// to 40) plus 8 bytes of headroom
constexpr uint32_t kMaxOpMsgSize =
    TNMSG_OP_TENSORS_OFFSET + TNMSG_MAX_OP_TENSORS * TNMSG_TENSOR_DESC_SIZE + 48;

// serialize one op launch message (TNRT header + op payload: opcode,
// operand descriptors, parameter payload and the host-computed core-0
// work split) into dst, which must hold kMaxOpMsgSize bytes. Shared by
// operator mode (immediate launch) and graph mode (node record), so a
// graph node is byte-identical to the corresponding operator launch
// (docs/tndnn-execution.md §6, docs/tndnn-graph-format.md §7).
tn_status_t build_op_msg(uint8_t *dst, uint32_t *msg_size, uint32_t num_cores, tnop_opcode_t opcode,
                         const tn_tensor_t *const *tensors, uint32_t tensor_count,
                         const void *param, uint32_t param_size) {
    // the ops module's host path computes the core-0 (worst-case) work
    // split for this launch; the scalar side consumes it as-is
    tnop_split_t split;
    const tn_status_t split_st =
        tnop_host_split(opcode, tensors, tensor_count, param, num_cores, &split);
    if (split_st != TN_SUCCESS) {
        return split_st;
    }
    const uint32_t param_padded = (param_size + 7u) & ~7u;
    const uint32_t tensors_size = tensor_count * TNMSG_TENSOR_DESC_SIZE;
    // the parameter payload stays 8-byte aligned after the descriptor
    // array (docs/tndnn-execution.md §6.1); the gap is zero padding
    const uint32_t param_offset = (TNMSG_OP_TENSORS_OFFSET + tensors_size + 7u) & ~7u;
    const uint32_t size =
        param_size != 0 ? param_offset + param_padded : TNMSG_OP_TENSORS_OFFSET + tensors_size;
    if (size > kMaxOpMsgSize) {
        return TN_ERR_PARAM;
    }
    memset(dst, 0, size);
    uint32_t tensor_mask = 0;
    for (uint32_t i = 0; i < tensor_count; ++i) {
        if (tensors[i] != nullptr) {
            tensor_mask |= 1u << i;
        }
        tnmsg_write_tensor(dst + TNMSG_OP_TENSORS_OFFSET + i * TNMSG_TENSOR_DESC_SIZE, tensors[i]);
    }
    if (param_size != 0) {
        memcpy(dst + param_offset, param, param_size);
    }
    const tnmsg_header_t hdr = {TNMSG_TYPE_OP, size};
    tnmsg_write_header(dst, &hdr);
    const tnmsg_op_header_t op = {num_cores,
                                  opcode,
                                  tensor_mask,
                                  tensor_count,
                                  TNMSG_OP_TENSORS_OFFSET,
                                  param_size != 0 ? param_offset : 0,
                                  param_size,
                                  split};
    tnmsg_write_op_header(dst, &op);
    *msg_size = size;
    return TN_SUCCESS;
}

} // namespace

tndnn_ctx::tndnn_ctx(tnrt_kernel_t kernel, tnrt_stream_t stream, tndnn_runmode_t runmode,
                     uint32_t num_cores)
    : kernel_(kernel), stream_(stream), runmode_(runmode), num_cores_(num_cores) {}

tndnn_ctx::~tndnn_ctx() {
    // the caller guarantees no execution is still in flight
    // (docs/tndnn-graph-format.md §13)
    if (device_graph_ != nullptr) {
        tnrt_mem_free(device_graph_);
    }
}

tn_status_t tndnn_ctx::AddNode(tnop_opcode_t opcode, const tn_tensor_t *const *tensors,
                               uint32_t tensor_count, const void *param, uint32_t param_size) {
    if (opcode >= TNOP_OPCODE_COUNT || tensors == nullptr || tensor_count > TNMSG_MAX_OP_TENSORS ||
        (param == nullptr) != (param_size == 0)) {
        return TN_ERR_PARAM;
    }
    if (runmode_ == TNDNN_RUNMODE_GRAPH && state_ != STATE_RECORDING) {
        return TN_ERR_STATE;
    }
    uint8_t msg[kMaxOpMsgSize];
    uint32_t msg_size = 0;
    const tn_status_t st =
        build_op_msg(msg, &msg_size, num_cores_, opcode, tensors, tensor_count, param, param_size);
    if (st != TN_SUCCESS) {
        return st;
    }
    if (runmode_ == TNDNN_RUNMODE_OP) {
        return tnrt_kernel_launch(kernel_, TNMSG_ENTRY_OP, msg, msg_size, num_cores_, stream_);
    }
    // graph mode: append the node record; records are 8-byte aligned and
    // the zero padding is skipped by the executor's align8(msg_size)
    // stride (docs/tndnn-graph-format.md §7)
    const uint64_t padded = (uint64_t(msg_size) + 7u) & ~7u;
    if (nodes_.size() + padded > UINT32_MAX - TNMSG_GHDR_SIZE) {
        return TN_ERR_PARAM; // graph offsets are 32-bit (docs §6)
    }
    try {
        const size_t begin = nodes_.size();
        nodes_.resize(begin + size_t(padded), 0);
        memcpy(nodes_.data() + begin, msg, msg_size);
    } catch (...) {
        return TN_ERR_NOMEM;
    }
    node_count_++;
    return TN_SUCCESS;
}

tn_status_t tndnn_ctx::BuildGraph() {
    if (runmode_ != TNDNN_RUNMODE_GRAPH || state_ != STATE_RECORDING) {
        return TN_ERR_STATE;
    }
    // device graph = 16-byte header + the recorded node records
    // (docs/tndnn-graph-format.md §5); per-node serialization and
    // validation already happened in AddNode()
    const uint64_t graph_size = TNMSG_GHDR_SIZE + nodes_.size();
    tnrt_mem_t device_graph = nullptr;
    tn_status_t st = tnrt_mem_alloc(&device_graph, graph_size);
    if (st != TN_SUCCESS) {
        return st;
    }
    uint8_t header[TNMSG_GHDR_SIZE];
    const tnmsg_graph_header_t gh = {node_count_, TNMSG_GHDR_SIZE};
    tnmsg_write_graph_header(header, &gh);
    st = tnrt_mem_s2d(device_graph, header, sizeof(header));
    if (st == TN_SUCCESS && !nodes_.empty()) {
        st = tnrt_mem_s2d(static_cast<uint8_t *>(device_graph) + TNMSG_GHDR_SIZE, nodes_.data(),
                          nodes_.size());
    }
    if (st != TN_SUCCESS) {
        // the graph stays RECORDING; the caller may retry the build
        tnrt_mem_free(device_graph);
        return st;
    }
    device_graph_ = device_graph;
    graph_size_ = graph_size;
    std::vector<uint8_t>().swap(nodes_); // recording scratch is no longer needed
    state_ = STATE_BUILT;
    return TN_SUCCESS;
}

tn_status_t tndnn_ctx::ExecuteGraph() {
    if (runmode_ != TNDNN_RUNMODE_GRAPH || state_ != STATE_BUILT) {
        return TN_ERR_STATE;
    }
    // launch message: TNRT header + the graph payload (device graph
    // address, launch width) — docs/tndnn-graph-format.md §11
    uint8_t msg[TNMSG_GRAPH_MSG_SIZE];
    memset(msg, 0, sizeof(msg));
    const tnmsg_header_t hdr = {TNMSG_TYPE_GRAPH, sizeof(msg)};
    tnmsg_write_header(msg, &hdr);
    const tnmsg_graph_payload_t payload = {(uint64_t)(uintptr_t)device_graph_, num_cores_};
    tnmsg_write_graph_payload(msg, &payload);
    return tnrt_kernel_launch(kernel_, TNMSG_ENTRY_GRAPH, msg, sizeof(msg), num_cores_, stream_);
}
