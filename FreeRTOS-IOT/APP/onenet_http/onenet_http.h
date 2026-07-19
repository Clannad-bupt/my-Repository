/*-------------------------------------------------*/
/*            超纬电子STM32系列开发板              */
/*-------------------------------------------------*/
/*                                                 */
/*            onenet_http功能的头文件              */
/*                                                 */
/*-------------------------------------------------*/

#ifndef _ONENET_HTTP_H
#define _ONENET_HTTP_H

#include "stm32f1xx_hal.h"
#include "app_config.h"

#define SWITCH_DID         ONENET_SWITCH_DID	//4路开关，产品ID
#define TEMPHUMI_DID       ONENET_TEMPHUMI_DID
#define API_KEY            ONENET_API_KEY//产品的Master-APIkey，何时查询都是不变的

extern char *ServerIP;         //存放服务器IP或是域名
extern int  ServerPort;        //存放服务器的端口号区
extern char RXbuff[2048];      //接收数据缓冲区
extern char TXbuff[2048];      //发送数据缓冲区

void OneNet_GET(char *device_id);
void OneNet_POST(char *device_id);
void OneNet_POST_TempHumi(char *device_id, int16_t temp_x10, uint16_t humi_x10);

void OneNET_GetSwitch(void); /* legacy HTTP polling, replaced by OneNET MQTT */
uint8_t OneNET_PostTempHumi(int16_t temp_x10, uint16_t humi_x10);
	
#endif



