#ifndef __OLED_H
#define __OLED_H

#include "main.h"

// OLED控制用函数
void OLED_Init(void);
void OLED_Clear(void);
void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr, uint8_t size);
void OLED_ShowString(uint8_t x, uint8_t y, const char *p, uint8_t size);
void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len, uint8_t size);
void OLED_ShowFloat(uint8_t x, uint8_t y, float num, uint8_t intLen, uint8_t fracLen, uint8_t size);
void OLED_DrawBMP(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, const uint8_t BMP[]);
void OLED_Fill(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t dot);
void OLED_ShowChinese(uint8_t x, uint8_t y, uint8_t no);
void OLED_DisplayOff(void);
void OLED_DisplayOn(void);
void OLED_Refresh(void);
void OLED_DrawLine(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2);
void OLED_DrawRectangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2);
void OLED_DrawCircle(uint8_t x, uint8_t y, uint8_t radius);

// 显示温度等专用函数
void OLED_DisplayTemperature(float temp1, float temp2, uint8_t relay_status, float temp_threshold);
void OLED_DisplaySensorInfo(uint8_t num_sensors);
#endif