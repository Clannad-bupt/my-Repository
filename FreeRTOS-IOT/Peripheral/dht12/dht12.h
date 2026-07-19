#ifndef __DHT12_H
#define __DHT12_H

#include "main.h"

#define DHT12_I2C_ADDR      (0x5C << 1)

typedef struct
{
    int16_t temperature_x10;
    uint16_t humidity_x10;
} DHT12_Data_t;

uint8_t DHT12_ReadData(DHT12_Data_t *data);

#endif
