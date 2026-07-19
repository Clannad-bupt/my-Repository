#include "usart.h"
#include "stm32f1xx_hal.h"

/*
 * printf 重定向到 UART1（HAL 版本）
 * 需要在 Keil 中勾选 Use MicroLIB
 */

extern UART_HandleTypeDef huart1;

void debug_usart_send_string(const char *str)
{
    if (str == NULL)
    {
        return;
    }

    while (*str != '\0')
    {
        (void)HAL_UART_Transmit(&huart1, (const uint8_t *)str, 1U, 1000U);
        str++;
    }
}

/* 重定向 fputc 到 UART1 */
int fputc(int ch, FILE *f)
{
    (void)f;
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 1000);
    return ch;
}

/* 重定向 fgetc 从 UART1 */
int fgetc(FILE *f)
{
    uint8_t ch = 0;
    (void)f;
    (void)HAL_UART_Receive(&huart1, &ch, 1, 1000);
    return (int)ch;
}
