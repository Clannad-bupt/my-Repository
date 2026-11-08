/*
 * mini_libc.h — the fw's own minimal libc (dropping newlib tree-wide)
 *
 * The fw is built with the Xuantie Linux toolchain and -nostdlib (the
 * toolchain serves only as compiler/linker, linking no host libc), so the
 * basics the fw needs ship as local implementations. printf writes to the
 * UART character by character, no buffering, no heap — only for the boot
 * banner and cold selftest paths; printf is forbidden in the dispatch main
 * loop (discipline). Implementations split per fw (2026-10-30): the ap version
 * (fw/ap/) carries a line-level spinlock (worker tasks print concurrently);
 * the scalar version (fw/scalar/) has no lock (output funnels through the
 * single core0); see each .c file header.
 *
 * printf subset: %u %d %x %X %s %c %%, flags '-'/'0', decimal width, length
 * modifiers l/ll/z (64-bit, added 2026-10-31: integration-debug logs needed
 * to print 64-bit addresses/masks).
 */
#ifndef MINI_LIBC_H
#define MINI_LIBC_H

#include <stddef.h>

int printf(const char *fmt, ...);
void *memset(void *s, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
size_t strlen(const char *s);

#endif /* MINI_LIBC_H */
