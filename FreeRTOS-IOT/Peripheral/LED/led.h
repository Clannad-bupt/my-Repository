#ifndef __LED_H
#define __LED_H	 

#include "main.h"

//使用位带操作（需要在 sys.h 中定义或直接使用 HAL 宏）
//这里为了简单直接使用 HAL 库宏

#define SCR1_ON()   HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET)
#define SCR1_OFF()  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET)

#define SCR2_ON()   HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET)
#define SCR2_OFF()  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET)

#define JK1_ON()    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET)
#define JK1_OFF()   HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET)

#define JK2_ON()    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET)
#define JK2_OFF()   HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET)

/* Compatibility aliases for onenet_http.c */
#define LED1_ON  SCR1_ON()
#define LED1_OFF SCR1_OFF()
#define LED2_ON  SCR2_ON()
#define LED2_OFF SCR2_OFF()
#define LED3_ON  JK1_ON()
#define LED3_OFF JK1_OFF()
#define LED4_ON  JK2_ON()
#define LED4_OFF JK2_OFF()


void LED_Init(void);

#endif
