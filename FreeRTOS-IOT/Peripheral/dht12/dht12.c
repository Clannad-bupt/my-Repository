#include "dht12.h"
#include "i2c.h"

extern I2C_HandleTypeDef hi2c1;

uint8_t DHT12_ReadData(DHT12_Data_t *data)
{
    uint8_t raw[5];
    uint8_t checksum;
    int16_t temp;

    if(data == NULL)
    {
        return 1;
    }

    if(HAL_I2C_Mem_Read(&hi2c1, DHT12_I2C_ADDR, 0x00, I2C_MEMADD_SIZE_8BIT, raw, sizeof(raw), 100) != HAL_OK)
    {
        return 2;
    }

    checksum = (uint8_t)(raw[0] + raw[1] + raw[2] + raw[3]);
    if(checksum != raw[4])
    {
        return 3;
    }

    data->humidity_x10 = (uint16_t)(raw[0] * 10U + raw[1]);
    temp = (int16_t)(raw[2] * 10 + (raw[3] & 0x7F));
    if(raw[3] & 0x80)
    {
        temp = -temp;
    }
    data->temperature_x10 = temp;

    return 0;
}
