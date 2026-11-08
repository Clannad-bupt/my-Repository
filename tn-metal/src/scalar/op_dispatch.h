// Copyright (c) 2026 ThinkNative. All rights reserved.

// Internal: the decoded view of one operator invocation, the op-message
// decoder and the opcode dispatch shared by the operator-message entry
// (operator.cpp) and the graph executor (graph.cpp) —
// docs/tndnn-execution.md §7. A graph node record is a complete op
// launch message, so both adapters use the same decoder; everything past
// tn_op_dispatch is run-mode independent.

#pragma once

#include "runtime/tnmsg.h"
#include "tndef.h"

typedef struct tn_op_view {
    uint32_t opcode;      // tnop_opcode_t
    uint32_t tensor_mask; // bit i = operand slot i present
    uint32_t tensor_count;
    tn_tensor_t tensors[TNMSG_MAX_OP_TENSORS]; // slot i valid iff mask bit i set
    const uint8_t *param;                      // payload, NULL when param_size == 0
    uint32_t param_size;
    uint32_t num_cores; // launch width from the op header / graph payload
    tnop_split_t split; // host-computed core-0 work share (op header)
} tn_op_view_t;

// decode and validate one op launch message (TNRT header + op payload)
// into a run-mode independent view (docs/tndnn-execution.md §6); the
// per-opcode operand/param definition is checked by tn_op_dispatch
tn_status_t tn_op_msg_decode(const void *msg, uint64_t size, tn_op_view_t *view);

// validate the view against the opcode's fixed operand/param definition,
// then dispatch to the operator implementation; TN_ERR_PARAM on an
// unknown opcode or a definition mismatch
tn_status_t tn_op_dispatch(const tn_op_view_t *view);
