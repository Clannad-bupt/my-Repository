#ifndef __LCD_UI_H
#define __LCD_UI_H

#include "main.h"
#include "structure.h"

// 替换原来的 ui_ShowBootImg 等函数
void lcd_ui_ShowBootImg(void);
void lcd_ui_ShowTime(struct xTime* Time);
void lcd_ui_ShowLed(uint8_t xnum, uint8_t status);
void lcd_ui_ShowAllWeather(struct xWeather* weather);

#endif
