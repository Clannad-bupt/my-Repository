// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/chip/kernel.c
 *
 * Backend: chip — silicon implementation (instruction issue path)
 *
 * The kernel library runs on the chip's scalar core: the firmware entry
 * (scalar/) calls kernel.h functions directly and each function encodes
 * and issues its instruction synchronously — no command queue, no
 * submission step, no async completion at this layer.
 *
 *   - TDR truth lives in hardware; the shadow is the scalar-side mirror
 *   - tnkr_chip_tdr_program is the only path that programs the TDR
 *   - Completion/sync semantics (fence / poll sys), if the execution
 *     model needs them, are private actions inside each instruction
 *
 * kernel.h instruction implementations: TODO, each mapped to encoded
 * instruction issue per include/kernel/kernel.h
 * ========================================================================== */
#include "kernel/kernel.h"
#include "kernel/common/context_internal.h"
#include "kernel/kernel_backend.h"

/* shadow-change notification: program the hardware TDR with slot idx's
 * new content (TNKR_TDR_PROGRAM_DESC: the 128-bit descriptor;
 * TNKR_TDR_PROGRAM_STRIDE: the FREE companion stride).
 * TODO: encoding path pending the execution-model spec */
void tnkr_chip_tdr_program(tnkr_ctx_t *ctx, uint8_t idx, tnkr_tdr_program_kind_t kind) {
    (void)ctx;
    (void)idx;
    (void)kind;
}
