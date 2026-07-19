#ifndef __UART3_H
#define __UART3_H

#include <stdio.h>	
#include "main.h" 

#define EN_USART3_RX 		1		//使能3，关闭0，目前为1。

#define u3_printf printf

#define	DMA_UART3_RX_SIZE	2048
#define	DMA_UART3_TX_SIZE	256

// 485控制管脚
#define RS485_TX_EN() HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET)
#define RS485_RX_EN() HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET)

/* 发送缓存区+接收缓冲区大小定义+接收标志 */
extern uint8_t Uart3_RxBuff[DMA_UART3_RX_SIZE];
extern uint8_t Uart3_TxBuff[DMA_UART3_TX_SIZE];
extern uint16_t Uart3_RxLength;	//Uart3 DMA 接收到的数据长度

typedef struct
{
	uint16_t length;
	uint8_t data[DMA_UART3_RX_SIZE];
} Uart3_Frame_t;


void uart3_dma_rx_configuration(void);
uint16_t Uart3_DMA_Send_Data(void * buffer, uint16_t size);
uint16_t Uart3_GetFrame(uint8_t *buffer, uint16_t max_size, uint32_t timeout_ms);

void USART3_sendonechar(uint8_t data);
void USART3_sendstring(char *data, uint16_t length);

#endif
