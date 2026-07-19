#include "delay.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * HAL + FreeRTOS 环境下的延时模块
 * 
 * 注意：SysTick 已被 FreeRTOS 接管（FreeRTOSConfig.h 中 xPortSysTickHandler = SysTick_Handler）
 *       HAL 时基使用 TIM4（CubeMX 已配置）
 *       因此本文件不再包含 SysTick_Handler 和 SysTick 初始化
 */

static u8  fac_us = 0;  // us延时倍乘数

/**
 * @brief  延时初始化（适配HAL + FreeRTOS）
 * @note   SysTick 由 FreeRTOS 内核初始化，此处仅计算 us 延时因子
 */
void delay_init(void)
{
    fac_us = SystemCoreClock / 1000000;  // 72MHz -> fac_us = 72
}

/**
 * @brief  微秒级延时（不依赖中断，使用 SysTick 差值法）
 * @param  nus: 延时微秒数
 * @note   在任务调度前后均可使用
 */
void delay_us(u32 nus)
{
    u32 ticks;
    u32 told, tnow, tcnt = 0;
    u32 reload = SysTick->LOAD;

    ticks = nus * fac_us;
    told = SysTick->VAL;
    while(1)
    {
        tnow = SysTick->VAL;
        if(tnow != told)
        {
            if(tnow < told)
                tcnt += told - tnow;
            else
                tcnt += reload - tnow + told;
            told = tnow;
            if(tcnt >= ticks)
                break;
        }
    }
}

/**
 * @brief  毫秒级延时
 * @param  nms: 延时毫秒数
 * @note   如果 FreeRTOS 调度器已启动，使用 vTaskDelay 让出 CPU
 *         否则使用 delay_us 忙等
 */
void delay_ms(u32 nms)
{
    if(xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        if(nms >= 1)
        {
            vTaskDelay(pdMS_TO_TICKS(nms));
        }
    }
    else
    {
        /* Scheduler not running yet: SysTick is owned by FreeRTOS, so use HAL tick (TIM4). */
        HAL_Delay(nms);
    }
}

/**
 * @brief  毫秒级延时（不会触发任务调度，强制忙等）
 * @param  nms: 延时毫秒数
 */
void delay_xms(u32 nms)
{
    u32 i;

    if(xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        for(i = 0; i < nms; i++)
        {
            delay_us(1000);
        }
    }
    else
    {
        HAL_Delay(nms);
    }
}
