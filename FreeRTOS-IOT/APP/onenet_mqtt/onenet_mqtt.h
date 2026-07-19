#ifndef _ONENET_MQTT_H
#define _ONENET_MQTT_H

#include "stm32f1xx_hal.h"
#include "app_config.h"

/*
 * OneNET MQTT 配置占位。
 * 拿到正式 MQTT 接入参数后，只需修改下面宏即可。
 */
#ifndef ONENET_MQTT_BROKER_HOST
#define ONENET_MQTT_BROKER_HOST      "183.230.40.39"
#endif

#ifndef ONENET_MQTT_BROKER_PORT
#define ONENET_MQTT_BROKER_PORT      6002
#endif

#ifndef ONENET_MQTT_PRODUCT_ID
#define ONENET_MQTT_PRODUCT_ID       "CHANGE_ME_PRODUCT_ID"
#endif

#ifndef ONENET_MQTT_DEVICE_NAME
#define ONENET_MQTT_DEVICE_NAME      "CHANGE_ME_DEVICE_NAME"
#endif

#ifndef ONENET_MQTT_CLIENT_ID
#define ONENET_MQTT_CLIENT_ID        ONENET_MQTT_DEVICE_NAME
#endif

#ifndef ONENET_MQTT_USERNAME
#define ONENET_MQTT_USERNAME         ONENET_MQTT_PRODUCT_ID
#endif

#ifndef ONENET_MQTT_PASSWORD
#define ONENET_MQTT_PASSWORD         "CHANGE_ME_PASSWORD"
#endif

#ifndef ONENET_MQTT_CMD_TOPIC
#define ONENET_MQTT_CMD_TOPIC        "$creq/+/cmd"
#endif

#ifndef ONENET_MQTT_STATE_TOPIC
#define ONENET_MQTT_STATE_TOPIC      "$dp"
#endif

#ifndef ONENET_MQTT_KEEPALIVE_SEC
#define ONENET_MQTT_KEEPALIVE_SEC    60
#endif

#ifndef ONENET_MQTT_PING_INTERVAL_MS
#define ONENET_MQTT_PING_INTERVAL_MS 30000
#endif

typedef enum
{
    ONENET_MQTT_DISCONNECTED = 0,
    ONENET_MQTT_CONNECTED
} onenet_mqtt_status_t;

void OneNET_MQTT_Reset(void);
onenet_mqtt_status_t OneNET_MQTT_GetStatus(void);
uint8_t OneNET_MQTT_Tick(void);
uint8_t OneNET_MQTT_PauseForHttp(void);
uint8_t OneNET_MQTT_ResumeAfterHttp(void);
uint8_t OneNET_MQTT_PublishSwitchState(void);
uint8_t OneNET_MQTT_PublishTempHumi(int16_t temp_x10, uint16_t humi_x10);

#endif



