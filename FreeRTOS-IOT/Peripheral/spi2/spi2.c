#include "spi2.h"
#include "spi.h"  // CubeMX 生成的 spi.h，包含 hspi2 句柄

extern SPI_HandleTypeDef hspi2;

/*
 * SPI2速度设置函数（在HAL库中通常不需要频繁改变分频，如果需要，可重新配置Init）
 * SpeedSet: 0-保留, 1-使用CubeMX默认速度
 */
void SPI2_SetSpeed(uint8_t SpeedSet)
{
    // HAL 库不建议在运行中直接改寄存器改变速度，通常在 MX_SPI2_Init 中设定好即可。
    // 为了兼容旧代码，这里保持空实现或简单实现。
}

/*
 * SPI2 读写一个字节
 * TxData: 要写入的字节
 * 返回值: 读取到的字节
 */
uint8_t SPI2_ReadWriteByte(uint8_t TxData)
{
    uint8_t rxData = 0;
    // 使用 HAL API，超时设置为 1000ms
    HAL_SPI_TransmitReceive(&hspi2, &TxData, &rxData, 1, 1000);
    return rxData;
}
