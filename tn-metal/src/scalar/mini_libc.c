/*
 * mini_libc.c — the fw's bundled minimal libc implementation (see the
 * mini_libc.h contract) — scalar fw version
 *
 * This file is compiled by the Makefile with -ffreestanding (implying
 * -fno-builtin): gcc will not recognize memset/memcpy loop idioms back into
 * calls to themselves (prevents infinite recursion).
 *
 * printf is lock-free: output is collapsed to core0 alone (src/scalar/main.c
 * — other cores call no output function). The former amoswap line-level
 * spinlock is gone — concurrent multi-core printing does not exist in the
 * scalar fw; and it never protected against the real killer (concurrent
 * .bss clearing zeroing the lock word/uart8250_dev, see the
 * src/scalar/uart8250.c header), which the start.S release gate fixed.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "mini_libc.h"
#include "uart8250.h"

static void put(char c) { uart8250_putc(c); }

static void pad(char c, int n) {
    while (n-- > 0)
        put(c);
}

static void putnum(uint64_t v, int base, int upper, int w, bool zero, bool left, bool neg) {
    char buf[24]; /* 2^64-1 is 22 octal digits, covers every base */
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    do {
        buf[n++] = dig[v % (uint64_t)base];
        v /= (uint64_t)base;
    } while (v);
    int total = n + (neg ? 1 : 0);

    if (left) {
        if (neg)
            put('-');
        while (n > 0)
            put(buf[--n]);
        pad(' ', w - total);
    } else if (zero) {
        if (neg)
            put('-');
        pad('0', w - total);
        while (n > 0)
            put(buf[--n]);
    } else {
        pad(' ', w - total);
        if (neg)
            put('-');
        while (n > 0)
            put(buf[--n]);
    }
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            put(*p);
            continue;
        }
        p++;
        if (*p == '%') {
            put('%');
            continue;
        }

        bool left = false, zero = false;
        while (*p == '-' || *p == '0') {
            if (*p == '-')
                left = true;
            else
                zero = true;
            p++;
        }
        int w = 0;
        while (*p >= '0' && *p <= '9') {
            w = w * 10 + (*p - '0');
            p++;
        }
        /* length modifiers: l/ll/z → 64-bit argument fetch (added
         * 2026-10-31: integration logs need %llx for 64-bit
         * addresses/masks); h/hh go through default promotion,
         * nothing to do */
        bool wide = false;
        while (*p == 'l' || *p == 'z') {
            wide = true;
            p++;
        }

        switch (*p) {
        case 'u':
            putnum(wide ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, unsigned int), 10, 0, w, zero,
                   left, false);
            break;
        case 'd': {
            int64_t v = wide ? (int64_t)va_arg(ap, long) : (int64_t)va_arg(ap, int);
            uint64_t a = v < 0 ? (uint64_t)(-v) : (uint64_t)v;
            putnum(a, 10, 0, w, zero, left, v < 0);
            break;
        }
        case 'x':
            putnum(wide ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, unsigned int), 16, 0, w, zero,
                   left, false);
            break;
        case 'X':
            putnum(wide ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, unsigned int), 16, 1, w, zero,
                   left, false);
            break;
        case 'c': {
            char c = (char)va_arg(ap, int);
            if (left) {
                put(c);
                pad(' ', w - 1);
            } else {
                pad(' ', w - 1);
                put(c);
            }
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            int len = (int)strlen(s);
            if (left) {
                while (*s)
                    put(*s++);
                pad(' ', w - len);
            } else {
                pad(' ', w - len);
                while (*s)
                    put(*s++);
            }
            break;
        }
        case '\0':
            va_end(ap);
            return 0; /* format string cut off at '%' */
        default:
            put('%');
            put(*p);
            break;
        }
    }
    va_end(ap);
    return 0;
}

void *memset(void *s, int c, size_t n) {
    unsigned char *d = s;
    while (n-- > 0)
        *d++ = (unsigned char)c;
    return s;
}

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n-- > 0)
        *d++ = *s++;
    return dst;
}

size_t strlen(const char *s) {
    const char *p = s;
    while (*p)
        p++;
    return (size_t)(p - s);
}
