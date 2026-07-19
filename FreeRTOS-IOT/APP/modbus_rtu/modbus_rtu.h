#ifndef __MODBUS_RTU_H
#define __MODBUS_RTU_H

#include "main.h"

typedef enum
{
    MODBUS_OK = 0,
    MODBUS_ERR_PARAM = 1,
    MODBUS_ERR_TIMEOUT = 2,
    MODBUS_ERR_CRC = 3,
    MODBUS_ERR_EXCEPTION = 4,
    MODBUS_ERR_FRAME = 5
} ModbusStatus_t;

uint16_t Modbus_CRC16(const uint8_t *buf, uint16_t len);
ModbusStatus_t Modbus_ReadHoldingRegisters(uint8_t slave, uint16_t start, uint16_t count, uint16_t *out, uint32_t timeout_ms);
ModbusStatus_t Modbus_ReadInputRegisters(uint8_t slave, uint16_t start, uint16_t count, uint16_t *out, uint32_t timeout_ms);
ModbusStatus_t Modbus_WriteSingleRegister(uint8_t slave, uint16_t reg, uint16_t value, uint32_t timeout_ms);
ModbusStatus_t Modbus_WriteMultipleRegisters(uint8_t slave, uint16_t start, const uint16_t *values, uint16_t count, uint32_t timeout_ms);

#endif
