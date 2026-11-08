// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/test/smoke.c
 *
 * ref backend smoke test: driver (main) + shared-piece implementations.
 * Test families are split per kernel.h section, see the smoke.h header;
 * a new family is a new test_<family>.c with suite_<family>(),
 * registered here in main and in the CMake source list.
 *
 * Data path: tests read/write ctx->global directly via the internal header
 * context_internal.h (the real host memory API is not designed yet); input
 * data enters URF via top_ldst_load, results come back to global via
 * top_ldst_store and are compared against expectations — except for the
 * "address-formula direct assertion" cases, tests do not depend on the
 * URF-internal layout, matching the end-user perspective.
 * ========================================================================== */
#include "smoke.h"

int g_fail = 0;

/* ---- violation capture (see smoke.h TNVI_BEGIN/END) ---- */
static char tnvi_expect[64];
char tnvi_msg[256];
jmp_buf tnvi_jmp;

static void tnvi_handler(const char *op, const char *file, int line, const char *msg) {
    (void)file;
    (void)line;
    snprintf(tnvi_msg, sizeof(tnvi_msg), "%s: %s", op, msg);
    longjmp(tnvi_jmp, 1);
}

void tnvi_begin(const char *expect_sub) {
    snprintf(tnvi_expect, sizeof(tnvi_expect), "%s", expect_sub);
    tnkr_ref_set_violation_handler(tnvi_handler);
}

void tnvi_caught(void) {
    char buf[512];
    tnkr_ref_set_violation_handler(NULL);
    snprintf(buf, sizeof(buf), "violation matched '%s' (got: %s)", tnvi_expect, tnvi_msg);
    CHECK(strstr(tnvi_msg, tnvi_expect) != NULL, buf);
}

void tnvi_missed(void) {
    char buf[128];
    tnkr_ref_set_violation_handler(NULL);
    snprintf(buf, sizeof(buf), "expected violation '%s' — none raised", tnvi_expect);
    CHECK(0, buf);
}

uint8_t mk_desc(tnkr_ctx_t *ctx, const tdr_t *d) {
    int idx = tdr_alloc(ctx);

    if (idx < 0 || tdr_shadow_write(ctx, (uint8_t)idx, d) != TN_SUCCESS) {
        printf("  FATAL: descriptor setup failed\n");
        g_fail++;
        return 0;
    }
    return (uint8_t)idx;
}

uint32_t t_expect_int(int64_t v, unsigned bits, int is_signed, ovf_mode_t ovf) {
    uint32_t mask = (bits >= 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);

    if (ovf == OVF_MODE_TRUNCATE)
        return (uint32_t)v & mask;
    {
        int64_t lo = is_signed ? -(1LL << (bits - 1u)) : 0;
        int64_t hi = is_signed ? (1LL << (bits - 1u)) - 1 : (1LL << bits) - 1;
        if (v < lo)
            v = lo;
        if (v > hi)
            v = hi;
    }
    return (uint32_t)v & mask;
}

void run_binop(tnkr_ctx_t *ctx, binop_fn fn, tn_dtype_t idt, tn_dtype_t odt, ovf_mode_t ovf,
               const void *A, const void *B, void *OUT) {
    size_t isz = NELEM * tn_dtype_size(idt);
    size_t osz = NELEM * tn_dtype_size(odt);
    tdr_t d;
    uint8_t sga, sgb, sgo, sua, sub_, suo;

    memcpy(ctx->global + GA, A, isz);
    memcpy(ctx->global + GB, B, isz);
    memset(ctx->global + GO, 0xAA, osz);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, idt, GA, 1, 1, 4, 8);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, idt, GB, 1, 1, 4, 8);
    sgb = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, odt, GO, 1, 1, 4, 8);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, idt, UA, 1, 1, 4, 8, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, idt, UB, 1, 1, 4, 8, 0);
    sub_ = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, odt, UO, 1, 1, 4, 8, 0);
    suo = mk_desc(ctx, &d);

    top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
    top_ldst_load(sub_, sgb, 0, LDST_MODE_NORMAL);
    fn(suo, sua, sub_, ovf);
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(OUT, ctx->global + GO, osz);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgb);
    tdr_free(ctx, sgo);
    tdr_free(ctx, sua);
    tdr_free(ctx, sub_);
    tdr_free(ctx, suo);
}

void op_div(tdr_idx_t o, tdr_idx_t l, tdr_idx_t r, ovf_mode_t m) {
    (void)m;
    top_vec_div(o, l, r);
}
void op_max(tdr_idx_t o, tdr_idx_t l, tdr_idx_t r, ovf_mode_t m) {
    (void)m;
    top_vec_max(o, l, r);
}
void op_min(tdr_idx_t o, tdr_idx_t l, tdr_idx_t r, ovf_mode_t m) {
    (void)m;
    top_vec_min(o, l, r);
}
void op_and(tdr_idx_t o, tdr_idx_t l, tdr_idx_t r, ovf_mode_t m) {
    (void)m;
    top_vec_and(o, l, r);
}
void op_or(tdr_idx_t o, tdr_idx_t l, tdr_idx_t r, ovf_mode_t m) {
    (void)m;
    top_vec_or(o, l, r);
}
void op_xor(tdr_idx_t o, tdr_idx_t l, tdr_idx_t r, ovf_mode_t m) {
    (void)m;
    top_vec_xor(o, l, r);
}
void op_nxor(tdr_idx_t o, tdr_idx_t l, tdr_idx_t r, ovf_mode_t m) {
    (void)m;
    top_vec_nxor(o, l, r);
}

void run_unary(tnkr_ctx_t *ctx, unop_fn fn, tn_dtype_t idt, tn_dtype_t odt, const void *A,
               void *OUT) {
    size_t isz = NELEM * tn_dtype_size(idt);
    size_t osz = NELEM * tn_dtype_size(odt);
    tdr_t d;
    uint8_t sga, sgo, sua, suo;

    memcpy(ctx->global + GA, A, isz);
    memset(ctx->global + GO, 0xAA, osz);

    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, idt, GA, 1, 1, 4, 8);
    sga = mk_desc(ctx, &d);
    d = tdr_make_global(TDR_LAYOUT_CONTINUOUS, odt, GO, 1, 1, 4, 8);
    sgo = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, idt, UA, 1, 1, 4, 8, 0);
    sua = mk_desc(ctx, &d);
    d = tdr_make_urf(TDR_LAYOUT_HWALIGN, odt, UO, 1, 1, 4, 8, 0);
    suo = mk_desc(ctx, &d);

    top_ldst_load(sua, sga, 0, LDST_MODE_NORMAL);
    fn(suo, sua);
    top_ldst_store(sgo, 0, suo, LDST_MODE_NORMAL);
    memcpy(OUT, ctx->global + GO, osz);

    tdr_free(ctx, sga);
    tdr_free(ctx, sgo);
    tdr_free(ctx, sua);
    tdr_free(ctx, suo);
}

void check_int_binop(tnkr_ctx_t *ctx, const char *name, binop_fn fn, char op, tn_dtype_t idt,
                     tn_dtype_t odt, ovf_mode_t ovf, const int64_t *A, const int64_t *B) {
    uint8_t in_a[NELEM * 4], in_b[NELEM * 4], out[NELEM * 4];
    uint8_t expct[NELEM * 4];
    unsigned obits = tn_dtype_bits(odt);
    unsigned isz = tn_dtype_size(idt), osz = tn_dtype_size(odt);
    int isgn = (idt == TN_DTYPE_S8 || idt == TN_DTYPE_S16 || idt == TN_DTYPE_S32);
    int osgn = (odt == TN_DTYPE_S8 || odt == TN_DTYPE_S16 || odt == TN_DTYPE_S32);
    int64_t imask = (1LL << tn_dtype_bits(idt)) - 1;
    uint32_t i;

    for (i = 0; i < NELEM; i++) {
        int64_t a = A[i], b = B[i], v;
        uint32_t e;
        if (!isgn) {
            a &= imask;
            b &= imask;
        }
        switch (op) {
        case '+':
            v = a + b;
            break;
        case '-':
            v = a - b;
            break;
        case '*':
            v = a * b;
            break;
        case '>':
            v = (a > b) ? a : b;
            break;
        default:
            v = (a < b) ? a : b;
            break;
        }
        e = t_expect_int(v, obits, osgn, ovf);
        memset(in_a + i * isz, 0, isz);
        memset(in_b + i * isz, 0, isz);
        memcpy(in_a + i * isz, &a, isz < sizeof(a) ? isz : sizeof(a));
        memcpy(in_b + i * isz, &b, isz < sizeof(b) ? isz : sizeof(b));
        memset(expct + i * osz, 0, osz);
        memcpy(expct + i * osz, &e, osz < sizeof(e) ? osz : sizeof(e));
    }
    run_binop(ctx, fn, idt, odt, ovf, in_a, in_b, out);
    CHECK(memcmp(out, expct, NELEM * osz) == 0, name);
}

int main(void) {
    tnkr_chip_info_t info;
    tnkr_ctx_t *ctx;
    int err;

    setvbuf(stdout, NULL, _IONBF, 0); /* unbuffered: keep stderr interleave honest */
    printf("== violation paths ==\n");
    /* not initialized: every instruction raises a violation (one block per
     * call so each path is pinned individually) */
    TNVI_BEGIN("context not initialized") { top_vec_add(0, 0, 0, OVF_MODE_SATURATE); }
    TNVI_END();
    TNVI_BEGIN("context not initialized") { top_ldst_load(0, 0, 0, LDST_MODE_NORMAL); }
    TNVI_END();
    TNVI_BEGIN("context not initialized") { top_vec_move(0, 0, 0, 0); }
    TNVI_END();
    TNVI_BEGIN("context not initialized") { sys_read(0, 0); }
    TNVI_END();
    TNVI_BEGIN("context not initialized") { tdr_insert_imm(0, 0, TDR_HALF_LO, 0, 0); }
    TNVI_END();
    TNVI_BEGIN("context not initialized") { dmr_ld(0, 0, 0, DMR_MODE_NORMAL); }
    TNVI_END();

    /* capabilities are a compile-time constant set (spec_<chip>.h) */
    info = *tnkr_spec_info();
    printf("device: cores=%u smc/core=%u urf/core=%uB banks/smc=%u\n", info.core_num, info.smc_num,
           info.urf_bytes, info.bank_num);

    ctx = tnkr_init(&err);
    CHECK(ctx != NULL && err == TN_SUCCESS, "tnkr_init");
    if (ctx == NULL)
        return 1;

    /* static-context lifecycle: a double init is TN_ERR_STATE, and a
     * deinit -> init cycle fully resets the software state without any
     * BSS re-zero (runs before any suite touches the allocator) */
    {
        tnkr_ctx_t *dup;
        int err2 = TN_SUCCESS;

        dup = tnkr_init(&err2);
        CHECK(dup == NULL && err2 == TN_ERR_STATE, "double init -> TN_ERR_STATE");
        tnkr_deinit(ctx);
        ctx = tnkr_init(&err);
        CHECK(ctx != NULL && err == TN_SUCCESS, "re-init after deinit");
        if (ctx == NULL)
            return 1;
    }

    /* bank-split invariant: bank_size > 0 and all banks together exactly
     * reproduce the total URF capacity */
    CHECK(tnkr_urf_bank_size(ctx) > 0 &&
              (uint64_t)tnkr_urf_bank_size(ctx) * info.smc_num * info.bank_num == info.urf_bytes,
          "tnkr_urf_bank_size: banks evenly rebuild urf_bytes");

    /* out-of-range / unconfigured slots: violation with the resolve reason
     * (index tdr_num is the first out-of-range slot, per ctx->info) */
    TNVI_BEGIN("tdr index out of range") {
        top_vec_add((tdr_idx_t)info.tdr_num, (tdr_idx_t)info.tdr_num, (tdr_idx_t)info.tdr_num,
                    OVF_MODE_SATURATE);
        top_ldst_store((tdr_idx_t)info.tdr_num, (tdr_idx_t)info.tdr_num, (tdr_idx_t)info.tdr_num,
                       LDST_MODE_NORMAL);
        dmr_st((tdr_idx_t)info.tdr_num, (tdr_idx_t)info.tdr_num, (tdr_idx_t)info.tdr_num,
               REDUCE_MODE_NONE, DMR_MODE_NORMAL);
    }
    TNVI_END();
    TNVI_BEGIN("tdr slot unconfigured") { top_vec_add(0, 0, 0, OVF_MODE_SATURATE); }
    TNVI_END();
    TNVI_BEGIN("tdr slot unconfigured") { top_ldst_store(0, 0, 0, LDST_MODE_NORMAL); }
    TNVI_END();
    TNVI_BEGIN("tdr slot unconfigured") { dmr_st(0, 0, 0, REDUCE_MODE_NONE, DMR_MODE_NORMAL); }
    TNVI_END();

    suite_regops(ctx);
    suite_mm_fp(ctx);
    suite_mm_bq(ctx);
    suite_mm_conv(ctx);
    suite_ldst(ctx);
    suite_ldst_gather(ctx);
    suite_vec_move(ctx);
    suite_vec_arith(ctx);
    suite_vec_mac(ctx);
    suite_vec_unary(ctx);
    suite_vec_logic(ctx);
    suite_vec_shift(ctx);
    suite_vec_reduce(ctx);
    suite_vec_cmp(ctx);
    suite_vec_special(ctx);
    suite_vec_cast(ctx);
    suite_vec_quant(ctx);
    suite_vec_conv(ctx);
    suite_vec_depthwise(ctx);
    suite_vec_pool(ctx);
    suite_vec_matmul(ctx);
    suite_dmr_move(ctx);
    suite_dmr_gather(ctx);
    suite_dmr_sort(ctx);

    /* chip-specific families (declared in smoke.h, same macro as the build) */
#ifdef TN_CHIP_TNA1
    suite_tna1_dtype(ctx);
#endif

    tnkr_deinit(ctx);
    printf("== %s (%d failures) ==\n", g_fail == 0 ? "ALL PASS" : "FAILURES PRESENT", g_fail);
    return g_fail != 0;
}
