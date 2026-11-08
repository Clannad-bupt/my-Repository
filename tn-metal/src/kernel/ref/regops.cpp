// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/regops.cpp
 *
 * sys/tdr-class instruction implementations — ref backend
 *
 * Pure register-file semantics (moves and field rewrites between
 * GPR/SYS/TDR): the ref backend's raw writer of the register files.
 * Only ref compiles this file — the chip backend encodes and issues
 * the same instructions instead (chip/kernel.c).
 *
 * Conventions:
 *   - Instructions fetch the context via tnkr_ctx_default() and act on the
 *     local core's register files (one firmware instance per core);
 *     uninitialized / out-of-range index -> A-class violation (violate.h)
 *   - tdr-class instructions rewrite raw bits: no tdr_check / alloc checks
 *     (hardware is equally unaware of host bookkeeping); checked host-side
 *     configuration goes through context_internal.h tdr_shadow_write*
 *   - After any tdr-class write to a slot: valid=1, is_stride=0
 * ========================================================================== */
#include "kernel/common/context_internal.h"
#include "kernel/kernel.h"
#include "kernel/ref/violate.h"

namespace {

/* 128-bit field ops (GNU __int128 extension, available on the host simulator) */
typedef unsigned __int128 u128;

u128 tdr_load128(const tnkr_ctx_t *ctx, tdr_idx_t idx) {
    return ((u128)ctx->shadow[idx].desc.raw[1] << 64) | ctx->shadow[idx].desc.raw[0];
}

void tdr_store128(tnkr_ctx_t *ctx, tdr_idx_t idx, u128 v) {
    ctx->shadow[idx].desc.raw[0] = (uint64_t)v;
    ctx->shadow[idx].desc.raw[1] = (uint64_t)(v >> 64);
    TNKR_BIT_SET(ctx->valid, idx);
    TNKR_BIT_CLR(ctx->is_stride, idx);
}

/* target field [MSB:LSB], 0 <= LSB <= MSB < 128 */
/* field ranges are half-local: 0..63 inside the offset-selected half */
bool field_ok(uint8_t lsb, uint8_t msb) { return lsb <= msb && msb < 64; }

u128 field_mask(uint8_t lsb, uint8_t msb) {
    unsigned w = (unsigned)msb - lsb + 1;
    return (w == 128) ? ~(u128)0 : ((((u128)1) << w) - 1) << lsb;
}

/* GPR pair as 128-bit: src0 low 64, src1 high 64 (dual of tdr_read/write) */
u128 gpr_pair(const tnkr_ctx_t *ctx, gpr_idx_t s0, gpr_idx_t s1) {
    const uint64_t *gpr = ctx->gpr;
    return ((u128)gpr[s1] << 64) | gpr[s0];
}

bool gpr_ok(const tnkr_ctx_t *ctx, gpr_idx_t g) { return g < ctx->info.gpr_num; }

bool tdr_ok(const tnkr_ctx_t *ctx, tdr_idx_t t) { return t < ctx->info.tdr_num; }

} // anonymous namespace

/* ==========================================================================
 * sys — scalar moves between SYS and GPR
 * ========================================================================== */
extern "C" void sys_read(gpr_idx_t gpr_dst, sys_idx_t sys_src) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    if (ctx == nullptr)
        TNKR_REF_VIOLATE("sys_read", "context not initialized");
    TNKR_REF_CHECK("sys_read", gpr_ok(ctx, gpr_dst), "GPR index %u out of range",
                   (unsigned)gpr_dst);
    TNKR_REF_CHECK("sys_read", sys_src < ctx->info.sys_num, "SYS index %u out of range",
                   (unsigned)sys_src);
    ctx->gpr[gpr_dst] = ctx->sys[sys_src];
}

extern "C" void sys_write(sys_idx_t sys_dst, gpr_idx_t gpr_src) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    if (ctx == nullptr)
        TNKR_REF_VIOLATE("sys_write", "context not initialized");
    TNKR_REF_CHECK("sys_write", sys_dst < ctx->info.sys_num, "SYS index %u out of range",
                   (unsigned)sys_dst);
    TNKR_REF_CHECK("sys_write", gpr_ok(ctx, gpr_src), "GPR index %u out of range",
                   (unsigned)gpr_src);
    ctx->sys[sys_dst] = ctx->gpr[gpr_src];
}

/* ==========================================================================
 * tdr — TDR read/write and field rewrite
 * ========================================================================== */
extern "C" void tdr_read(gpr_idx_t gpr_dst0, gpr_idx_t gpr_dst1, tdr_idx_t tdr_src) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    if (ctx == nullptr)
        TNKR_REF_VIOLATE("tdr_read", "context not initialized");
    TNKR_REF_CHECK("tdr_read", tdr_ok(ctx, tdr_src), "TDR index %u out of range",
                   (unsigned)tdr_src);
    TNKR_REF_CHECK("tdr_read", gpr_ok(ctx, gpr_dst0) && gpr_ok(ctx, gpr_dst1),
                   "GPR pair index out of range");
    ctx->gpr[gpr_dst0] = ctx->shadow[tdr_src].desc.raw[0];
    ctx->gpr[gpr_dst1] = ctx->shadow[tdr_src].desc.raw[1];
}

extern "C" void tdr_write(tdr_idx_t tdr_dst, gpr_idx_t gpr_src0, gpr_idx_t gpr_src1) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    if (ctx == nullptr)
        TNKR_REF_VIOLATE("tdr_write", "context not initialized");
    TNKR_REF_CHECK("tdr_write", tdr_ok(ctx, tdr_dst), "TDR index %u out of range",
                   (unsigned)tdr_dst);
    TNKR_REF_CHECK("tdr_write", gpr_ok(ctx, gpr_src0) && gpr_ok(ctx, gpr_src1),
                   "GPR pair index out of range");
    tdr_store128(ctx, tdr_dst, gpr_pair(ctx, gpr_src0, gpr_src1));
}

/* field rewrite: one instruction touches exactly one 64-bit half —
 * offset (tdr_half_t) selects it, LSB/MSB are half-local (0..63), one
 * source GPR / 64-bit immediate covers every field (kernel.h) */
extern "C" void tdr_insert(tdr_idx_t tdr_dst, gpr_idx_t gpr_src, tdr_half_t offset, uint8_t LSB,
                           uint8_t MSB) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    uint8_t lo, hi;
    u128 cur, m, v;

    if (ctx == nullptr)
        TNKR_REF_VIOLATE("tdr_insert", "context not initialized");
    TNKR_REF_CHECK("tdr_insert", tdr_ok(ctx, tdr_dst), "TDR index %u out of range",
                   (unsigned)tdr_dst);
    TNKR_REF_CHECK("tdr_insert", gpr_ok(ctx, gpr_src), "GPR index out of range");
    TNKR_REF_CHECK("tdr_insert", offset == TDR_HALF_LO || offset == TDR_HALF_HI,
                   "offset must be TDR_HALF_LO or TDR_HALF_HI");
    TNKR_REF_CHECK("tdr_insert", field_ok(LSB, MSB), "field range %u..%u illegal", (unsigned)LSB,
                   (unsigned)MSB);
    lo = (uint8_t)(offset * 64u + LSB);
    hi = (uint8_t)(offset * 64u + MSB);
    cur = tdr_load128(ctx, tdr_dst);
    m = field_mask(lo, hi);
    v = ((u128)ctx->gpr[gpr_src] << lo) & m;
    tdr_store128(ctx, tdr_dst, (cur & ~m) | v);
}

extern "C" void tdr_insert_imm(tdr_idx_t tdr_dst, uint64_t imm, tdr_half_t offset, uint8_t LSB,
                               uint8_t MSB) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    uint8_t lo, hi;
    u128 cur, m, v;

    if (ctx == nullptr)
        TNKR_REF_VIOLATE("tdr_insert_imm", "context not initialized");
    TNKR_REF_CHECK("tdr_insert_imm", tdr_ok(ctx, tdr_dst), "TDR index %u out of range",
                   (unsigned)tdr_dst);
    TNKR_REF_CHECK("tdr_insert_imm", offset == TDR_HALF_LO || offset == TDR_HALF_HI,
                   "offset must be TDR_HALF_LO or TDR_HALF_HI");
    TNKR_REF_CHECK("tdr_insert_imm", field_ok(LSB, MSB), "field range %u..%u illegal",
                   (unsigned)LSB, (unsigned)MSB);
    lo = (uint8_t)(offset * 64u + LSB);
    hi = (uint8_t)(offset * 64u + MSB);
    cur = tdr_load128(ctx, tdr_dst);
    m = field_mask(lo, hi);
    v = (((u128)imm) << lo) & m;
    tdr_store128(ctx, tdr_dst, (cur & ~m) | v);
}

/* add: the field <- (field + value) truncated to the field width */
extern "C" void tdr_add(tdr_idx_t tdr_dst, gpr_idx_t gpr_src, tdr_half_t offset, uint8_t LSB,
                        uint8_t MSB) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    uint8_t lo, hi;
    u128 cur, m, sum;

    if (ctx == nullptr)
        TNKR_REF_VIOLATE("tdr_add", "context not initialized");
    TNKR_REF_CHECK("tdr_add", tdr_ok(ctx, tdr_dst), "TDR index %u out of range", (unsigned)tdr_dst);
    TNKR_REF_CHECK("tdr_add", gpr_ok(ctx, gpr_src), "GPR index out of range");
    TNKR_REF_CHECK("tdr_add", offset == TDR_HALF_LO || offset == TDR_HALF_HI,
                   "offset must be TDR_HALF_LO or TDR_HALF_HI");
    TNKR_REF_CHECK("tdr_add", field_ok(LSB, MSB), "field range %u..%u illegal", (unsigned)LSB,
                   (unsigned)MSB);
    lo = (uint8_t)(offset * 64u + LSB);
    hi = (uint8_t)(offset * 64u + MSB);
    cur = tdr_load128(ctx, tdr_dst);
    m = field_mask(lo, hi);
    sum = ((cur & m) + (((u128)ctx->gpr[gpr_src] << lo) & m)) & m;
    tdr_store128(ctx, tdr_dst, (cur & ~m) | sum);
}

extern "C" void tdr_add_imm(tdr_idx_t tdr_dst, uint64_t imm, tdr_half_t offset, uint8_t LSB,
                            uint8_t MSB) {
    tnkr_ctx_t *ctx = tnkr_ctx_default();
    uint8_t lo, hi;
    u128 cur, m, sum;

    if (ctx == nullptr)
        TNKR_REF_VIOLATE("tdr_add_imm", "context not initialized");
    TNKR_REF_CHECK("tdr_add_imm", tdr_ok(ctx, tdr_dst), "TDR index %u out of range",
                   (unsigned)tdr_dst);
    TNKR_REF_CHECK("tdr_add_imm", offset == TDR_HALF_LO || offset == TDR_HALF_HI,
                   "offset must be TDR_HALF_LO or TDR_HALF_HI");
    TNKR_REF_CHECK("tdr_add_imm", field_ok(LSB, MSB), "field range %u..%u illegal", (unsigned)LSB,
                   (unsigned)MSB);
    lo = (uint8_t)(offset * 64u + LSB);
    hi = (uint8_t)(offset * 64u + MSB);
    cur = tdr_load128(ctx, tdr_dst);
    m = field_mask(lo, hi);
    sum = ((cur & m) + ((((u128)imm) << lo) & m)) & m;
    tdr_store128(ctx, tdr_dst, (cur & ~m) | sum);
}
