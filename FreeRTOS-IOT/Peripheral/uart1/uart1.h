#ifndef __UART1_H
#define __UART1_H

#include <stdio.h>	
#include "main.h" 

#define EN_USART1_RX 		1		//使能1，关闭0，目前为1。

#define u1_printf printf

#define	DMA_UART1_RX_SIZE	128
#define	DMA_UART1_TX_SIZE	128

/* 发送缓存区+接收缓冲区大小定义+接收标志 */
extern uint8_t Uart1_RxBuff[DMA_UART1_RX_SIZE];
extern uint8_t Uart1_TxBuff[DMA_UART1_TX_SIZE];
extern uint16_t Uart1_RxLength;	//Uart1 DMA 接收到的数据长度

void uart1_dma_rx_configuration(void);
uint16_t Uart1_DMA_Send_Data(void * buffer, uint16_t size);

void USART1_sendonechar(uint8_t data);
void USART1_sendstring(char *data, uint16_t length);

#endif
