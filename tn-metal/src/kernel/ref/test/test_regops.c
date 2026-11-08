// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/test/test_regops.c
 *
 * sys/tdr-class register instruction cases: sys_read/sys_write round-trip
 * (direct per-core register-file pokes), tdr_write/tdr_read 128-bit halves
 * plus shadow bookkeeping (any tdr-class write re-marks the slot
 * valid=1 / is_stride=0), tdr_insert/tdr_insert_imm field rewrite
 * (offset selects the 64-bit half, LSB/MSB are half-local; over-wide
 * values truncate to the field, the rest of the 128 bits stay
 * untouched) and tdr_add/tdr_add_imm field-width wrap (the carry out of
 * the field is dropped), plus violation negatives (bad GPR/SYS/TDR index,
 * illegal field range / offset) where the destination keeps its sentinel
 * value.
 * Field positions come from the tdr.h macros (TDR_W_LO/TDR_W_W), never
 * literals. Direct ctx->gpr/sys pokes are the register-file counterpart
 * of the ldst file's direct ctx->urf pokes.
 * ========================================================================== */
#include "smoke.h"

void suite_regops(tnkr_ctx_t *ctx) {
    const gpr_idx_t g_last = (gpr_idx_t)(ctx->info.gpr_num - 1);
    const sys_idx_t s_last = (sys_idx_t)(ctx->info.sys_num - 1);
    const tdr_half_t w_off = (tdr_half_t)(TDR_W_LO / 64); /* W field's half */
    const uint8_t w_lsb = TDR_W_LO % 64;                  /* W field [msb:lsb], */
    const uint8_t w_msb = TDR_W_LO % 64 + TDR_W_W - 1;    /* half-local */

    printf("== regops (sys/tdr) ==\n");

    CHECK(ctx->info.gpr_num >= 5 && ctx->info.sys_num >= 1 && ctx->info.tdr_num >= 1,
          "register files meet the suite's minimum sizes");

    /* ---- sys_read / sys_write round-trip at the last valid index ---- */
    ctx->sys[s_last] = 0x0123456789ABCDEFULL;
    sys_read(g_last, s_last);
    CHECK(ctx->gpr[g_last] == 0x0123456789ABCDEFULL, "sys_read: GPR[last] <- SYS[last]");

    ctx->gpr[0] = 0xFEDCBA9876543210ULL;
    sys_write(s_last, 0);
    CHECK(ctx->sys[s_last] == 0xFEDCBA9876543210ULL, "sys_write: SYS[last] <- GPR[0]");

    /* ---- tdr_write / tdr_read: 128-bit halves + shadow bookkeeping ----
     * the slot first holds a FREE companion stride: any tdr-class write
     * re-marks it valid=1 / is_stride=0 (regops.cpp convention) */
    {
        tdr_stride_t s = tdr_make_stride(1, 8, 0, 0);
        int idx = tdr_alloc(ctx);

        if (idx < 0 || tdr_shadow_write_stride(ctx, (uint8_t)idx, &s) != TN_SUCCESS) {
            printf("  FATAL: stride slot setup failed\n");
            g_fail++;
        } else {
            const uint8_t t = (uint8_t)idx;

            CHECK(TNKR_BIT_TEST(ctx->is_stride, idx) == 1, "slot starts as a stride (is_stride=1)");

            ctx->gpr[0] = 0xDEADBEEF01234567ULL; /* low 64  -> raw[0] */
            ctx->gpr[1] = 0x0BADF00DCAFEBABEULL; /* high 64 -> raw[1] */
            tdr_write(t, 0, 1);
            CHECK(ctx->shadow[t].desc.raw[0] == 0xDEADBEEF01234567ULL &&
                      ctx->shadow[t].desc.raw[1] == 0x0BADF00DCAFEBABEULL,
                  "tdr_write: GPR pair -> raw[0]=low, raw[1]=high");
            CHECK(TNKR_BIT_TEST(ctx->valid, idx) == 1 && TNKR_BIT_TEST(ctx->is_stride, idx) == 0,
                  "tdr_write: slot re-marked valid=1, is_stride=0");

            ctx->gpr[2] = 0;
            ctx->gpr[3] = 0;
            tdr_read(2, 3, t);
            CHECK(ctx->gpr[2] == 0xDEADBEEF01234567ULL && ctx->gpr[3] == 0x0BADF00DCAFEBABEULL,
                  "tdr_read: raw[0] -> GPR[2], raw[1] -> GPR[3]");

            tdr_free(ctx, t);
        }
    }

    /* ---- tdr_insert_imm: field rewrite on the descriptor W field ----
     * the W field lives in the high half (tdr.h static asserts), so
     * offset selects half 1 and the expectation is computed half-local:
     * an over-wide immediate truncates to TDR_W_W bits, every other bit
     * stays untouched */
    {
        tdr_t d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 1, 4, 8);
        uint8_t t = mk_desc(ctx, &d);
        uint64_t cur0 = ctx->shadow[t].desc.raw[0];
        uint64_t cur1 = ctx->shadow[t].desc.raw[1];
        const uint64_t imm = 0x1ABCDEULL; /* 21-bit value: truncates to 16 */
        uint64_t exp1 = (cur1 & ~TDR_W_MASK) | (imm & TDR_W_MASK);

        tdr_insert_imm(t, imm, w_off, w_lsb, w_msb);
        CHECK(ctx->shadow[t].desc.raw[0] == cur0 && ctx->shadow[t].desc.raw[1] == exp1,
              "tdr_insert_imm: W field rewritten, over-wide imm truncated, rest unchanged");
        CHECK(tdr_get_w(&ctx->shadow[t].desc) == (uint16_t)(imm & TDR_W_MASK),
              "tdr_insert_imm: tdr_get_w reads the new field");

        tdr_free(ctx, t);
    }

    /* ---- tdr_insert: single-GPR source, field in the low half ---- */
    {
        int idx = tdr_alloc(ctx);
        const uint8_t lsb = 8, msb = 31; /* 24-bit field, low half */
        const uint64_t fmask = (((uint64_t)1 << (msb - lsb + 1)) - 1) << lsb;

        if (idx < 0) {
            printf("  FATAL: descriptor alloc failed\n");
            g_fail++;
        } else {
            const uint8_t t = (uint8_t)idx;
            uint64_t pat0, pat1, exp0;

            ctx->gpr[0] = 0x5A5A5A5A5A5A5A5AULL;
            ctx->gpr[1] = 0xA5A5A5A5A5A5A5A5ULL;
            tdr_write(t, 0, 1);
            pat0 = ctx->shadow[t].desc.raw[0];
            pat1 = ctx->shadow[t].desc.raw[1];

            ctx->gpr[4] = 0x1ABCDEFULL; /* 25-bit value: truncates to 24 */
            tdr_insert(t, 4, TDR_HALF_LO, lsb, msb);
            exp0 = (pat0 & ~fmask) | ((ctx->gpr[4] << lsb) & fmask);
            CHECK(ctx->shadow[t].desc.raw[0] == exp0 && ctx->shadow[t].desc.raw[1] == pat1,
                  "tdr_insert: field rewritten from one GPR, truncated, rest unchanged");

            tdr_free(ctx, t);
        }
    }

    /* ---- tdr_add_imm / tdr_add: field-width wrap, carry dropped ---- */
    {
        tdr_t d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 1, 4, 8);
        uint8_t t = mk_desc(ctx, &d);
        uint64_t cur0 = ctx->shadow[t].desc.raw[0];
        uint64_t hi;

        /* W field: 0xFFFF + 2 wraps to 1; the carry out of bit w_msb must
         * not touch the H field above */
        tdr_insert_imm(t, 0xFFFFULL, w_off, w_lsb, w_msb);
        hi = ctx->shadow[t].desc.raw[1];
        tdr_add_imm(t, 2, w_off, w_lsb, w_msb);
        CHECK(tdr_get_w(&ctx->shadow[t].desc) == 1 &&
                  ctx->shadow[t].desc.raw[1] == ((hi & ~TDR_W_MASK) | 1) &&
                  ctx->shadow[t].desc.raw[0] == cur0,
              "tdr_add_imm: W field wraps at field width, no carry out");

        /* low-half field [31:8]: 0xFFFF00 + 0x200 wraps to 0x100 */
        tdr_insert_imm(t, 0xFFFF00ULL, TDR_HALF_LO, 8, 31);
        cur0 = ctx->shadow[t].desc.raw[0];
        hi = ctx->shadow[t].desc.raw[1];
        ctx->gpr[4] = 0x200ULL;
        tdr_add(t, 4, TDR_HALF_LO, 8, 31);
        CHECK(((ctx->shadow[t].desc.raw[0] >> 8) & 0xFFFFFFu) == 0x100u &&
                  (ctx->shadow[t].desc.raw[0] & ~((((uint64_t)1 << 24) - 1) << 8)) ==
                      (cur0 & ~((((uint64_t)1 << 24) - 1) << 8)) &&
                  ctx->shadow[t].desc.raw[1] == hi,
              "tdr_add: field wraps at field width, bits outside untouched");

        tdr_free(ctx, t);
    }

    /* ---- negatives: violation raised, the destination keeps its sentinel
     * value (checks run before any side effect) ---- */

    /* sys_read with a GPR index at gpr_num: GPR file untouched */
    ctx->gpr[0] = 0x1111111111111111ULL;
    TNVI_BEGIN("GPR index") { sys_read((gpr_idx_t)ctx->info.gpr_num, 0); }
    TNVI_END();
    CHECK(ctx->gpr[0] == 0x1111111111111111ULL, "sys_read bad gpr -> violation, GPR untouched");

    /* sys_write with a SYS index at sys_num: SYS file untouched */
    ctx->sys[0] = 0x2222222222222222ULL;
    TNVI_BEGIN("SYS index") { sys_write((sys_idx_t)ctx->info.sys_num, 0); }
    TNVI_END();
    CHECK(ctx->sys[0] == 0x2222222222222222ULL, "sys_write bad sys -> violation, SYS untouched");

    /* tdr_read with a TDR index at tdr_num: GPR pair untouched */
    ctx->gpr[2] = 0x3333333333333333ULL;
    ctx->gpr[3] = 0x4444444444444444ULL;
    TNVI_BEGIN("TDR index") { tdr_read(2, 3, (tdr_idx_t)ctx->info.tdr_num); }
    TNVI_END();
    CHECK(ctx->gpr[2] == 0x3333333333333333ULL && ctx->gpr[3] == 0x4444444444444444ULL,
          "tdr_read bad tdr -> violation, GPR pair untouched");

    /* tdr_write with an out-of-range GPR pair: slot untouched */
    {
        tdr_t d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 1, 4, 8);
        uint8_t t = mk_desc(ctx, &d);
        uint64_t cur0 = ctx->shadow[t].desc.raw[0];
        uint64_t cur1 = ctx->shadow[t].desc.raw[1];

        TNVI_BEGIN("GPR pair index out of range") { tdr_write(t, (gpr_idx_t)ctx->info.gpr_num, 0); }
        TNVI_END();
        CHECK(ctx->shadow[t].desc.raw[0] == cur0 && ctx->shadow[t].desc.raw[1] == cur1,
              "tdr_write bad gpr pair -> violation, slot untouched");
        tdr_free(ctx, t);
    }

    /* tdr_insert with LSB > MSB: slot untouched */
    {
        tdr_t d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 1, 4, 8);
        uint8_t t = mk_desc(ctx, &d);
        uint64_t cur0 = ctx->shadow[t].desc.raw[0];
        uint64_t cur1 = ctx->shadow[t].desc.raw[1];

        TNVI_BEGIN("field range") { tdr_insert(t, 0, TDR_HALF_LO, 8, 4); }
        TNVI_END();
        CHECK(ctx->shadow[t].desc.raw[0] == cur0 && ctx->shadow[t].desc.raw[1] == cur1,
              "tdr_insert LSB>MSB -> violation, slot untouched");
        tdr_free(ctx, t);
    }

    /* tdr_insert_imm with MSB = 64 (fields are half-local, 0..63): slot
     * untouched */
    {
        tdr_t d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 1, 4, 8);
        uint8_t t = mk_desc(ctx, &d);
        uint64_t cur0 = ctx->shadow[t].desc.raw[0];
        uint64_t cur1 = ctx->shadow[t].desc.raw[1];

        TNVI_BEGIN("field range") { tdr_insert_imm(t, 1, TDR_HALF_LO, 0, 64); }
        TNVI_END();
        CHECK(ctx->shadow[t].desc.raw[0] == cur0 && ctx->shadow[t].desc.raw[1] == cur1,
              "tdr_insert_imm MSB>=64 -> violation, slot untouched");
        tdr_free(ctx, t);
    }

    /* tdr_insert_imm with offset = 2 (only two 64-bit halves exist): slot
     * untouched */
    {
        tdr_t d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, TN_DTYPE_S8, GA, 1, 1, 4, 8);
        uint8_t t = mk_desc(ctx, &d);
        uint64_t cur0 = ctx->shadow[t].desc.raw[0];
        uint64_t cur1 = ctx->shadow[t].desc.raw[1];

        TNVI_BEGIN("offset") { tdr_insert_imm(t, 1, (tdr_half_t)2, 0, 7); }
        TNVI_END();
        CHECK(ctx->shadow[t].desc.raw[0] == cur0 && ctx->shadow[t].desc.raw[1] == cur1,
              "tdr_insert_imm offset>=2 -> violation, slot untouched");
        tdr_free(ctx, t);
    }

    /* tdr_add with a TDR index at tdr_num */
    TNVI_BEGIN("TDR index") { tdr_add((tdr_idx_t)ctx->info.tdr_num, 0, TDR_HALF_LO, 0, 4); }
    TNVI_END();
}
