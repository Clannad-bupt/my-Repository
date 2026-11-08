/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Minimal DW 8250 (ns16550-compatible) UART driver, polling mode — scalar fw version
 *
 * Modeled on the OpenSBI uart8250 driver (lib/utils/serial/uart8250.c),
 * reduced to what a boot ROM needs: init (baud divisor, 8N1, FIFO),
 * polled putc/puts/getc. No interrupts, no console framework. Register
 * N sits at base + (N << reg_shift) and is accessed reg_width wide.
 *
 * Same API as the ap fw version (src/ap/uart8250.c), different concurrency
 * policy:
 *   - output collapsed to core0 alone (src/scalar/main.c — other cores call
 *     no output function) → this driver is lock-free;
 *   - uart8250_dev is shared .bss — the safety precondition is the start.S
 *     release gate (only core0 copies .data / clears .bss): by the time core0
 *     first prints, .bss is fully cleared and nobody clears it again.
 *     It used to be 8 cores concurrently clearing .bss; a lagging core zeroed
 *     the printing core's dev->base → get_reg computed address 0x5 → load
 *     access fault → park silent death (2026-10-30 live: only 1 of 8 cores
 *     survived).
 */

#include "uart8250.h"

#define UART_RBR 0 /* In:  receive buffer */
#define UART_THR 0 /* Out: transmit holding */
#define UART_DLL 0 /* Out: divisor latch low */
#define UART_IER 1 /* I/O: interrupt enable */
#define UART_DLM 1 /* Out: divisor latch high */
#define UART_FCR 2 /* Out: FIFO control */
#define UART_LCR 3 /* Out: line control */
#define UART_MCR 4 /* Out: modem control */
#define UART_LSR 5 /* In:  line status */
#define UART_SCR 7 /* I/O: scratch */

#define UART_LCR_DLAB 0x80 /* divisor latch access bit */
#define UART_LSR_TEMT 0x40 /* transmitter empty */
#define UART_LSR_THRE 0x20 /* transmit holding empty */
#define UART_LSR_DR 0x01   /* receive data ready */

struct uart8250_device {
    volatile void *base;
    unsigned int reg_shift;
    unsigned int reg_width;
};

/* shared global (.bss): read/written by core0 only (output discipline + release gate, see file
 * header) */
static struct uart8250_device uart8250_dev;

static unsigned int get_reg(struct uart8250_device *dev, unsigned int num) {
    volatile unsigned char *addr =
        (volatile unsigned char *)dev->base + ((unsigned long)num << dev->reg_shift);

    if (dev->reg_width == 1)
        return *addr;
    if (dev->reg_width == 2)
        return *(volatile unsigned short *)addr;
    return *(volatile unsigned int *)addr;
}

static void set_reg(struct uart8250_device *dev, unsigned int num, unsigned int val) {
    volatile unsigned char *addr =
        (volatile unsigned char *)dev->base + ((unsigned long)num << dev->reg_shift);

    if (dev->reg_width == 1)
        *addr = val;
    else if (dev->reg_width == 2)
        *(volatile unsigned short *)addr = val;
    else
        *(volatile unsigned int *)addr = val;
}

void uart8250_init(unsigned long base, unsigned int in_freq, unsigned int baudrate,
                   unsigned int reg_shift, unsigned int reg_width) {
    struct uart8250_device *dev = &uart8250_dev;
    unsigned int bdiv = 0;

    dev->base = (volatile void *)base;
    dev->reg_shift = reg_shift;
    dev->reg_width = reg_width;

    if (baudrate)
        bdiv = (in_freq + 8 * baudrate) / (16 * baudrate);

    /* disable all interrupts */
    set_reg(dev, UART_IER, 0x00);
    /* enable DLAB and program the divisor */
    set_reg(dev, UART_LCR, UART_LCR_DLAB);
    if (bdiv) {
        set_reg(dev, UART_DLL, bdiv & 0xff);
        set_reg(dev, UART_DLM, (bdiv >> 8) & 0xff);
    }
    /* 8 data bits, no parity, one stop bit */
    set_reg(dev, UART_LCR, 0x03);
    /* enable FIFO */
    set_reg(dev, UART_FCR, 0x01);
    /* no modem control (DTR/RTS) */
    set_reg(dev, UART_MCR, 0x00);
    /* clear any pending receive byte */
    if (get_reg(dev, UART_LSR) & UART_LSR_DR)
        get_reg(dev, UART_RBR);
}

void uart8250_putc(char c) {
    while (!(get_reg(&uart8250_dev, UART_LSR) & UART_LSR_THRE))
        ;
    set_reg(&uart8250_dev, UART_THR, (unsigned char)c);
}

int uart8250_getc(void) {
    if (get_reg(&uart8250_dev, UART_LSR) & UART_LSR_DR)
        return get_reg(&uart8250_dev, UART_RBR);
    return -1;
}

void uart8250_puts(const char *s) {
    while (*s)
        uart8250_putc(*s++);
}
