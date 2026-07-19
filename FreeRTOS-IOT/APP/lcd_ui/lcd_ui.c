#include "lcd_ui.h"
#include "bsp_ili9341_lcd.h"
#include "fonts.h"
#include <stdio.h>
#include "delay.h"

/*
 * 显示开机画面
 */
void lcd_ui_ShowBootImg(void)
{
    LCD_SetFont(&Font24x32);
    LCD_SetColors(WHITE, BLACK);
    ILI9341_Clear(0, 0, LCD_X_LENGTH, LCD_Y_LENGTH);
    
    ILI9341_DispStringLine_EN(LINE(2), "    IOT System    ");
    ILI9341_DispStringLine_EN(LINE(4), "  Starting...   ");
    
    delay_ms(2000);
    ILI9341_Clear(0, 0, LCD_X_LENGTH, LCD_Y_LENGTH);
    
    // 画初始框架
    LCD_SetFont(&Font16x24);
    LCD_SetColors(YELLOW, BLACK);
    ILI9341_DispStringLine_EN(LINE(0), "Time:");
    ILI9341_DispStringLine_EN(LINE(3), "Weather:");
    ILI9341_DispStringLine_EN(LINE(8), "Device Status:");
    
    // 初始化 LED 状态显示
    lcd_ui_ShowLed(0, 0);
    lcd_ui_ShowLed(1, 0);
    lcd_ui_ShowLed(2, 0);
    lcd_ui_ShowLed(3, 0);
}

/*
 * 刷新时间显示
 */
void lcd_ui_ShowTime(struct xTime* Time)
{
    char timeStr[32];
    sprintf(timeStr, "Time: %02d:%02d:%02d   ", 
            Time->hour, Time->min, Time->sec);
            
    LCD_SetFont(&Font16x24);
    LCD_SetColors(GREEN, BLACK);
    ILI9341_DispString_EN(0, 24, timeStr);
}

/*
 * 刷新 LED 状态
 * xnum: 0-3 对应 4 个指示灯
 * status: 0-关，1-开
 */
void lcd_ui_ShowLed(uint8_t xnum, uint8_t status)
{
    uint16_t color = status ? GREEN : RED;
    uint16_t x_start = 20 + xnum * 50;
    
    // 在屏幕底部画几个圆代表 LED
    ILI9341_DrawCircle(x_start, 240, 15, 1); // 先画外圈
    LCD_SetTextColor(color);
    ILI9341_DrawCircle(x_start, 240, 14, 1); // 填充内部颜色
}

/*
 * 刷新天气信息
 */
void lcd_ui_ShowAllWeather(struct xWeather* weather)
{
    char weaStr[64];
    
    LCD_SetFont(&Font16x24);
    LCD_SetColors(CYAN, BLACK);
    
    // 简单地把天气信息打印出来
    sprintf(weaStr, "Cond: Code %d  ", weather->today.day.weaCode);
    ILI9341_DispString_EN(0, 96, weaStr);
    
    ILI9341_DispString_EN(0, 120, "                ");
}
