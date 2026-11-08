// Copyright (c) 2026 ThinkNative. All rights reserved.

#include "scalar/scalar.h"

#include <string.h>

#include "op_dispatch.h"
#include "runtime/tnmsg.h"
#include "tndef.h"

tn_status_t tn_scalar_run_graph(const void *msg, uint64_t size) {
    // the graph launch message is exactly 28 bytes (docs/tndnn-graph-format.md §11)
    if (msg == nullptr || size != TNMSG_GRAPH_MSG_SIZE) {
        return TN_ERR_PARAM;
    }
    const auto *bytes = static_cast<const uint8_t *>(msg);
    tnmsg_header_t hdr;
    tnmsg_read_header(bytes, &hdr);
    uint32_t reserved1 = 0;
    tnmsg_load(bytes, TNMSG_HDR_RESERVED1, &reserved1, 4);
    if (memcmp(bytes + TNMSG_HDR_MAGIC, TNMSG_MAGIC, 4) != 0 || hdr.type != TNMSG_TYPE_GRAPH ||
        hdr.msg_size != size || reserved1 != 0) {
        return TN_ERR_PARAM;
    }
    tnmsg_graph_payload_t payload;
    tnmsg_read_graph_payload(bytes, &payload);
    if (payload.num_cores == 0 || payload.graph_addr == 0) {
        return TN_ERR_PARAM;
    }

    // device graph: 16-byte header + node records, each a complete op
    // launch message (docs/tndnn-graph-format.md §5-§7); the launch
    // message carries no graph size, so the walk trusts each node's
    // msg_size — every node is still fully validated by the shared
    // op-message decoder before dispatch
    const auto *graph =
        reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(payload.graph_addr));
    uint32_t header_size = 0;
    tnmsg_load(graph, TNMSG_GHDR_HEADER_SIZE, &header_size, 4);
    tnmsg_graph_header_t gh;
    tnmsg_read_graph_header(graph, &gh);
    if (memcmp(graph + TNMSG_GHDR_MAGIC, TNMSG_GRAPH_MAGIC, 4) != 0 ||
        header_size != TNMSG_GHDR_SIZE || gh.nodes_offset < TNMSG_GHDR_SIZE ||
        (gh.nodes_offset & 7u) != 0) {
        return TN_ERR_PARAM;
    }
    const uint8_t *node = graph + gh.nodes_offset;
    for (uint32_t i = 0; i < gh.node_count; ++i) {
        uint32_t node_size = 0;
        tnmsg_load(node, TNMSG_HDR_MSG_SIZE, &node_size, 4);
        // same decode and dispatch as the operator entry
        tn_op_view_t view;
        tn_status_t st = tn_op_msg_decode(node, node_size, &view);
        if (st == TN_SUCCESS) {
            st = tn_op_dispatch(&view);
        }
        if (st != TN_SUCCESS) {
            return st;
        }
        node += (uint64_t(node_size) + 7u) & ~7u; // 8-byte node alignment
    }
    return TN_SUCCESS;
}
