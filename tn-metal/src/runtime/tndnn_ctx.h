// Copyright (c) 2026 ThinkNative. All rights reserved.
// Internal header for tndnn runtime implementation details

#pragma once

#include <stdint.h>

#include <vector>

#include "runtime/tndnn.h"

// Per-handle context behind tndnn_handle_t. One context serves both run
// modes (see docs/tndnn-execution.md):
//   - TNDNN_RUNMODE_GRAPH: AddNode() serializes each node as one op
//     launch message into nodes_; BuildGraph() prepends the device graph
//     header and copies the graph into device memory (the handle turns
//     read-only); ExecuteGraph() launches the built graph on the bound
//     stream.
//   - TNDNN_RUNMODE_OP: AddNode() serializes the same op launch message
//     and launches it immediately; BuildGraph/ExecuteGraph are
//     TN_ERR_STATE.
struct tndnn_ctx {
  public:
    tndnn_ctx(tnrt_kernel_t kernel, tnrt_stream_t stream, tndnn_runmode_t runmode,
              uint32_t num_cores);
    ~tndnn_ctx();

    // Serialize one node as an op launch message (runtime/tnmsg.h).
    // Graph mode: append the message to nodes_ as a node record.
    // Operator mode: launch the message immediately through the unified
    // scalar entry. opcode is the device operator number (ops/params.h);
    // tensors holds the fixed operand slots in order (NULL = absent
    // optional slot); param/param_size describe the non-tensor parameter
    // payload (NULL/0 if the op has none).
    tn_status_t AddNode(tnop_opcode_t opcode, const tn_tensor_t *const *tensors,
                        uint32_t tensor_count, const void *param, uint32_t param_size);

    tn_status_t BuildGraph();
    tn_status_t ExecuteGraph();

  private:
    enum State { STATE_RECORDING, STATE_BUILT };

    const tnrt_kernel_t kernel_;
    const tnrt_stream_t stream_;
    const tndnn_runmode_t runmode_;
    const uint32_t num_cores_;

    State state_ = STATE_RECORDING;
    uint32_t node_count_ = 0;
    // graph mode only: serialized node records, each an op launch message
    // at an 8-byte aligned stride; freed by BuildGraph()
    std::vector<uint8_t> nodes_;
    tnrt_mem_t device_graph_ = nullptr; // built device graph, freed at destroy
    uint64_t graph_size_ = 0;
};
