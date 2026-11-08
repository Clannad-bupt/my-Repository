// Copyright (c) 2026 ThinkNative. All rights reserved.

/* ==========================================================================
 * tn-metal: src/kernel/kernel_backend.h
 *
 * Backend selection switch (internal header — only src/kernel/<backend>/
 * implementations include it; including it without a backend macro is a
 * hard error, user code must not touch it)
 *
 * Usage: define exactly one at compile time
 *   -DTN_BACKEND_REF      pure-software C++ reference (golden model;
 *                         operator semantics are defined here)
 *   -DTN_BACKEND_CHIP     chip implementation (instruction issue path)
 *
 * Rule: macros only select which instruction semantics a build compiles;
 *       implementations live in src/kernel/<backend>/ and share the same
 *       signatures in include/kernel/kernel.h. There is no runtime
 *       backend interface — the kernel library on the chip is the chip
 *       build, the host-side golden model is the ref build.
 * ========================================================================== */
#ifndef TN_KERNEL_BACKEND_H
#define TN_KERNEL_BACKEND_H

#if (defined(TN_BACKEND_REF) ? 1 : 0) + (defined(TN_BACKEND_CHIP) ? 1 : 0) > 1
#error "tn-metal: only one backend (TN_BACKEND_REF/CHIP)"
#elif !defined(TN_BACKEND_REF) && !defined(TN_BACKEND_CHIP)
#error "tn-metal: backend undefined TN_BACKEND_REF / TN_BACKEND_CHIP"
#endif

#endif /* TN_KERNEL_BACKEND_H */
