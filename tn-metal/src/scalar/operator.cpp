// Copyright (c) 2026 ThinkNative. All rights reserved.

#include "scalar/scalar.h"

#include <string.h>

#include "op_dispatch.h"
#include "runtime/tnmsg.h"

// decode and validate an operator launch message into a run-mode
// independent view (docs/tndnn-execution.md §6); the per-opcode
// operand/param definition is checked by tn_op_dispatch
tn_status_t tn_op_msg_decode(const void *msg, uint64_t size, tn_op_view_t *view) {
    if (msg == nullptr || size < TNMSG_OP_TENSORS_OFFSET) {
        return TN_ERR_PARAM;
    }
    const auto *bytes = static_cast<const uint8_t *>(msg);

    // launch header: TNRT envelope with an OP payload
    tnmsg_header_t hdr;
    tnmsg_read_header(bytes, &hdr);
    uint32_t reserved1 = 0;
    tnmsg_load(bytes, TNMSG_HDR_RESERVED1, &reserved1, 4);
    if (memcmp(bytes + TNMSG_HDR_MAGIC, TNMSG_MAGIC, 4) != 0 || hdr.type != TNMSG_TYPE_OP ||
        hdr.msg_size != size || reserved1 != 0) {
        return TN_ERR_PARAM;
    }

    // op payload header
    tnmsg_op_header_t op;
    tnmsg_read_op_header(bytes, &op);
    if (op.num_cores == 0 || op.opcode >= TNOP_OPCODE_COUNT ||
        op.tensor_count > TNMSG_MAX_OP_TENSORS || op.tensors_offset != TNMSG_OP_TENSORS_OFFSET ||
        (op.tensor_mask >> op.tensor_count) != 0) {
        return TN_ERR_PARAM;
    }
    const uint64_t tensors_end =
        uint64_t(op.tensors_offset) + uint64_t(op.tensor_count) * TNMSG_TENSOR_DESC_SIZE;
    if (tensors_end > size) {
        return TN_ERR_PARAM;
    }
    // the payload is 8-byte aligned (docs/tndnn-execution.md §6.1); the
    // exec layer reinterprets it as a param struct pointer
    if (op.param_size != 0 && (op.param_offset < tensors_end || (op.param_offset & 7u) != 0 ||
                               uint64_t(op.param_offset) + op.param_size > size)) {
        return TN_ERR_PARAM;
    }
    view->opcode = op.opcode;
    view->tensor_mask = op.tensor_mask;
    view->tensor_count = op.tensor_count;
    view->param = op.param_size != 0 ? bytes + op.param_offset : nullptr;
    view->param_size = op.param_size;
    view->num_cores = op.num_cores;
    view->split = op.split;
    for (uint32_t i = 0; i < op.tensor_count; ++i) {
        if ((op.tensor_mask & (1u << i)) != 0) {
            const uint8_t *desc = bytes + op.tensors_offset + uint64_t(i) * TNMSG_TENSOR_DESC_SIZE;
            tnmsg_read_tensor(desc, &view->tensors[i]);
        }
        // an absent slot's descriptor is ignored, whatever it holds
    }
    return TN_SUCCESS;
}

tn_status_t tn_scalar_run_operator(const void *msg, uint64_t size) {
    tn_op_view_t view;
    const tn_status_t st = tn_op_msg_decode(msg, size, &view);
    if (st != TN_SUCCESS) {
        return st;
    }
    return tn_op_dispatch(&view);
}
