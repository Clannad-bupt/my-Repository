/*
 * mini_libc.h — the fw's bundled minimal libc (newlib-free across the tree)
 *
 * All fws are built with the Xuantie Linux toolchain and -nostdlib (the
 * toolchain serves only as compiler/linker, no host libc is linked), so the
 * base functions the fw needs ship their own implementations. printf writes
 * straight to the UART char by char, unbuffered, no heap — used only for the
 * startup banner and cold selftest paths; printf is forbidden in the dispatch
 * main loop (discipline). Implementations were split per fw (2026-10-30):
 * the ap version (src/ap/) carries a line-level spinlock (worker tasks print
 * concurrently), the scalar version (src/scalar/) is lock-free (output
 * collapsed to core0 alone); see each .c file's header.
 *
 * printf supports the subset: %u %d %x %X %s %c %%, flags '-'/'0', decimal
 * width, length modifiers l/ll/z (64-bit, added 2026-10-31: integration logs
 * need to print 64-bit addresses/masks).
 */
#ifndef MINI_LIBC_H
#define MINI_LIBC_H

#include <stddef.h>

int printf(const char *fmt, ...);
void *memset(void *s, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
size_t strlen(const char *s);

#endif /* MINI_LIBC_H */
