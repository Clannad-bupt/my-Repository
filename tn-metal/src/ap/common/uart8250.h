/* SPDX-License-Identifier: BSD-2-Clause */

#ifndef __DRIVERS_UART8250_H__
#define __DRIVERS_UART8250_H__

/** Init the UART: program the baud divisor, 8N1, enable FIFO */
void uart8250_init(unsigned long base, unsigned int in_freq, unsigned int baudrate,
                   unsigned int reg_shift, unsigned int reg_width);
/** Send one character (blocks until the transmit holding register is free) */
void uart8250_putc(char c);
/** Send a NUL-terminated string */
void uart8250_puts(const char *s);
/** Receive one character if available, -1 otherwise */
int uart8250_getc(void);

#endif
