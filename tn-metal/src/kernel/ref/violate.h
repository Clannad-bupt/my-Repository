// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/ref/violate.h
 *
 * ref backend violation policy — the golden model fails loudly.
 *
 * Policy: a violation is any input the instruction contract rejects
 * that is NOT part of the instruction's specified behavior:
 *   A  environment / addressing violations (not initialized, bad slot,
 *      illegal layout x loc, footprint over the lane window / global
 *      capacity)      -> violation
 *   B  operand semantic violations (dtype pairing, shape mismatch, attr
 *      ranges, mutually exclusive flags, unsupported mode / encoding)
 *      -> violation
 *   C  specified boundary behavior (gather/scatter OOB fill/drop,
 *      saturating encodes)                 -> handled per spec, silent
 *
 * A violation reports the instruction name, the check that failed and
 * the offending values through the registered handler; the default
 * handler prints to stderr and aborts. Tests install a capturing
 * handler (see smoke.h) that longjmps back, which is safe because
 * every check runs BEFORE any side effect (validate-first discipline).
 *
 * The macros end in `return` and therefore require a void function —
 * every engine instruction and validation helper qualifies. The return
 * is unreachable with well-behaved handlers and exists only as a
 * fail-safe against a handler that returns.
 * ========================================================================== */
#ifndef TN_REF_VIOLATE_H
#define TN_REF_VIOLATE_H

#include <stddef.h>

#include "tensor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* violation sink; NULL restores the default (print to stderr + abort) */
typedef void (*tnkr_ref_violation_fn)(const char *op, const char *file, int line, const char *msg);
void tnkr_ref_set_violation_handler(tnkr_ref_violation_fn fn);

/* format + dispatch; the default handler aborts, so this call does not
 * return under the default policy */
void tnkr_ref_violation(const char *op, const char *file, int line, const char *fmt, ...);

/* Report a violation and leave the enclosing void function. */
#define TNKR_REF_VIOLATE(op, ...)                                                                  \
    do {                                                                                           \
        tnkr_ref_violation(op, __FILE__, __LINE__, __VA_ARGS__);                                   \
        return; /* fail-safe: handlers abort or longjmp */                                         \
    } while (0)

/* Guarded check: cond false -> violation. */
#define TNKR_REF_CHECK(op, cond, ...)                                                              \
    do {                                                                                           \
        if (!(cond))                                                                               \
            TNKR_REF_VIOLATE(op, __VA_ARGS__);                                                     \
    } while (0)

/* Resolve one operand; a failed resolve is a violation carrying the
 * status name. view must be an lvalue tnkr_view_t. */
#define TNKR_REF_RESOLVE(ctx, view, di, si, op, name)                                              \
    do {                                                                                           \
        tnkr_resolve_status_t tnkr__st = tnkr_view_resolve(ctx, di, si, view);                     \
        if (tnkr__st != TNKR_RESOLVE_OK)                                                           \
            TNKR_REF_VIOLATE(op, "operand '%s' unresolvable: %s", name,                            \
                             tnkr_resolve_status_name(tnkr__st));                                  \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* TN_REF_VIOLATE_H */
