#include "led.h"

/*	LED IO初始化
 *	因为涉及到了PA3/4引脚，所以需要关闭JTAG功能
 *
 *	SCR1  -  PB8
 *	SCR2  -  PB5
 *	JK1   -  PB4
 *	JK2   -  PB3
 */
void LED_Init(void)
{
	GPIO_InitTypeDef  GPIO_InitStruct = {0};
    
	__HAL_RCC_GPIOB_CLK_ENABLE();	//使能GPIOB时钟

	GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_5 | GPIO_PIN_4 | GPIO_PIN_3;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;		//推挽输出
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;	//IO口速度为50MHz
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);					//根据设定参数初始化GPIOB
	
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);		//PB.8 输出高
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);		//PB.5 输出高
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);	//PB.4 输出低
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);	//PB.3 输出低
	
    // 关闭JTAG，开启SWD。在HAL库中，通常在SystemClock_Config或HAL_MspInit中统一配置
    __HAL_AFIO_REMAP_SWJ_NOJTAG(); 
}
