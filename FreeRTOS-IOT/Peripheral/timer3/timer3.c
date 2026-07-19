#include "timer3.h"

TIM_HandleTypeDef htim3;

/*
 * 配置 TIM3 (HAL)
 */
void TIMER3_Configuration(uint16_t arr, uint16_t psc)
{
    __HAL_RCC_TIM3_CLK_ENABLE();
    
    htim3.Instance = TIM3;
    htim3.Init.Prescaler = psc;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = arr;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    
    HAL_TIM_Base_Init(&htim3);
    
    // 如果需要中断，可以取消注释下面两行，并在 stm32f1xx_it.c 中添加 TIM3_IRQHandler 和 HAL_TIM_PeriodElapsedCallback
    // HAL_NVIC_SetPriority(TIM3_IRQn, 5, 0);
    // HAL_NVIC_EnableIRQ(TIM3_IRQn);
    // HAL_TIM_Base_Start_IT(&htim3);
    
    HAL_TIM_Base_Start(&htim3);
}
