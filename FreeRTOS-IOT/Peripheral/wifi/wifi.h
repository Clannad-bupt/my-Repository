/*-------------------------------------------------*/
/*            超纬电子STM32系列开发板              */
/*-------------------------------------------------*/
/*                                                 */
/*              操作Wifi功能的头文件               */
/*                                                 */
/*-------------------------------------------------*/

#ifndef __WIFI_H
#define __WIFI_H

#include "uart2.h"	       //包含需要的头文件
#include "app_config.h"

extern char Connect_flag;  //外部变量声明，同服务器连接状态  0：还没有连接服务器  1：连接上服务器了

#define RESET_IO(x)    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, (GPIO_PinState)(x))  //PA4控制WiFi的复位

#define WiFi_printf			printf2
#if (WIFI_UART_INSTANCE == 3)
#define WiFi_UART_DESC      "USART3 PB10/PB11"
#else
#define WiFi_UART_DESC      "USART2 PA2/PA3"
#endif
#define WiFi_RxCounter		Usart2_RxCounter
#define WiFi_RX_BUF			((char*)Usart2_RxBuff)
#define WiFi_RXBUFF_SIZE	USART2_RXBUFF_SIZE
#define Delay_Ms			delay_ms

#define SSID    WIFI_SSID
#define PASS    WIFI_PASS

void WiFi_ResetIO_Init(void);
char WiFi_SendCmd(char *cmd, int timeout);
char WiFi_Reset(int timeout);
char WiFi_JoinAP(int timeout);
char WiFi_Connect_TCP(const char *host, int port, int timeout);
char WiFi_Connect_Server(int timeout);
char WiFi_Smartconfig(int timeout);
char WiFi_WaitAP(int timeout);
char WiFi_GetIP(int timeout);
char WiFi_Get_LinkSta(void);
char WiFi_Get_Data(char *data, char *len, char *id);
char WiFi_SendData(char id, char *databuff, int data_len, int timeout);
char WiFi_Close(int timeout);
char WiFi_Init(void);

#endif





