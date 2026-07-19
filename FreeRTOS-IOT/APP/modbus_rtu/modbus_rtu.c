#include "modbus_rtu.h"
#include "uart3.h"
#include <string.h>

#define MODBUS_MAX_REG_COUNT       64U
#define MODBUS_MAX_FRAME_SIZE      DMA_UART3_RX_SIZE

uint16_t Modbus_CRC16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    uint16_t i;
    uint8_t j;

    for(i = 0; i < len; i++)
    {
        crc ^= buf[i];
        for(j = 0; j < 8; j++)
        {
            if(crc & 0x0001)
            {
                crc = (uint16_t)((crc >> 1) ^ 0xA001);
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return crc;
}

static void Modbus_AppendCRC(uint8_t *frame, uint16_t len_without_crc)
{
    uint16_t crc = Modbus_CRC16(frame, len_without_crc);
    frame[len_without_crc] = (uint8_t)(crc & 0xFF);
    frame[len_without_crc + 1] = (uint8_t)(crc >> 8);
}

static uint8_t Modbus_CheckCRC(const uint8_t *frame, uint16_t len)
{
    uint16_t crc_calc;
    uint16_t crc_recv;

    if(frame == NULL || len < 4)
    {
        return 0;
    }

    crc_calc = Modbus_CRC16(frame, (uint16_t)(len - 2));
    crc_recv = (uint16_t)(frame[len - 2] | (frame[len - 1] << 8));
    return (crc_calc == crc_recv);
}

static ModbusStatus_t Modbus_Transceive(const uint8_t *tx, uint16_t tx_len, uint8_t *rx, uint16_t *rx_len, uint32_t timeout_ms)
{
    uint16_t len;
    uint8_t stale[MODBUS_MAX_FRAME_SIZE];

    if(tx == NULL || rx == NULL || rx_len == NULL || tx_len == 0)
    {
        return MODBUS_ERR_PARAM;
    }

    while(Uart3_GetFrame(stale, sizeof(stale), 0) > 0)
    {
        /* drain stale frames before issuing a new request */
    }

    Uart3_DMA_Send_Data((void*)tx, tx_len);
    len = Uart3_GetFrame(rx, MODBUS_MAX_FRAME_SIZE, timeout_ms);
    if(len == 0)
    {
        return MODBUS_ERR_TIMEOUT;
    }

    *rx_len = len;
    if(!Modbus_CheckCRC(rx, len))
    {
        return MODBUS_ERR_CRC;
    }

    if(rx[1] & 0x80)
    {
        return MODBUS_ERR_EXCEPTION;
    }

    return MODBUS_OK;
}

static ModbusStatus_t Modbus_ReadRegisters(uint8_t func, uint8_t slave, uint16_t start, uint16_t count, uint16_t *out, uint32_t timeout_ms)
{
    uint8_t tx[8];
    uint8_t rx[MODBUS_MAX_FRAME_SIZE];
    uint16_t rx_len = 0;
    uint16_t i;
    ModbusStatus_t status;

    if(slave == 0 || out == NULL || count == 0 || count > MODBUS_MAX_REG_COUNT)
    {
        return MODBUS_ERR_PARAM;
    }

    tx[0] = slave;
    tx[1] = func;
    tx[2] = (uint8_t)(start >> 8);
    tx[3] = (uint8_t)(start & 0xFF);
    tx[4] = (uint8_t)(count >> 8);
    tx[5] = (uint8_t)(count & 0xFF);
    Modbus_AppendCRC(tx, 6);

    status = Modbus_Transceive(tx, sizeof(tx), rx, &rx_len, timeout_ms);
    if(status != MODBUS_OK)
    {
        return status;
    }

    if(rx_len < (uint16_t)(5 + count * 2) || rx[0] != slave || rx[1] != func || rx[2] != (uint8_t)(count * 2))
    {
        return MODBUS_ERR_FRAME;
    }

    for(i = 0; i < count; i++)
    {
        out[i] = (uint16_t)((rx[3 + i * 2] << 8) | rx[4 + i * 2]);
    }

    return MODBUS_OK;
}

ModbusStatus_t Modbus_ReadHoldingRegisters(uint8_t slave, uint16_t start, uint16_t count, uint16_t *out, uint32_t timeout_ms)
{
    return Modbus_ReadRegisters(0x03, slave, start, count, out, timeout_ms);
}

ModbusStatus_t Modbus_ReadInputRegisters(uint8_t slave, uint16_t start, uint16_t count, uint16_t *out, uint32_t timeout_ms)
{
    return Modbus_ReadRegisters(0x04, slave, start, count, out, timeout_ms);
}

ModbusStatus_t Modbus_WriteSingleRegister(uint8_t slave, uint16_t reg, uint16_t value, uint32_t timeout_ms)
{
    uint8_t tx[8];
    uint8_t rx[MODBUS_MAX_FRAME_SIZE];
    uint16_t rx_len = 0;
    ModbusStatus_t status;

    if(slave == 0)
    {
        return MODBUS_ERR_PARAM;
    }

    tx[0] = slave;
    tx[1] = 0x06;
    tx[2] = (uint8_t)(reg >> 8);
    tx[3] = (uint8_t)(reg & 0xFF);
    tx[4] = (uint8_t)(value >> 8);
    tx[5] = (uint8_t)(value & 0xFF);
    Modbus_AppendCRC(tx, 6);

    status = Modbus_Transceive(tx, sizeof(tx), rx, &rx_len, timeout_ms);
    if(status != MODBUS_OK)
    {
        return status;
    }

    if(rx_len != sizeof(tx) || memcmp(tx, rx, sizeof(tx)) != 0)
    {
        return MODBUS_ERR_FRAME;
    }

    return MODBUS_OK;
}

ModbusStatus_t Modbus_WriteMultipleRegisters(uint8_t slave, uint16_t start, const uint16_t *values, uint16_t count, uint32_t timeout_ms)
{
    uint8_t tx[MODBUS_MAX_FRAME_SIZE];
    uint8_t rx[MODBUS_MAX_FRAME_SIZE];
    uint16_t tx_len;
    uint16_t rx_len = 0;
    uint16_t i;
    ModbusStatus_t status;

    if(slave == 0 || values == NULL || count == 0 || count > MODBUS_MAX_REG_COUNT)
    {
        return MODBUS_ERR_PARAM;
    }

    tx_len = (uint16_t)(9 + count * 2);
    if(tx_len > MODBUS_MAX_FRAME_SIZE)
    {
        return MODBUS_ERR_PARAM;
    }

    tx[0] = slave;
    tx[1] = 0x10;
    tx[2] = (uint8_t)(start >> 8);
    tx[3] = (uint8_t)(start & 0xFF);
    tx[4] = (uint8_t)(count >> 8);
    tx[5] = (uint8_t)(count & 0xFF);
    tx[6] = (uint8_t)(count * 2);
    for(i = 0; i < count; i++)
    {
        tx[7 + i * 2] = (uint8_t)(values[i] >> 8);
        tx[8 + i * 2] = (uint8_t)(values[i] & 0xFF);
    }
    Modbus_AppendCRC(tx, (uint16_t)(tx_len - 2));

    status = Modbus_Transceive(tx, tx_len, rx, &rx_len, timeout_ms);
    if(status != MODBUS_OK)
    {
        return status;
    }

    if(rx_len != 8 || rx[0] != slave || rx[1] != 0x10 ||
       rx[2] != (uint8_t)(start >> 8) || rx[3] != (uint8_t)(start & 0xFF) ||
       rx[4] != (uint8_t)(count >> 8) || rx[5] != (uint8_t)(count & 0xFF))
    {
        return MODBUS_ERR_FRAME;
    }

    return MODBUS_OK;
}
