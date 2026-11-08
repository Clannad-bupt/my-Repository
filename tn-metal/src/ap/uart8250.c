/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Minimal DW 8250 (ns16550-compatible) UART driver, polling mode — ap fw version
 *
 * Modeled on the OpenSBI uart8250 driver (lib/utils/serial/uart8250.c),
 * reduced to what a boot ROM needs: init (baud divisor, 8N1, FIFO),
 * polled putc/puts/getc. No interrupts, no console framework. Register
 * N sits at base + (N << reg_shift) and is accessed reg_width wide.
 *
 * Same API as the scalar fw version (fw/scalar/uart8250.c) but a different
 * concurrency strategy: the AP fw has no tp/per-CPU data (TLS accesses would
 * fault); it has the release gate (fw/ap/start.S — only core0 moves .data /
 * zeroes .bss, other cores only touch the shared areas after seeing the
 * magic) → by the time anyone prints, the shared-.bss dev is stable and
 * nobody zeroes it again, so sharing is safe.
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

/* shared global (not TLS): the AP fw has no tp (see the file header); the
   release gate guarantees .bss is stable before any printing */
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
