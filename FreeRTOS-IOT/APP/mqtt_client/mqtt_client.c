#include "mqtt_client.h"
#include <string.h>

static uint16_t mqtt_put_string(uint8_t *out, uint16_t offset, const char *str)
{
    uint16_t len = (uint16_t)strlen(str);

    out[offset++] = (uint8_t)(len >> 8);
    out[offset++] = (uint8_t)(len & 0xFF);
    memcpy(&out[offset], str, len);
    return (uint16_t)(offset + len);
}

int mqtt_encode_remaining_length(uint32_t length, uint8_t *out, uint16_t out_size)
{
    uint16_t i = 0;

    if(out == NULL || out_size == 0)
    {
        return -1;
    }

    do
    {
        if(i >= out_size)
        {
            return -1;
        }

        out[i] = (uint8_t)(length % 128U);
        length /= 128U;
        if(length > 0U)
        {
            out[i] |= 0x80U;
        }
        i++;
    } while(length > 0U && i < 4U);

    return (int)i;
}

int mqtt_decode_remaining_length(const uint8_t *in, uint16_t in_len, uint32_t *length, uint8_t *used_bytes)
{
    uint32_t multiplier = 1U;
    uint32_t value = 0U;
    uint8_t i;

    if(in == NULL || length == NULL || used_bytes == NULL || in_len == 0U)
    {
        return -1;
    }

    for(i = 0; i < 4U && i < in_len; i++)
    {
        value += (uint32_t)(in[i] & 0x7FU) * multiplier;
        multiplier *= 128U;
        if((in[i] & 0x80U) == 0U)
        {
            *length = value;
            *used_bytes = (uint8_t)(i + 1U);
            return 0;
        }
    }

    return -1;
}

uint16_t mqtt_packet_total_length(const uint8_t *data, uint16_t data_len)
{
    uint32_t remaining = 0U;
    uint8_t used = 0U;

    if(data == NULL || data_len < 2U)
    {
        return 0U;
    }

    if(mqtt_decode_remaining_length(&data[1], (uint16_t)(data_len - 1U), &remaining, &used) != 0)
    {
        return 0U;
    }

    return (uint16_t)(1U + used + remaining);
}

mqtt_err_t mqtt_build_connect(const mqtt_connect_opts_t *opts, uint8_t *out, uint16_t out_size, uint16_t *out_len)
{
    uint8_t variable[384];
    uint8_t rem_buf[4];
    uint16_t var_len = 0U;
    uint8_t connect_flags = 0U;
    int rem_len_bytes;

    if(opts == NULL || out == NULL || out_len == NULL || opts->client_id[0] == '\0')
    {
        return MQTT_ERR_PARAM;
    }

    var_len = mqtt_put_string(variable, var_len, "MQTT");
    variable[var_len++] = 0x04U;

    if(opts->clean_session)
    {
        connect_flags |= 0x02U;
    }
    if(opts->username[0] != '\0')
    {
        connect_flags |= 0x80U;
    }
    if(opts->password[0] != '\0')
    {
        connect_flags |= 0x40U;
    }
    variable[var_len++] = connect_flags;
    variable[var_len++] = (uint8_t)(opts->keep_alive_sec >> 8);
    variable[var_len++] = (uint8_t)(opts->keep_alive_sec & 0xFFU);
    var_len = mqtt_put_string(variable, var_len, opts->client_id);
    if(opts->username[0] != '\0')
    {
        var_len = mqtt_put_string(variable, var_len, opts->username);
    }
    if(opts->password[0] != '\0')
    {
        var_len = mqtt_put_string(variable, var_len, opts->password);
    }

    rem_len_bytes = mqtt_encode_remaining_length(var_len, rem_buf, sizeof(rem_buf));
    if(rem_len_bytes <= 0 || (uint16_t)(1U + rem_len_bytes + var_len) > out_size)
    {
        return MQTT_ERR_BUFFER;
    }

    out[0] = 0x10U;
    memcpy(&out[1], rem_buf, (size_t)rem_len_bytes);
    memcpy(&out[1U + rem_len_bytes], variable, var_len);
    *out_len = (uint16_t)(1U + rem_len_bytes + var_len);
    return MQTT_OK;
}

mqtt_err_t mqtt_build_subscribe(const char *topic, uint8_t qos, uint16_t packet_id, uint8_t *out, uint16_t out_size, uint16_t *out_len)
{
    uint8_t payload[256];
    uint8_t rem_buf[4];
    uint16_t payload_len = 0U;
    int rem_len_bytes;

    if(topic == NULL || out == NULL || out_len == NULL)
    {
        return MQTT_ERR_PARAM;
    }

    payload[payload_len++] = (uint8_t)(packet_id >> 8);
    payload[payload_len++] = (uint8_t)(packet_id & 0xFFU);
    payload_len = mqtt_put_string(payload, payload_len, topic);
    payload[payload_len++] = qos;

    rem_len_bytes = mqtt_encode_remaining_length(payload_len, rem_buf, sizeof(rem_buf));
    if(rem_len_bytes <= 0 || (uint16_t)(1U + rem_len_bytes + payload_len) > out_size)
    {
        return MQTT_ERR_BUFFER;
    }

    out[0] = 0x82U;
    memcpy(&out[1], rem_buf, (size_t)rem_len_bytes);
    memcpy(&out[1U + rem_len_bytes], payload, payload_len);
    *out_len = (uint16_t)(1U + rem_len_bytes + payload_len);
    return MQTT_OK;
}

mqtt_err_t mqtt_build_publish_qos0(const char *topic, const uint8_t *payload, uint16_t payload_len, uint8_t *out, uint16_t out_size, uint16_t *out_len)
{
    uint8_t header[256];
    uint8_t rem_buf[4];
    uint16_t header_len = 0U;
    int rem_len_bytes;
    uint32_t remaining;

    if(topic == NULL || out == NULL || out_len == NULL || payload_len > MQTT_MAX_PAYLOAD_LEN)
    {
        return MQTT_ERR_PARAM;
    }

    header_len = mqtt_put_string(header, header_len, topic);
    remaining = header_len + payload_len;

    rem_len_bytes = mqtt_encode_remaining_length(remaining, rem_buf, sizeof(rem_buf));
    if(rem_len_bytes <= 0 || (uint16_t)(1U + rem_len_bytes + remaining) > out_size)
    {
        return MQTT_ERR_BUFFER;
    }

    out[0] = 0x30U;
    memcpy(&out[1], rem_buf, (size_t)rem_len_bytes);
    memcpy(&out[1U + rem_len_bytes], header, header_len);
    if(payload_len > 0U && payload != NULL)
    {
        memcpy(&out[1U + rem_len_bytes + header_len], payload, payload_len);
    }
    *out_len = (uint16_t)(1U + rem_len_bytes + remaining);
    return MQTT_OK;
}

mqtt_err_t mqtt_build_pingreq(uint8_t *out, uint16_t out_size, uint16_t *out_len)
{
    if(out == NULL || out_len == NULL || out_size < 2U)
    {
        return MQTT_ERR_PARAM;
    }

    out[0] = 0xC0U;
    out[1] = 0x00U;
    *out_len = 2U;
    return MQTT_OK;
}

mqtt_err_t mqtt_build_disconnect(uint8_t *out, uint16_t out_size, uint16_t *out_len)
{
    if(out == NULL || out_len == NULL || out_size < 2U)
    {
        return MQTT_ERR_PARAM;
    }

    out[0] = 0xE0U;
    out[1] = 0x00U;
    *out_len = 2U;
    return MQTT_OK;
}

mqtt_err_t mqtt_parse_message(const uint8_t *data, uint16_t data_len, mqtt_message_t *msg)
{
    uint32_t remaining = 0U;
    uint8_t used = 0U;
    uint16_t offset;
    uint16_t topic_len;

    if(data == NULL || msg == NULL || data_len < 2U)
    {
        return MQTT_ERR_PARAM;
    }

    memset(msg, 0, sizeof(*msg));

    if(mqtt_decode_remaining_length(&data[1], (uint16_t)(data_len - 1U), &remaining, &used) != 0)
    {
        return MQTT_ERR_PROTOCOL;
    }

    if((uint32_t)(1U + used) + remaining > data_len)
    {
        return MQTT_ERR_PROTOCOL;
    }

    switch(data[0] & 0xF0U)
    {
        case 0x20U:
            msg->type = MQTT_PKT_CONNACK;
            if(remaining >= 2U)
            {
                msg->connack_code = data[1U + used + 1U];
            }
            break;

        case 0x30U:
        case 0x32U:
            msg->type = MQTT_PKT_PUBLISH;
            msg->qos = (uint8_t)((data[0] >> 1) & 0x03U);
            msg->retain = data[0] & 0x01U;
            offset = (uint16_t)(1U + used);
            topic_len = (uint16_t)((data[offset] << 8) | data[offset + 1U]);
            offset += 2U;
            if(topic_len >= MQTT_MAX_TOPIC_LEN || offset + topic_len > data_len)
            {
                return MQTT_ERR_PROTOCOL;
            }
            memcpy(msg->topic, &data[offset], topic_len);
            msg->topic[topic_len] = '\0';
            msg->topic_len = topic_len;
            offset += topic_len;
            if(msg->qos > 0U)
            {
                msg->packet_id = (uint16_t)((data[offset] << 8) | data[offset + 1U]);
                offset += 2U;
            }
            msg->payload_len = (uint16_t)((1U + used + remaining) - offset);
            if(msg->payload_len > MQTT_MAX_PAYLOAD_LEN)
            {
                return MQTT_ERR_BUFFER;
            }
            if(msg->payload_len > 0U)
            {
                memcpy(msg->payload, &data[offset], msg->payload_len);
            }
            msg->payload[msg->payload_len] = '\0';
            break;

        case 0x90U:
            msg->type = MQTT_PKT_SUBACK;
            break;

        case 0xD0U:
            msg->type = MQTT_PKT_PINGRESP;
            break;

        default:
            msg->type = MQTT_PKT_UNKNOWN;
            break;
    }

    return MQTT_OK;
}
