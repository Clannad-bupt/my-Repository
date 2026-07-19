#ifndef __UART2_H
#define __UART2_H
#include <stdio.h>	
#include "main.h" 

#define EN_USART2_RX 			1

/*
 * 2 = 原 IOT 工程 / uart2_utf8.txt：USART2 PA2/PA3
 * 3 = 野火霸道 V2 板载 ESP8266 跳线：USART3 PB10/PB11
 */
#ifndef WIFI_UART_INSTANCE
#define WIFI_UART_INSTANCE      3
#endif

#define u2_printf printf2

#define USART2_RXBUFF_SIZE 2048
#ifndef UART2_FRAME_IDLE_MS
#define UART2_FRAME_IDLE_MS     80U
#endif
extern uint16_t Usart2_RxCounter;
extern uint8_t Usart2_RxBuff[USART2_RXBUFF_SIZE];

void USART2_Clear_RxBuff(void);
void Uart2_BeginReceiveFrame(void);
uint8_t Uart2_HasRxOverflow(void);
uint16_t Uart2_GetAggregateLen(void);
const char *Uart2_GetAggregatePtr(void);
void Uart2_ConsumeAggregate(uint16_t len);
void USART2_sendonechar(uint8_t data);
void USART2_sendstring(char *data, uint16_t length);
void printf2(char * fmt,...);
void uart2_rx_configuration(void);
void Uart2_PollRx(void);
void Uart2_OnRxByte(uint8_t data);
void Uart2_RxIrqHandler(UART_HandleTypeDef *huart);
void Uart2_OnRxIrqFromIsr(void);
void Uart2_ListenProbe(uint32_t listen_ms);
void Uart2_LoopbackTest(void);
uint32_t Uart2_GetRxIrqCount(void);
void Uart2_UartDiag(const char *tag);

#endif

