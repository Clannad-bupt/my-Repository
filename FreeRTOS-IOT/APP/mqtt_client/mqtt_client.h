#ifndef _MQTT_CLIENT_H
#define _MQTT_CLIENT_H

#include "stm32f1xx_hal.h"

#define MQTT_MAX_CLIENT_ID_LEN   64
#define MQTT_MAX_USERNAME_LEN    64
#define MQTT_MAX_PASSWORD_LEN    128
#define MQTT_MAX_TOPIC_LEN       128
#define MQTT_MAX_PAYLOAD_LEN     512
#define MQTT_TX_BUFFER_SIZE      512
#define MQTT_RX_BUFFER_SIZE      1024

typedef enum
{
    MQTT_OK = 0,
    MQTT_ERR_PARAM,
    MQTT_ERR_BUFFER,
    MQTT_ERR_TIMEOUT,
    MQTT_ERR_PROTOCOL,
    MQTT_ERR_CONNACK,
    MQTT_ERR_SUBACK,
    MQTT_ERR_NOT_CONNECTED
} mqtt_err_t;

typedef enum
{
    MQTT_PKT_CONNECT = 1,
    MQTT_PKT_CONNACK,
    MQTT_PKT_PUBLISH,
    MQTT_PKT_PUBACK,
    MQTT_PKT_SUBSCRIBE,
    MQTT_PKT_SUBACK,
    MQTT_PKT_PINGREQ,
    MQTT_PKT_PINGRESP,
    MQTT_PKT_DISCONNECT,
    MQTT_PKT_UNKNOWN
} mqtt_pkt_type_t;

typedef struct
{
    char client_id[MQTT_MAX_CLIENT_ID_LEN];
    char username[MQTT_MAX_USERNAME_LEN];
    char password[MQTT_MAX_PASSWORD_LEN];
    uint16_t keep_alive_sec;
    uint8_t clean_session;
} mqtt_connect_opts_t;

typedef struct
{
    mqtt_pkt_type_t type;
    uint8_t qos;
    uint8_t retain;
    uint16_t packet_id;
    char topic[MQTT_MAX_TOPIC_LEN];
    uint16_t topic_len;
    uint16_t payload_len;
    uint8_t payload[MQTT_MAX_PAYLOAD_LEN];
    uint8_t connack_code;
} mqtt_message_t;

int mqtt_encode_remaining_length(uint32_t length, uint8_t *out, uint16_t out_size);
int mqtt_decode_remaining_length(const uint8_t *in, uint16_t in_len, uint32_t *length, uint8_t *used_bytes);
uint16_t mqtt_packet_total_length(const uint8_t *data, uint16_t data_len);

mqtt_err_t mqtt_build_connect(const mqtt_connect_opts_t *opts, uint8_t *out, uint16_t out_size, uint16_t *out_len);
mqtt_err_t mqtt_build_subscribe(const char *topic, uint8_t qos, uint16_t packet_id, uint8_t *out, uint16_t out_size, uint16_t *out_len);
mqtt_err_t mqtt_build_publish_qos0(const char *topic, const uint8_t *payload, uint16_t payload_len, uint8_t *out, uint16_t out_size, uint16_t *out_len);
mqtt_err_t mqtt_build_pingreq(uint8_t *out, uint16_t out_size, uint16_t *out_len);
mqtt_err_t mqtt_build_disconnect(uint8_t *out, uint16_t out_size, uint16_t *out_len);

mqtt_err_t mqtt_parse_message(const uint8_t *data, uint16_t data_len, mqtt_message_t *msg);

#endif
