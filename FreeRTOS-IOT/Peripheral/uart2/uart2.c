#include "uart2.h"
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "structure.h"
#include "uart1.h"
#include "delay.h"

extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;
extern QueueHandle_t Wifi_Message_Queue;
extern char RXbuff[2048];
extern struct xRecMsg WifiMsg;

#if (WIFI_UART_INSTANCE == 3)
#define WIFI_HUART      huart3
#define WIFI_USART      USART3
#else
#define WIFI_HUART      huart2
#define WIFI_USART      USART2
#endif

uint16_t Usart2_RxCounter = 0;
uint8_t Usart2_RxBuff[USART2_RXBUFF_SIZE];

static uint16_t Uart2_AggregateLen = 0;
static uint8_t Uart2_RxOverflow = 0;
static volatile uint32_t Uart2_RxIrqCount = 0;
static uint32_t Uart2_LastRxTick = 0;
static UART_HandleTypeDef *wifi_uart_handle(void);
static void uart2_wifi_gpio_force(void);
static void uart2_wifi_hw_prepare(UART_HandleTypeDef *huart);
static uint8_t uart2_http_response_complete(void);
static UART_HandleTypeDef *wifi_uart_handle(void)
{
    return &WIFI_HUART;
}

static void uart2_wifi_tx_byte(uint8_t data)
{
    while((WIFI_USART->SR & USART_SR_TXE) == 0U)
    {
    }
    WIFI_USART->DR = data;
    while((WIFI_USART->SR & USART_SR_TC) == 0U)
    {
    }
}

#if (WIFI_UART_INSTANCE == 3)
static void uart2_gpio_short_test(void)
{
    GPIO_InitTypeDef gpio = {0};
    GPIO_PinState pb11_hi;
    GPIO_PinState pb11_lo;

    CLEAR_BIT(USART3->CR1, USART_CR1_UE);

    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);

    gpio.Pin = GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &gpio);

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_SET);
    HAL_Delay(1);
    pb11_hi = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_11);

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET);
    HAL_Delay(1);
    pb11_lo = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_11);

    if((pb11_hi == GPIO_PIN_SET) && (pb11_lo == GPIO_PIN_RESET))
    {
        u1_printf("GPIO short PB11: hi=1 lo=0 (PB10-PB11 path OK)\r\n");
    }
    else if((pb11_hi == GPIO_PIN_SET) && (pb11_lo == GPIO_PIN_SET))
    {
        u1_printf("GPIO PB11: hi=1 lo=1 (normal: module on OR no short)\r\n");
        u1_printf("Loopback: remove signal caps, short top-row PB10-PB11 (not W pins)\r\n");
    }
    else
    {
        u1_printf("GPIO short PB11: hi=%u lo=%u (check wiring)\r\n",
                  (unsigned)(pb11_hi == GPIO_PIN_SET ? 1U : 0U),
                  (unsigned)(pb11_lo == GPIO_PIN_SET ? 1U : 0U));
    }

    uart2_wifi_gpio_force();
    uart2_wifi_hw_prepare(wifi_uart_handle());
}
#endif

static void uart2_wifi_gpio_force(void)
{
#if (WIFI_UART_INSTANCE == 3)
    uint32_t crh;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();

    /* 野火 uart3_utf8: PB10=AF_PP, PB11=IPU (F103 CRH nibble 0xB / 0x8) */
    crh = GPIOB->CRH;
    crh &= ~((0xFUL << 8) | (0xFUL << 12));
    crh |= (0xBUL << 8) | (0x8UL << 12);
    GPIOB->CRH = crh;
#endif
}

static void uart2_wifi_hw_prepare(UART_HandleTypeDef *huart)
{
    if(huart == NULL || huart->Instance != WIFI_USART)
    {
        return;
    }

    uart2_wifi_gpio_force();

    (void)HAL_UART_AbortReceive(huart);

    if(huart->hdmarx != NULL)
    {
        (void)HAL_DMA_Abort(huart->hdmarx);
        __HAL_DMA_DISABLE(huart->hdmarx);
    }

    CLEAR_BIT(huart->Instance->CR3, USART_CR3_DMAR | USART_CR3_DMAT | USART_CR3_EIE);
    __HAL_UART_DISABLE_IT(huart, UART_IT_IDLE);
    SET_BIT(huart->Instance->CR1, USART_CR1_UE | USART_CR1_TE | USART_CR1_RE);
    __HAL_UART_ENABLE_IT(huart, UART_IT_RXNE);

    if(__HAL_UART_GET_FLAG(huart, UART_FLAG_RXNE) != RESET)
    {
        (void)huart->Instance->DR;
    }
    if(__HAL_UART_GET_FLAG(huart, UART_FLAG_ORE) != RESET)
    {
        __HAL_UART_CLEAR_OREFLAG(huart);
    }

    huart->RxState = HAL_UART_STATE_READY;
    huart->gState = HAL_UART_STATE_READY;
}

static uint8_t uart2_http_response_complete(void)
{
    const char *header_end;
    const char *content_len;
    const char *transfer_encoding;
    uint32_t body_len;
    uint32_t header_len;

    if(RXbuff[0] == '\0' || strstr(RXbuff, "HTTP/") == NULL)
    {
        return 0U;
    }

    header_end = strstr(RXbuff, "\r\n\r\n");
    if(header_end == NULL)
    {
        return 0U;
    }

    header_len = (uint32_t)(header_end - RXbuff) + 4U;
    content_len = strstr(RXbuff, "Content-Length:");
    if(content_len == NULL)
    {
        content_len = strstr(RXbuff, "content-length:");
    }

    if(content_len != NULL)
    {
        body_len = (uint32_t)strtoul(content_len + 15, NULL, 10);
        return (Uart2_AggregateLen >= (header_len + body_len)) ? 1U : 0U;
    }

    transfer_encoding = strstr(RXbuff, "Transfer-Encoding:");
    if(transfer_encoding == NULL)
    {
        transfer_encoding = strstr(RXbuff, "transfer-encoding:");
    }
    if(transfer_encoding != NULL && strstr(transfer_encoding, "chunked") != NULL)
    {
        return (strstr(header_end + 4, "\r\n0\r\n\r\n") != NULL) ? 1U : 0U;
    }

    return (strstr(header_end + 4, "CLOSED") != NULL) ? 1U : 0U;
}
static void uart2_store_byte(uint8_t data)
{
    if(Usart2_RxCounter >= (USART2_RXBUFF_SIZE - 1U))
    {
        Uart2_RxOverflow = 1;
        return;
    }

    Usart2_RxBuff[Usart2_RxCounter++] = data;
    Usart2_RxBuff[Usart2_RxCounter] = '\0';

    if(Uart2_AggregateLen < 2047U)
    {
        RXbuff[Uart2_AggregateLen++] = (char)data;
        RXbuff[Uart2_AggregateLen] = '\0';
    }
    else
    {
        Uart2_RxOverflow = 1;
    }

    WifiMsg.RxFlag = 1;
    WifiMsg.RxNum = Usart2_RxCounter;
    Uart2_LastRxTick = HAL_GetTick();

    if(uart2_http_response_complete())
    {
        WifiMsg.U2_RxCompleted = 1U;
    }
}

void uart2_rx_configuration(void)
{
    uart2_wifi_hw_prepare(wifi_uart_handle());
    USART2_Clear_RxBuff();
}

void USART2_Clear_RxBuff(void)
{
    memset(Usart2_RxBuff, 0, USART2_RXBUFF_SIZE);
    Usart2_RxCounter = 0;
    Uart2_AggregateLen = 0;
    Uart2_RxOverflow = 0;
    memset(RXbuff, 0, 2048);
    WifiMsg.RxFlag = 0;
    WifiMsg.RxNum = 0;
    WifiMsg.U2_RxCompleted = 0;
    Uart2_LastRxTick = HAL_GetTick();
    uart2_wifi_hw_prepare(wifi_uart_handle());
}

void Uart2_BeginReceiveFrame(void)
{
    USART2_Clear_RxBuff();
}

void Uart2_OnRxByte(uint8_t data)
{
    uart2_store_byte(data);
}

void Uart2_RxIrqHandler(UART_HandleTypeDef *huart)
{
    if(huart == NULL || huart->Instance != WIFI_USART)
    {
        return;
    }

    while(__HAL_UART_GET_FLAG(huart, UART_FLAG_RXNE) != RESET)
    {
        uint8_t data = (uint8_t)(huart->Instance->DR & 0xFFU);
        Uart2_OnRxByte(data);
    }

    if(__HAL_UART_GET_FLAG(huart, UART_FLAG_ORE) != RESET)
    {
        if(__HAL_UART_GET_FLAG(huart, UART_FLAG_RXNE) != RESET)
        {
            uint8_t data = (uint8_t)(huart->Instance->DR & 0xFFU);
            Uart2_OnRxByte(data);
        }
        __HAL_UART_CLEAR_OREFLAG(huart);
    }
}

void Uart2_OnRxIrqFromIsr(void)
{
    Uart2_RxIrqCount++;
    Uart2_RxIrqHandler(wifi_uart_handle());
}

uint32_t Uart2_GetRxIrqCount(void)
{
    return Uart2_RxIrqCount;
}

void Uart2_UartDiag(const char *tag)
{
    UART_HandleTypeDef *huart = wifi_uart_handle();
    u1_printf("%s CR1=0x%04lX CR3=0x%04lX(EIE=%u DMAR=%u) SR=0x%04lX RXNE=%u isr=%lu raw=%u\r\n",
              tag,
              (unsigned long)huart->Instance->CR1,
              (unsigned long)huart->Instance->CR3,
              (unsigned)((huart->Instance->CR3 & USART_CR3_EIE) ? 1U : 0U),
              (unsigned)((huart->Instance->CR3 & USART_CR3_DMAR) ? 1U : 0U),
              (unsigned long)huart->Instance->SR,
              (unsigned)((huart->Instance->SR & USART_SR_RXNE) ? 1U : 0U),
              (unsigned long)Uart2_RxIrqCount,
              (unsigned)Usart2_RxCounter);
}

void Uart2_PollRx(void)
{
    Uart2_RxIrqHandler(wifi_uart_handle());
    if(WifiMsg.U2_RxCompleted == 0U &&
       Uart2_AggregateLen > 0U &&
       (HAL_GetTick() - Uart2_LastRxTick) >= UART2_FRAME_IDLE_MS)
    {
        WifiMsg.U2_RxCompleted = 1U;
    }
}

void Uart2_ListenProbe(uint32_t listen_ms)
{
    uint32_t elapsed = 0U;

    uart2_wifi_gpio_force();
    uart2_wifi_hw_prepare(wifi_uart_handle());
    USART2_Clear_RxBuff();

    u1_printf("Listen %lums on %s (module boot)...\r\n",
              (unsigned long)listen_ms,
#if (WIFI_UART_INSTANCE == 3)
              "USART3 PB11"
#else
              "USART2 PA3"
#endif
              );

    while(elapsed < listen_ms)
    {
        Uart2_PollRx();
        HAL_Delay(10);
        elapsed += 10U;
    }

    Uart2_PollRx();
    if(Usart2_RxCounter > 0U)
    {
        u1_printf("Module RX %u bytes: %s\r\n",
                  (unsigned)Usart2_RxCounter, (char*)Usart2_RxBuff);
    }
    else
    {
        u1_printf("Module RX 0 bytes -> check jumpers PB10-W_RX PB11-W_TX\r\n");
    }
}

void Uart2_LoopbackTest(void)
{
    uint8_t i;
    uint32_t crh;
    uint32_t sr_after_tx;

#if (WIFI_UART_INSTANCE == 3)
    uart2_gpio_short_test();
#endif

    uart2_wifi_gpio_force();
    uart2_wifi_hw_prepare(wifi_uart_handle());
    USART2_Clear_RxBuff();

    for(i = 0; i < 8U; i++)
    {
        uart2_wifi_tx_byte((uint8_t)('0' + i));
        Uart2_PollRx();
    }

    HAL_Delay(20);
    Uart2_PollRx();

    sr_after_tx = WIFI_USART->SR;

#if (WIFI_UART_INSTANCE == 3)
    crh = GPIOB->CRH;
    u1_printf("PB10/PB11 CRH TX=0x%lX RX=0x%lX SR=0x%04lX\r\n",
              (unsigned long)((crh >> 8) & 0xFUL),
              (unsigned long)((crh >> 12) & 0xFUL),
              (unsigned long)sr_after_tx);
#endif
    if(Usart2_RxCounter == 8U)
    {
        u1_printf("USART3 loopback raw=8 (TX-RX path OK)\r\n");
    }
    else
    {
        u1_printf("USART3 loopback raw=%u (remove caps, short top PB10-PB11, 3V3 OFF)\r\n",
                  (unsigned)Usart2_RxCounter);
    }
}

uint8_t Uart2_HasRxOverflow(void)
{
    return Uart2_RxOverflow;
}

uint16_t Uart2_GetAggregateLen(void)
{
    return Uart2_AggregateLen;
}

const char *Uart2_GetAggregatePtr(void)
{
    return RXbuff;
}

void Uart2_ConsumeAggregate(uint16_t len)
{
    if(len == 0U)
    {
        return;
    }

    if(len >= Uart2_AggregateLen)
    {
        Uart2_AggregateLen = 0U;
        RXbuff[0] = '\0';
        Usart2_RxCounter = 0U;
        Usart2_RxBuff[0] = '\0';
        return;
    }

    memmove(RXbuff, &RXbuff[len], Uart2_AggregateLen - len);
    Uart2_AggregateLen = (uint16_t)(Uart2_AggregateLen - len);
    RXbuff[Uart2_AggregateLen] = '\0';
    Usart2_RxCounter = Uart2_AggregateLen;
    memcpy(Usart2_RxBuff, RXbuff, Usart2_RxCounter + 1U);
}

void USART2_sendonechar(uint8_t data)
{
    uart2_wifi_tx_byte(data);
}

void USART2_sendstring(char *data, uint16_t length)
{
    uint16_t i;

    for(i = 0; i < length; i++)
    {
        uart2_wifi_tx_byte((uint8_t)data[i]);
    }
}

void printf2(char * fmt,...)
{
    char buffer[256];
    uint16_t i = 0;
    va_list arg_ptr;
    va_start(arg_ptr, fmt);
    vsnprintf(buffer, 256, fmt, arg_ptr);
    va_end(arg_ptr);

    while(i < 256 && buffer[i])
    {
        uart2_wifi_tx_byte((uint8_t)buffer[i]);
        i++;
    }
}


