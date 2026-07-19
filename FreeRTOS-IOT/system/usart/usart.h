#ifndef __USART_SYS_H
#define __USART_SYS_H

#include "sys.h"
#include <stdio.h>

/* printf 重定向到 UART1（HAL 版本）*/
void debug_usart_send_string(const char *str);

#endif
