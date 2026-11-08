/*
 * mini_libc.c — the fw's own minimal libc (see the mini_libc.h contract) — ap fw version
 *
 * This file is compiled by the Makefile with -ffreestanding (which implies
 * -fno-builtin): gcc will not recognize memset/memcpy loop idioms and turn
 * them back into calls to themselves (guards against infinite recursion).
 *
 * Differences from the scalar fw version (fw/scalar/mini_libc.c): the AP fw's
 * worker tasks print concurrently with core0 (ap-smoke's smp hello) → the
 * amoswap line-level spinlock stays. The lock word starts zeroed in .bss =
 * free; the AP fw has the release gate (core0 only lets go after zeroing
 * .bss) → printing only happens after .bss is stable; the lock word has no
 * concurrent-zeroing window.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "mini_libc.h"
#include "uart8250.h"

static void put(char c) { uart8250_putc(c); }

/* printf spinlock (multi-core concurrent printing interleaves at character
 * level — observed with the AP fw worker tasks really running): amoswap.w
 * (rv64gc has the A extension). uart8250_putc/puts take no lock themselves =
 * the bare debug escape hatch; use them concurrently at your own risk. */
static volatile uint32_t printf_lock;

static void printf_lock_take(void) {
    uint32_t prev;
    do {
        __asm__ volatile("amoswap.w.aq %0, %1, (%2)"
                         : "=r"(prev)
                         : "r"(1u), "r"(&printf_lock)
                         : "memory");
    } while (prev != 0u);
}

static void printf_lock_drop(void) {
    /* amoswap zero↔zero = a zeroing with release semantics (same trick as
 zsbl vector.S poll_core) */
    __asm__ volatile("amoswap.w.rl zero, zero, (%0)" ::"r"(&printf_lock) : "memory");
}

static void pad(char c, int n) {
    while (n-- > 0)
        put(c);
}

static void putnum(uint64_t v, int base, int upper, int w, bool zero, bool left, bool neg) {
    char buf[24]; /* 2^64-1 in octal is 22 digits, covers every base */
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

    printf_lock_take();
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
        /* length modifiers: l/ll/z → take the argument as 64-bit (added
         * 2026-10-31: integration-debug logs needed %llx for 64-bit
         * addresses/masks); h/hh use default promotion, nothing to do */
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
            printf_lock_drop();
            va_end(ap);
            return 0; /* format string truncated at '%' */
        default:
            put('%');
            put(*p);
            break;
        }
    }
    printf_lock_drop();
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
