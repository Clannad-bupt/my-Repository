// Copyright (c) 2026 ThinkNative. All rights reserved.

#pragma once

#include "tndef.h"

#ifdef __cplusplus
extern "C" {
#endif

// ================================================
// Graph Mode
// ================================================
tn_status_t tn_scalar_run_graph(const void *msg, uint64_t size);

// ================================================
// Operator Mode
// ================================================
// unified operator entry: the launch message (runtime/tnmsg.h) carries a
// TNMSG_TYPE_OP payload whose op header holds the opcode, which selects
// the operator implementation; there are no per-op exported symbols
tn_status_t tn_scalar_run_operator(const void *msg, uint64_t size);

#if TN_PSEUDO_CODE
// host-emulation core identity (TNMSG_HOOK_SET_CORE_IDX): the runtime
// launches one thread per core and programs the calling thread's index
// through this hook before the entry runs; the dispatch layer reads it
// back as core_idx. Device images instead take the identity from the fw
// (lpu_fw_core_id) and carry no such hook
void tn_scalar_set_core_idx(uint32_t idx);
#endif

#ifdef __cplusplus
}
#endif
