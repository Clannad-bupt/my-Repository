// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/ops/host/split.cpp
 *
 * Generic host-side split entry for the runtime's operator-mode message
 * assembly: initializes the context (is_scalar=false, core 0) and
 * dispatches to the typed op split by opcode; tensors follow the
 * opcode's fixed operand slot order (out first, absent optional slots
 * NULL). Opcodes without a typed split yet yield an all-zero split.
 *
 * Host-only translation unit: the scalar/firmware image never compiles
 * this directory (it consumes the split from the launch message and
 * never runs split logic).
 * ==========================================================================
 */
#include "ops/ops.h"
#include "tndef.h"

#include <string.h>

#include "../common.h"

tn_status_t tnop_host_split(uint32_t opcode, const tn_tensor_t *const *tensors,
                            uint32_t tensor_count, const void *param, uint32_t num_cores,
                            tnop_split_t *split) {
    if (opcode >= TNOP_OPCODE_COUNT || tensors == nullptr || num_cores == 0 || split == nullptr) {
        return TN_ERR_PARAM;
    }
    tnop_ctx_init(num_cores, 0, false);
    switch (opcode) {
    case TNOP_OPCODE_RMSNORM:
        if (tensor_count != 3 || param == nullptr) {
            return TN_ERR_PARAM;
        }
        return tnop_rmsnorm_split(tensors[0], tensors[1], tensors[2],
                                  (const tnop_rmsnorm_param_t *)param, split);
    case TNOP_OPCODE_ELTWISE:
        if (tensor_count != 3 || param == nullptr) {
            return TN_ERR_PARAM;
        }
        return tnop_eltwise_split(tensors[0], tensors[1], tensors[2],
                                  (const tnop_eltwise_param_t *)param, split);
    default:
        // TODO: the split logic lands together with each op's implementation
        memset(split, 0, sizeof(*split));
        return TN_SUCCESS;
    }
}
