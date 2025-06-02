#include "oled.h"
#include "stdlib.h"
#include "oledfont.h"
#include "i2c.h"
#include <stdio.h>
#include <string.h>

// OLED的显存
// 存放格式如下：
// [0]0 1 2 3 ... 127
// [1]0 1 2 3 ... 127
// [2]0 1 2 3 ... 127
// [3]0 1 2 3 ... 127
// [4]0 1 2 3 ... 127
// [5]0 1 2 3 ... 127
// [6]0 1 2 3 ... 127
// [7]0 1 2 3 ... 127
static uint8_t OLED_GRAM[128][8];

#define OLED_ADDR 0x78  // OLED的I2C设备地址
#define COM 0x00  // OLED指令
#define DAT 0x40  // OLED数据

// I2C写入数据
static void OLED_Write_Byte(uint8_t data, uint8_t mode)
{
    uint8_t buf[2];
    buf[0] = mode;  // 写命令/数据控制字节
    buf[1] = data;  // 数据字节
    HAL_I2C_Master_Transmit(&hi2c1, OLED_ADDR, buf, 2, 100);
}

// 更新显存到OLED
void OLED_Refresh(void)
{
    uint8_t i, n;
    for (i = 0; i < 8; i++)
    {
        OLED_Write_Byte(0xB0 + i, COM);    // 设置页地址(0~7)
        OLED_Write_Byte(0x00, COM);        // 设置显示位置—列低地址
        OLED_Write_Byte(0x10, COM);        // 设置显示位置—列高地址
        
        // 一次发送一整行的数据(128字节)，提高效率
        uint8_t data[129];
        data[0] = DAT;  // 数据控制字节
        for (n = 0; n < 128; n++)
            data[n+1] = OLED_GRAM[n][i];
        HAL_I2C_Master_Transmit(&hi2c1, OLED_ADDR, data, 129, 200);
    }
}

// 开启OLED显示
void OLED_DisplayOn(void)
{
    OLED_Write_Byte(0x8D, COM); // 电荷泵使能
    OLED_Write_Byte(0x14, COM); // 开启电荷泵
    OLED_Write_Byte(0xAF, COM); // 点亮屏幕
}

// 关闭OLED显示
void OLED_DisplayOff(void)
{
    OLED_Write_Byte(0x8D, COM); // 电荷泵使能
    OLED_Write_Byte(0x10, COM); // 关闭电荷泵
    OLED_Write_Byte(0xAE, COM); // 关闭屏幕
}

// 清屏函数
void OLED_Clear(void)
{
    uint8_t i, n;
    for (i = 0; i < 8; i++)
    {
        for (n = 0; n < 128; n++)
        {
            OLED_GRAM[n][i] = 0;
        }
    }
    OLED_Refresh(); // 更新显示
}

// 画点
void OLED_DrawPoint(uint8_t x, uint8_t y, uint8_t t)
{
    uint8_t pos, bx, temp = 0;
    if (x > 127 || y > 63) return; // 超出范围
    pos = y / 8;                   // 得到页地址
    bx = y % 8;                    // 取得y在页内的位置
    temp = 1 << bx;                // 得到点在页内的位置对应的值
    if (t) OLED_GRAM[x][pos] |= temp;  // 置1
    else OLED_GRAM[x][pos] &= ~temp;   // 清0
}

// 填充区域
void OLED_Fill(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t dot)
{
    uint8_t x, y;
    for (x = x1; x <= x2; x++)
    {
        for (y = y1; y <= y2; y++)
        {
            OLED_DrawPoint(x, y, dot);
        }
    }
    OLED_Refresh();
}

// 在指定位置显示一个字符
void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr, uint8_t size)
{
    uint8_t i, j, c;
    c = chr - ' '; // 得到偏移后的值
    if (x > 128 - 1)
    {
        x = 0;
        y = y + 2;
    }
    if (size == 16)
    {
        for (i = 0; i < 8; i++)
        {
            for (j = 0; j < 8; j++)
            {
                if (F8X16[c * 16 + i] & (0x80 >> j))
                    OLED_DrawPoint(x + j, y + i, 1);
                else
                    OLED_DrawPoint(x + j, y + i, 0);
            }
        }
        for (i = 0; i < 8; i++)
        {
            for (j = 0; j < 8; j++)
            {
                if (F8X16[c * 16 + i + 8] & (0x80 >> j))
                    OLED_DrawPoint(x + j, y + i + 8, 1);
                else
                    OLED_DrawPoint(x + j, y + i + 8, 0);
            }
        }
    }
    else if (size == 8)
    {
        for (i = 0; i < 6; i++)
        {
            for (j = 0; j < 8; j++)
            {
                if (F6x8[c][i] & (1 << j))
                    OLED_DrawPoint(x + i, y + j, 1);
                else
                    OLED_DrawPoint(x + i, y + j, 0);
            }
        }
    }
}

// 显示字符串
void OLED_ShowString(uint8_t x, uint8_t y, const char *p, uint8_t size)
{
    while (*p != '\0')
    {
        OLED_ShowChar(x, y, *p, size);
        x += (size == 8) ? 6 : 8;
        if (x > 122)
        {
            x = 0;
            y += (size == 8) ? 9 : 16;
        }
        p++;
    }
}

// m^n函数
static uint32_t oled_pow(uint8_t m, uint8_t n)
{
    uint32_t result = 1;
    while (n--)
        result *= m;
    return result;
}

// 显示数字
void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len, uint8_t size)
{
    uint8_t t, temp;
    uint8_t enshow = 0;
    for (t = 0; t < len; t++)
    {
        temp = (num / oled_pow(10, len - t - 1)) % 10;
        if (enshow == 0 && t < (len - 1))
        {
            if (temp == 0)
            {
                OLED_ShowChar(x + (size / 2) * t, y, ' ', size);
                continue;
            } else 
                enshow = 1;
        }
        OLED_ShowChar(x + (size / 2) * t, y, temp + '0', size);
    }
}

// 显示浮点数
void OLED_ShowFloat(uint8_t x, uint8_t y, float num, uint8_t intLen, uint8_t fracLen, uint8_t size)
{
    int32_t intPart;
    float fracPart;
    
    // 处理负数
    if (num < 0)
    {
        OLED_ShowChar(x, y, '-', size);
        x += (size == 8) ? 6 : 8;
        num = -num;
    }
    
    intPart = (int32_t)num;           // 获取整数部分
    fracPart = num - (float)intPart;  // 获取小数部分
    
    // 显示整数部分
    OLED_ShowNum(x, y, intPart, intLen, size);
    x += intLen * ((size == 8) ? 6 : 8);
    
    // 显示小数点
    OLED_ShowChar(x, y, '.', size);
    x += (size == 8) ? 6 : 8;
    
    // 显示小数部分
    for (uint8_t i = 0; i < fracLen; i++)
    {
        fracPart *= 10;
        OLED_ShowChar(x, y, '0' + ((uint8_t)fracPart % 10), size);
        x += (size == 8) ? 6 : 8;
    }
}

// 初始化OLED
void OLED_Init(void)
{
    // 初始化命令
    uint8_t CMD_Data[] = {
        0xAE, 0x00, 0x10, 0x40, 0x81, 0xCF, 0xA1, 0xC8, 0xA6,
        0xA8, 0x3f, 0xD3, 0x00, 0xD5, 0x80, 0xD9, 0xF1, 0xDA, 0x12,
        0xDB, 0x40, 0x20, 0x02, 0x8D, 0x14, 0xA4, 0xA6, 0xAF
    };
    
    HAL_Delay(100);  // 延时等待OLED稳定
    
    // 发送初始化命令
    for (uint8_t i = 0; i < sizeof(CMD_Data); i++)
    {
        OLED_Write_Byte(CMD_Data[i], COM);
    }
    
    OLED_Clear();  // 清屏
}

// 显示温度和继电器状态
// void OLED_DisplayTemperature(float temp1, float temp2, uint8_t relay_status, float temp_threshold)
// {
//     char buffer[20];
    
//     OLED_Clear();  // 清屏
    
//     // 显示标题
//     OLED_ShowString(0, 0, "Temperature Monitor", 8);
//     OLED_ShowString(0, 16, "Sensor1:", 8);
    
//     // 显示传感器1温度
//     if (temp1 > -900.0) {
//         sprintf(buffer, "%.1fC", temp1);
//         OLED_ShowString(56, 16, buffer, 8);
//     } else {
//         OLED_ShowString(56, 16, "Error", 8);
//     }
    
//     // 显示传感器2温度
//     OLED_ShowString(0, 26, "Sensor2:", 8);
//     if (temp2 > -900.0) {
//         sprintf(buffer, "%.1fC", temp2);
//         OLED_ShowString(56, 26, buffer, 8);
//     } else {
//         OLED_ShowString(56, 26, "Error", 8);
//     }
    
//     // 显示继电器状态
//     OLED_ShowString(0, 40, "Relay:", 8);
//     if (relay_status) {
//         OLED_ShowString(56, 40, "ON ", 8);
//     } else {
//         OLED_ShowString(56, 40, "OFF", 8);
//     }
    
//     // 显示温度阈值
//     OLED_ShowString(0, 50, "Threshold:", 8);
//     sprintf(buffer, "%.1fC", temp_threshold);
//     OLED_ShowString(56, 50, buffer, 8);
    
//     OLED_Refresh();  // 更新显示
// }

// 显示温度和继电器状态
void OLED_DisplayTemperature(float temp1, float temp2, float temp3, uint8_t relay_status, float temp_threshold)
{
    char buffer[20];
    // 不再全屏清空，改为分区域覆盖
    // 标题
    OLED_ShowString(0, 0, "Temperature Monitor   ", 8); // 末尾补空格覆盖
    // 传感器1
    OLED_ShowString(0, 16, "Sensor1:      ", 8);
    if (temp1 > -900.0) {
        sprintf(buffer, "%6.1fC", temp1);
        OLED_ShowString(56, 16, buffer, 8);
    } else {
        OLED_ShowString(56, 16, " Error ", 8);
    }
    // 传感器2
    OLED_ShowString(0, 26, "Sensor2:      ", 8);
    if (temp2 > -900.0) {
        sprintf(buffer, "%6.1fC", temp2);
        OLED_ShowString(56, 26, buffer, 8);
    } else {
        OLED_ShowString(56, 26, " Error ", 8);
    }
    // 传感器3
    OLED_ShowString(0, 36, "Sensor3:      ", 8);
    if (temp3 > -900.0) {
        sprintf(buffer, "%6.1fC", temp3);
        OLED_ShowString(56, 36, buffer, 8);
    } else {
        OLED_ShowString(56, 36, " Error ", 8);
    }
    // 继电器状态
    OLED_ShowString(0, 50, "Relay:   ", 8);
    if (relay_status) {
        OLED_ShowString(56, 50, "ON  ", 8);
    } else {
        OLED_ShowString(56, 50, "OFF ", 8);
    }
    // 温度阈值
    OLED_ShowString(80, 50, "TH:", 8);
    sprintf(buffer, "%4.1f", temp_threshold);
    OLED_ShowString(104, 50, buffer, 8);
    OLED_Refresh();
}

// 显示传感器信息
void OLED_DisplaySensorInfo(uint8_t num_sensors)
{
    char buffer[20];
    
    OLED_Clear();  // 清屏
    
    // 显示标题
    OLED_ShowString(0, 0, "DS18B20 Sensors", 8);
    
    // 显示传感器数量
    sprintf(buffer, "Found: %d sensors", num_sensors);
    OLED_ShowString(0, 16, buffer, 8);
    
    if (num_sensors == 0) {
        OLED_ShowString(0, 30, "No sensors found!", 8);
        OLED_ShowString(0, 40, "Check connections", 8);
    } else {
        OLED_ShowString(0, 30, "System ready", 8);
        OLED_ShowString(0, 40, "Monitoring...", 8);
    }
    
    OLED_Refresh();  // 更新显示
}