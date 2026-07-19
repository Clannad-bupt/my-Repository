#include "onenet_mqtt.h"
#include "mqtt_client.h"
#include "wifi.h"
#include "uart2.h"
#include "uart1.h"
#include "led.h"
#include "lcd_ui.h"
#include "structure.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern struct xDevice Device;

typedef enum
{
    MQTT_SM_IDLE = 0,
    MQTT_SM_TCP_CONNECT,
    MQTT_SM_MQTT_CONNECT,
    MQTT_SM_SUBSCRIBE,
    MQTT_SM_RUNNING,
    MQTT_SM_RECONNECT_WAIT
} mqtt_sm_state_t;

static mqtt_sm_state_t s_state = MQTT_SM_IDLE;
static onenet_mqtt_status_t s_status = ONENET_MQTT_DISCONNECTED;
static uint8_t s_paused = 0;
static uint8_t s_fail_count = 0;
static uint32_t s_last_ping_ms = 0;
static uint32_t s_reconnect_after_ms = 0;
static uint16_t s_packet_id = 1;
static uint8_t s_tx_buf[MQTT_TX_BUFFER_SIZE];

static uint32_t mqtt_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static mqtt_err_t mqtt_send_bytes(const uint8_t *data, uint16_t len)
{
    if(data == NULL || len == 0U)
    {
        return MQTT_ERR_PARAM;
    }

    USART2_sendstring((char *)data, len);
    return MQTT_OK;
}

static void mqtt_apply_switch_value(uint8_t index, int value)
{
    switch(index)
    {
        case 1:
            Device.Led.Val1 = (value != 0) ? 1U : 0U;
            if(value != 0) { LED1_ON; lcd_ui_ShowLed(3, 1); }
            else { LED1_OFF; lcd_ui_ShowLed(3, 0); }
            break;
        case 2:
            Device.Led.Val2 = (value != 0) ? 1U : 0U;
            if(value != 0) { LED2_ON; lcd_ui_ShowLed(2, 1); }
            else { LED2_OFF; lcd_ui_ShowLed(2, 0); }
            break;
        case 3:
            Device.Led.Val3 = (value != 0) ? 1U : 0U;
            if(value != 0) { LED3_ON; lcd_ui_ShowLed(1, 1); }
            else { LED3_OFF; lcd_ui_ShowLed(1, 0); }
            break;
        case 4:
            Device.Led.Val4 = (value != 0) ? 1U : 0U;
            if(value != 0) { LED4_ON; lcd_ui_ShowLed(0, 1); }
            else { LED4_OFF; lcd_ui_ShowLed(0, 0); }
            break;
        default:
            break;
    }
}

static int mqtt_parse_json_switch(const char *payload, const char *key, int *value)
{
    char pattern[32];
    const char *p;
    char *endptr;

    if(payload == NULL || key == NULL || value == NULL)
    {
        return -1;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    p = strstr(payload, pattern);
    if(p == NULL)
    {
        return -1;
    }

    p = strchr(p, ':');
    if(p == NULL)
    {
        return -1;
    }

    p++;
    while(*p == ' ' || *p == '\t')
    {
        p++;
    }

    *value = (int)strtol(p, &endptr, 10);
    return 0;
}

static void mqtt_handle_switch_payload(const char *payload)
{
    int value = 0;
    uint8_t i;

    if(payload == NULL)
    {
        return;
    }

    u1_printf("MQTT cmd: %s\r\n", payload);

    for(i = 1; i <= 4U; i++)
    {
        char key[16];
        snprintf(key, sizeof(key), "switch_%u", i);
        if(mqtt_parse_json_switch(payload, key, &value) == 0)
        {
            mqtt_apply_switch_value(i, value);
        }
    }

    if(strstr(payload, "\"switch_1\"") != NULL && mqtt_parse_json_switch(payload, "switch_1", &value) != 0)
    {
        const char *swcStatus = strstr(payload, "\"id\":\"switch_1\"");
        if(swcStatus != NULL)
        {
            mqtt_apply_switch_value(1, (*(swcStatus - 4) != '0') ? 1 : 0);
        }
        swcStatus = strstr(payload, "\"id\":\"switch_2\"");
        if(swcStatus != NULL)
        {
            mqtt_apply_switch_value(2, (*(swcStatus - 4) != '0') ? 1 : 0);
        }
        swcStatus = strstr(payload, "\"id\":\"switch_3\"");
        if(swcStatus != NULL)
        {
            mqtt_apply_switch_value(3, (*(swcStatus - 4) != '0') ? 1 : 0);
        }
        swcStatus = strstr(payload, "\"id\":\"switch_4\"");
        if(swcStatus != NULL)
        {
            mqtt_apply_switch_value(4, (*(swcStatus - 4) != '0') ? 1 : 0);
        }
    }
}

static void mqtt_process_rx_packets(void)
{
    uint16_t agg_len;
    uint16_t pkt_len;
    mqtt_message_t msg;

    agg_len = Uart2_GetAggregateLen();
    while(agg_len >= 2U)
    {
        pkt_len = mqtt_packet_total_length((const uint8_t *)Uart2_GetAggregatePtr(), agg_len);
        if(pkt_len == 0U || agg_len < pkt_len)
        {
            break;
        }

        if(mqtt_parse_message((const uint8_t *)Uart2_GetAggregatePtr(), pkt_len, &msg) == MQTT_OK)
        {
            if(msg.type == MQTT_PKT_PUBLISH)
            {
                msg.payload[msg.payload_len] = '\0';
                mqtt_handle_switch_payload((char *)msg.payload);
            }
        }

        Uart2_ConsumeAggregate(pkt_len);
        agg_len = Uart2_GetAggregateLen();
    }
}

static mqtt_err_t mqtt_wait_for(uint8_t pkt_mask, uint32_t timeout_ms, mqtt_message_t *out_msg)
{
    uint32_t elapsed = 0U;

    while(elapsed < timeout_ms)
    {
        Uart2_PollRx();
        mqtt_process_rx_packets();

        if(Uart2_GetAggregateLen() >= 2U)
        {
            uint16_t pkt_len = mqtt_packet_total_length((const uint8_t *)Uart2_GetAggregatePtr(), Uart2_GetAggregateLen());
            if(pkt_len > 0U && Uart2_GetAggregateLen() >= pkt_len)
            {
                mqtt_message_t msg;
                if(mqtt_parse_message((const uint8_t *)Uart2_GetAggregatePtr(), pkt_len, &msg) == MQTT_OK)
                {
                    Uart2_ConsumeAggregate(pkt_len);
                    if(out_msg != NULL)
                    {
                        *out_msg = msg;
                    }

                    if((pkt_mask == 0x20U && msg.type == MQTT_PKT_CONNACK) ||
                       (pkt_mask == 0x90U && msg.type == MQTT_PKT_SUBACK) ||
                       (pkt_mask == 0xD0U && msg.type == MQTT_PKT_PINGRESP))
                    {
                        return MQTT_OK;
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
        elapsed += 10U;
    }

    return MQTT_ERR_TIMEOUT;
}

static uint8_t mqtt_do_connect(void)
{
    mqtt_connect_opts_t opts;
    mqtt_message_t msg;
    uint16_t tx_len = 0U;
    mqtt_err_t err;

    u1_printf("MQTT: connect TCP %s:%d\r\n", ONENET_MQTT_BROKER_HOST, ONENET_MQTT_BROKER_PORT);
    if(WiFi_Connect_TCP(ONENET_MQTT_BROKER_HOST, ONENET_MQTT_BROKER_PORT, 50) != 0)
    {
        u1_printf("MQTT: TCP connect failed\r\n");
        return 1;
    }

    Connect_flag = 1;
    WifiMsg.ConnectFlag = 1;
    Uart2_BeginReceiveFrame();

    memset(&opts, 0, sizeof(opts));
    strncpy(opts.client_id, ONENET_MQTT_CLIENT_ID, sizeof(opts.client_id) - 1U);
    strncpy(opts.username, ONENET_MQTT_USERNAME, sizeof(opts.username) - 1U);
    strncpy(opts.password, ONENET_MQTT_PASSWORD, sizeof(opts.password) - 1U);
    opts.keep_alive_sec = ONENET_MQTT_KEEPALIVE_SEC;
    opts.clean_session = 1U;

    err = mqtt_build_connect(&opts, s_tx_buf, sizeof(s_tx_buf), &tx_len);
    if(err != MQTT_OK)
    {
        return 2;
    }

    mqtt_send_bytes(s_tx_buf, tx_len);
    if(mqtt_wait_for(0x20U, 8000U, &msg) != MQTT_OK)
    {
        u1_printf("MQTT: CONNACK timeout, rx=%u\r\n", (unsigned)Uart2_GetAggregateLen());
        return 3;
    }

    if(msg.connack_code != 0U)
    {
        u1_printf("MQTT: CONNACK code=%u\r\n", msg.connack_code);
        return 4;
    }

    err = mqtt_build_subscribe(ONENET_MQTT_CMD_TOPIC, 0U, s_packet_id++, s_tx_buf, sizeof(s_tx_buf), &tx_len);
    if(err != MQTT_OK)
    {
        return 5;
    }

    mqtt_send_bytes(s_tx_buf, tx_len);
    if(mqtt_wait_for(0x90U, 5000U, NULL) != MQTT_OK)
    {
        u1_printf("MQTT: SUBACK timeout\r\n");
        return 6;
    }

    s_last_ping_ms = mqtt_now_ms();
    s_status = ONENET_MQTT_CONNECTED;
    s_fail_count = 0;
    u1_printf("MQTT: connected and subscribed\r\n");
    return 0;
}

static void mqtt_close_link(void)
{
    uint16_t tx_len = 0U;

    if(WifiMsg.ConnectFlag)
    {
        if(mqtt_build_disconnect(s_tx_buf, sizeof(s_tx_buf), &tx_len) == MQTT_OK)
        {
            mqtt_send_bytes(s_tx_buf, tx_len);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        WiFi_Close(50);
    }

    Connect_flag = 0;
    WifiMsg.ConnectFlag = 0;
    s_status = ONENET_MQTT_DISCONNECTED;
}

void OneNET_MQTT_Reset(void)
{
    mqtt_close_link();
    s_state = MQTT_SM_IDLE;
    s_paused = 0;
    s_fail_count = 0;
    s_reconnect_after_ms = 0;
}

onenet_mqtt_status_t OneNET_MQTT_GetStatus(void)
{
    return s_status;
}

uint8_t OneNET_MQTT_PauseForHttp(void)
{
    if(s_status != ONENET_MQTT_CONNECTED)
    {
        return 0;
    }

    s_paused = 1;
    mqtt_close_link();
    s_state = MQTT_SM_IDLE;
    return 0;
}

uint8_t OneNET_MQTT_ResumeAfterHttp(void)
{
    s_paused = 0;
    s_state = MQTT_SM_TCP_CONNECT;
    s_reconnect_after_ms = 0;
    return 0;
}

static uint8_t mqtt_publish_dp_json(const char *payload)
{
    uint16_t tx_len = 0U;
    uint16_t payload_len;

    if(payload == NULL)
    {
        return 2;
    }

    payload_len = (uint16_t)strlen(payload);
    if(mqtt_build_publish_qos0(ONENET_MQTT_STATE_TOPIC, (const uint8_t *)payload, payload_len,
                                s_tx_buf, sizeof(s_tx_buf), &tx_len) != MQTT_OK)
    {
        return 2;
    }

    mqtt_send_bytes(s_tx_buf, tx_len);
    return 0;
}

uint8_t OneNET_MQTT_PublishSwitchState(void)
{
    char payload[128];

    if(s_status != ONENET_MQTT_CONNECTED || s_paused)
    {
        return 1;
    }

    snprintf(payload, sizeof(payload),
             "{\"switch_1\":%u,\"switch_2\":%u,\"switch_3\":%u,\"switch_4\":%u}",
             Device.Led.Val1, Device.Led.Val2, Device.Led.Val3, Device.Led.Val4);

    return mqtt_publish_dp_json(payload);
}

uint8_t OneNET_MQTT_PublishTempHumi(int16_t temp_x10, uint16_t humi_x10)
{
    char payload[128];
    int temp_abs = (temp_x10 < 0) ? -temp_x10 : temp_x10;
    uint8_t ret;

    if(s_status != ONENET_MQTT_CONNECTED || s_paused)
    {
        return 1;
    }

    if(temp_x10 < 0)
    {
        snprintf(payload, sizeof(payload),
                 "{\"temp_data\":-%d.%d,\"humi_data\":%u.%u}",
                 temp_abs / 10, temp_abs % 10, humi_x10 / 10U, humi_x10 % 10U);
    }
    else
    {
        snprintf(payload, sizeof(payload),
                 "{\"temp_data\":%d.%d,\"humi_data\":%u.%u}",
                 temp_x10 / 10, temp_x10 % 10, humi_x10 / 10U, humi_x10 % 10U);
    }

    ret = mqtt_publish_dp_json(payload);
    if(ret == 0U)
    {
        u1_printf("MQTT dp: %s\r\n", payload);
    }
    return ret;
}

uint8_t OneNET_MQTT_Tick(void)
{
    uint32_t now;

    if(s_paused)
    {
        return 0;
    }

    now = mqtt_now_ms();

    switch(s_state)
    {
        case MQTT_SM_IDLE:
            s_state = MQTT_SM_TCP_CONNECT;
            break;

        case MQTT_SM_TCP_CONNECT:
            if(mqtt_do_connect() == 0U)
            {
                s_state = MQTT_SM_RUNNING;
                WifiMsg.Status = 0;
            }
            else
            {
                mqtt_close_link();
                s_fail_count++;
                WifiMsg.Status = 5;
                s_state = MQTT_SM_RECONNECT_WAIT;
                s_reconnect_after_ms = now + 3000U;
            }
            break;

        case MQTT_SM_RUNNING:
            mqtt_process_rx_packets();

            if(Uart2_HasRxOverflow())
            {
                WifiMsg.Status = 6;
                mqtt_close_link();
                s_state = MQTT_SM_RECONNECT_WAIT;
                s_reconnect_after_ms = now + 3000U;
                break;
            }

            if((now - s_last_ping_ms) >= ONENET_MQTT_PING_INTERVAL_MS)
            {
                uint16_t tx_len = 0U;
                if(mqtt_build_pingreq(s_tx_buf, sizeof(s_tx_buf), &tx_len) == MQTT_OK)
                {
                    mqtt_send_bytes(s_tx_buf, tx_len);
                    if(mqtt_wait_for(0xD0U, 3000U, NULL) != MQTT_OK)
                    {
                        u1_printf("MQTT: PING timeout\r\n");
                        WifiMsg.Status = 5;
                        mqtt_close_link();
                        s_state = MQTT_SM_RECONNECT_WAIT;
                        s_reconnect_after_ms = now + 3000U;
                        break;
                    }
                }
                s_last_ping_ms = now;
            }
            break;

        case MQTT_SM_RECONNECT_WAIT:
            if(now >= s_reconnect_after_ms)
            {
                s_state = MQTT_SM_TCP_CONNECT;
            }
            break;

        default:
            s_state = MQTT_SM_IDLE;
            break;
    }

    return s_fail_count;
}
