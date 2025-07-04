/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2025 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
#include "oled.h"

#define DS18B20_PORT GPIOA
#define DS18B20_PIN GPIO_PIN_1

#define RELAY_Pin GPIO_PIN_4
#define RELAY_GPIO_Port GPIOA
#define MAX_DS18B20_SENSORS 5
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
// --- 新增：存储传感器ROM地址和数量 ---
uint8_t g_ds18b20_roms[MAX_DS18B20_SENSORS][8];
uint8_t g_num_sensors = 0;
// --- 新增：1-Wire搜索状态变量 ---
uint8_t LastDiscrepancy;
uint8_t LastFamilyDiscrepancy;
uint8_t LastDeviceFlag;
float temperatureArray[3]; // 用于存储温度值

// --- 新增：串口3接收和继电器定时控制相关变量 ---
#define UART3_RX_BUFFER_SIZE 50
uint8_t uart3_rx_buffer[UART3_RX_BUFFER_SIZE];
uint8_t uart3_rx_index = 0;
uint8_t uart3_rx_complete = 0;
uint8_t uart3_current_char; // 当前接收的字符
uint32_t relay_timer_count = 0;  // 继电器定时器计数
uint8_t relay_timer_active = 0;  // 继电器定时器是否激活
uint8_t relay_force_state = 0;   // 强制继电器状态（0=关闭，1=开启）
uint8_t relay_temp_control = 1;  // 温度控制是否启用（1=启用，0=禁用）

// --- 新增：温度阈值变量（可调） ---
float temp_threshold = 30.0f; // 默认温度阈值
#define TEMP_THRESHOLD_MIN  0.0f
#define TEMP_THRESHOLD_MAX  99.0f
#define TEMP_THRESHOLD_STEP 1.0f

// 按键防抖相关变量
uint8_t key_pa2_last = 1, key_pa3_last = 1;
uint32_t key_debounce_tick_pa2 = 0, key_debounce_tick_pa3 = 0;
#define KEY_DEBOUNCE_MS 30
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* USER CODE BEGIN 0 */

// 初始化继电器控制引脚
void RELAY_Init(void)
{
  HAL_GPIO_WritePin(RELAY_GPIO_Port, RELAY_Pin, GPIO_PIN_RESET);
}

// 控制继电器状态
void RELAY_Control(uint8_t state)
{
  HAL_GPIO_WritePin(RELAY_GPIO_Port, RELAY_Pin, state ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

// 微秒级延时函数 (保持不变)
void Delay_us(uint16_t us)
{
  us *= 6;
  while (us--)
  {
    __NOP();
    __NOP();
    __NOP();
    __NOP();
    __NOP();
    __NOP();
    __NOP();
    __NOP();
  }
}

// 写1位数据 (保持不变)
void DS18B20_WriteBit(uint8_t bit)
{
  HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_RESET);
  Delay_us(1); // t_low1: 1-15us
  if (bit)
  {
    HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_SET); // 写1
  }
  else
  {
    // 保持低电平即可写0
  }
  Delay_us(60);                                               // t_slot: 60-120us
  HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_SET); // 释放总线
  Delay_us(1);                                                // t_rec: >1us
}

// 读1位数据 (保持不变)
uint8_t DS18B20_ReadBit(void)
{
  uint8_t bit = 0;
  HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_RESET);
  Delay_us(1);                                                // t_low0: 1-15us
  HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_SET); // 释放总线，由从机拉低或保持高电平
  Delay_us(10);                                               // t_rdv: <15us (在15us内采样)
  if (HAL_GPIO_ReadPin(DS18B20_PORT, DS18B20_PIN))
  {
    bit = 1;
  }
  Delay_us(50); // 等待时间片结束 (60-120us)
  return bit;
}

// 向DS18B20写入1字节 (保持不变)
void DS18B20_WriteByte(uint8_t data)
{
  for (uint8_t i = 0; i < 8; i++)
  {
    DS18B20_WriteBit(data & 0x01);
    data >>= 1;
  }
}

// 从DS18B20读取1字节 (保持不变)
uint8_t DS18B20_ReadByte(void)
{
  uint8_t data = 0;
  for (uint8_t i = 0; i < 8; i++)
  {
    data >>= 1;
    if (DS18B20_ReadBit())
    {
      data |= 0x80;
    }
  }
  return data;
}

// 复位DS18B20 (保持不变)
// 返回 0 表示成功检测到存在脉冲, 1 表示无设备响应
uint8_t DS18B20_Reset(void)
{
  uint8_t status;
  // 配置为推挽输出
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = DS18B20_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(DS18B20_PORT, &GPIO_InitStruct);

  HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_RESET);
  Delay_us(480); // 至少480us
  HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_SET);

  // 配置为浮空输入
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL; // 或 GPIO_PULLUP，取决于外部上拉
  HAL_GPIO_Init(DS18B20_PORT, &GPIO_InitStruct);

  Delay_us(60);                                         // 等待15-60us后采样
  status = HAL_GPIO_ReadPin(DS18B20_PORT, DS18B20_PIN); // 读取存在脉冲 (低电平有效)
  Delay_us(420);                                        // 等待存在脉冲结束 (总共480us)

  // 恢复为推挽输出，准备后续通信
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(DS18B20_PORT, &GPIO_InitStruct);
  HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_SET); // 释放总线

  return status; // 0=成功, 1=失败
}

// --- 新增：CRC8校验函数 ---
uint8_t crc8(const uint8_t *addr, uint8_t len)
{
  uint8_t crc = 0;
  while (len--)
  {
    uint8_t inbyte = *addr++;
    for (uint8_t i = 8; i; i--)
    {
      uint8_t mix = (crc ^ inbyte) & 0x01;
      crc >>= 1;
      if (mix)
        crc ^= 0x8C; // 校验多项式 X^8 + X^5 + X^4 + 1
      inbyte >>= 1;
    }
  }
  return crc;
}

// --- 新增：1-Wire ROM搜索核心函数 ---
// 返回值: 1 = 找到设备, 0 = 未找到/搜索完成
// rom_code: 用于存储找到的ROM地址
uint8_t DS18B20_Search(uint8_t *rom_code)
{
  uint8_t id_bit_number;
  uint8_t last_zero, rom_byte_number;
  uint8_t search_result;
  uint8_t id_bit, cmp_id_bit;
  uint8_t rom_byte_mask, search_direction;

  // 初始化搜索状态 (仅在首次搜索时)
  id_bit_number = 1;
  last_zero = 0;
  rom_byte_number = 0;
  rom_byte_mask = 1;
  search_result = 0;

  // 如果上一次搜索是最后一次，则重置状态开始新的搜索
  if (LastDeviceFlag)
  {
    LastDiscrepancy = 0;
    LastDeviceFlag = 0;
    LastFamilyDiscrepancy = 0;
    return 0; // 没有更多设备
  }

  // 1. 发送复位脉冲
  if (DS18B20_Reset() != 0)
  {
    // 总线无响应，重置搜索状态并返回错误
    LastDiscrepancy = 0;
    LastFamilyDiscrepancy = 0;
    LastDeviceFlag = 0;
    return 0;
  }

  // 2. 发送 ROM 搜索命令 (0xF0)
  DS18B20_WriteByte(0xF0);

  // 3. 循环搜索 ROM 的每一位 (64位)
  do
  {
    // 读取两位 (bit 和 complement bit)
    id_bit = DS18B20_ReadBit();
    cmp_id_bit = DS18B20_ReadBit();

    // 检查冲突
    if ((id_bit == 1) && (cmp_id_bit == 1))
    {
      // 没有设备响应此位，搜索失败
      break;
    }
    else
    {
      // 设备响应了
      if (id_bit != cmp_id_bit)
      {
        // 所有设备在这一位上值相同，直接选择该位
        search_direction = id_bit;
      }
      else
      {
        // 出现分歧 (Discrepancy)，即至少两个设备在这一位有不同值 (0和1)
        // 如果当前位 < 上次分歧位，选择上次确定的路径
        if (id_bit_number < LastDiscrepancy)
        {
          search_direction = ((rom_code[rom_byte_number] & rom_byte_mask) > 0);
        }
        else
        {
          // 如果等于上次分歧位，选择1路径
          // 如果大于上次分歧位，选择0路径 (优先探索0分支)
          search_direction = (id_bit_number == LastDiscrepancy);
        }

        // 如果选择0路径，记录下这个分歧点
        if (search_direction == 0)
        {
          last_zero = id_bit_number;
          // 如果是家族码内的分歧，也记录
          if (last_zero < 9)
            LastFamilyDiscrepancy = last_zero;
        }
      }

      // 存储选择的位到 ROM code 中
      if (search_direction == 1)
        rom_code[rom_byte_number] |= rom_byte_mask;
      else
        rom_code[rom_byte_number] &= ~rom_byte_mask;

      // 发送选择的位，让不匹配的设备进入休眠
      DS18B20_WriteBit(search_direction);

      // 移到下一位
      id_bit_number++;
      rom_byte_mask <<= 1;

      // 如果一个字节的8位都处理完了，移到下一个字节
      if (rom_byte_mask == 0)
      {
        rom_byte_number++;
        rom_byte_mask = 1;
      }
    }
  } while (rom_byte_number < 8); // 处理完8个字节 (64位)

  // 4. 检查搜索结果
  if (id_bit_number >= 65)
  { // 成功完成64位搜索
    // 更新下次搜索的分歧点
    LastDiscrepancy = last_zero;

    // 检查是否是最后一个设备
    if (LastDiscrepancy == 0)
    {
      LastDeviceFlag = 1; // 本次是最后一个设备
    }
    search_result = 1; // 成功找到一个设备
  }

  // 如果搜索失败或CRC校验失败，重置状态
  if (search_result == 0 || rom_code[0] == 0x00 || crc8(rom_code, 7) != rom_code[7])
  {
    LastDiscrepancy = 0;
    LastDeviceFlag = 0;
    LastFamilyDiscrepancy = 0;
    search_result = 0; // 标记为失败
  }

  return search_result;
}

// --- 新增：扫描总线上的所有DS18B20设备 ---
void DS18B20_ScanDevices(void)
{
  uint8_t id[8];
  g_num_sensors = 0; // 重置计数器

  // 重置搜索状态
  LastDiscrepancy = 0;
  LastDeviceFlag = 0;
  LastFamilyDiscrepancy = 0;

  // 循环搜索，直到找不到更多设备
  while (DS18B20_Search(id) && g_num_sensors < MAX_DS18B20_SENSORS)
  {
    // 检查是否是DS18B20家族码 (0x28)
    if (id[0] == 0x28)
    {
      // 检查CRC校验
      if (crc8(id, 7) == id[7])
      {
        memcpy(g_ds18b20_roms[g_num_sensors], id, 8);
        g_num_sensors++;
      }
    }
    // 如果需要支持其他家族码，可以在这里添加判断
  }
  // 可以在这里通过串口打印找到的传感器数量和ROM地址，用于调试
  char dbg_msg[100];
  sprintf(dbg_msg, "Found %d DS18B20 sensors.\r\n", g_num_sensors);
  HAL_UART_Transmit(&huart3, (uint8_t *)dbg_msg, strlen(dbg_msg), 100);
  for (int i = 0; i < g_num_sensors; i++)
  {
    sprintf(dbg_msg, "Sensor %d ROM: %02X%02X%02X%02X%02X%02X%02X%02X\r\n", i,
            g_ds18b20_roms[i][7], g_ds18b20_roms[i][6], g_ds18b20_roms[i][5], g_ds18b20_roms[i][4],
            g_ds18b20_roms[i][3], g_ds18b20_roms[i][2], g_ds18b20_roms[i][1], g_ds18b20_roms[i][0]);
    HAL_UART_Transmit(&huart3, (uint8_t *)dbg_msg, strlen(dbg_msg), 200);
    HAL_Delay(10); // 短暂延时避免串口发送过快
  }
}

// --- 新增：选择指定ROM地址的设备 ---
void DS18B20_Select(const uint8_t *rom_code)
{
  DS18B20_WriteByte(0x55); // Match ROM command
  for (uint8_t i = 0; i < 8; i++)
  {
    DS18B20_WriteByte(rom_code[i]);
  }
}

// --- 新增：向总线上所有设备发送命令 (使用 Skip ROM) ---
void DS18B20_SkipRom(void)
{
  DS18B20_WriteByte(0xCC); // Skip ROM command
}

// --- 修改：读取指定ROM地址的温度值 ---
// 返回值：读取到的温度，或特定错误值 (例如 -999.0)
float DS18B20_GetTemp(const uint8_t *rom_code)
{
  uint8_t tempL, tempH;
  uint16_t temp;
  float result = -999.0; // 默认错误值

  // 1. 复位并选择指定设备
  if (DS18B20_Reset() != 0)
    return result; // 复位失败
  DS18B20_Select(rom_code);

  // 2. 发送读取暂存器命令 (0xBE)
  DS18B20_WriteByte(0xBE);

  // 3. 读取暂存器内容 (前两个字节是温度)
  //    注意：实际应用中应读取全部9个字节并校验CRC
  tempL = DS18B20_ReadByte();
  tempH = DS18B20_ReadByte();
  // 读取剩余字节（可选，用于CRC校验）
  // uint8_t scratchpad[9];
  // scratchpad[0] = tempL;
  // scratchpad[1] = tempH;
  // for(int i=2; i<9; i++) {
  //     scratchpad[i] = DS18B20_ReadByte();
  // }
  // if (crc8(scratchpad, 8) != scratchpad[8]) {
  //     // CRC校验失败
  //     return -998.0; // 返回不同的错误码
  // }

  // 4. 计算温度
  temp = (tempH << 8) | tempL;

  // 处理负温度 (符号扩展)
  if (temp & 0x8000)
  {
    temp = ~temp + 1; // 取反加一
    result = -(float)temp / 16.0;
  }
  else
  {
    result = (float)temp / 16.0;
  }

  // 检查温度是否在合理范围 (DS18B20范围 -55 到 +125)
  // 85度是上电默认值，可能表示读取错误
  if (result == 85.0 || result < -55.0 || result > 125.0)
  {
    // 可能读取错误，可以返回特定错误值或上次有效值
    // return -997.0;
  }

  return result;
}

// 启动所有连接设备的温度转换 ---
uint8_t DS18B20_StartConversionAll(void)
{
  if (DS18B20_Reset() != 0)
    return 1;              // 复位失败
  DS18B20_SkipRom();       // 使用 Skip ROM 命令
  DS18B20_WriteByte(0x44); // 启动温度转换命令
  return 0;                // 成功启动
}

// --- 新增：串口3接收中断回调函数 ---
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART3)
  {
    // 检查是否接收到换行符或回车符
    if (uart3_current_char == '\n' || uart3_current_char == '\r')
    {
      uart3_rx_buffer[uart3_rx_index] = '\0'; // 添加字符串结束符
      uart3_rx_complete = 1; // 标记接收完成
      uart3_rx_index = 0;    // 重置索引
    }
    else if (uart3_rx_index >= UART3_RX_BUFFER_SIZE - 2) // 留一个位置给\0
    {
      uart3_rx_buffer[uart3_rx_index] = '\0'; // 添加字符串结束符
      uart3_rx_complete = 1; // 标记接收完成
      uart3_rx_index = 0;    // 重置索引
    }
    else
    {
      // 存储接收到的字符
      uart3_rx_buffer[uart3_rx_index] = uart3_current_char;
      uart3_rx_index++; // 移动到下一个位置
    }
    
    // 重新启动接收中断，继续接收下一个字符
    HAL_UART_Receive_IT(&huart3, &uart3_current_char, 1);
  }
}

// --- 新增：解析串口命令 ---
void Parse_UART3_Command(void)
{
  if (!uart3_rx_complete) return;
  
  char *command = (char *)uart3_rx_buffer;
  char response[100];
  
  // 添加详细调试信息 - 显示缓冲区内容
  sprintf(response, "RX Complete! Buffer: [");
  HAL_UART_Transmit(&huart3, (uint8_t *)response, strlen(response), 200);
  
  for (int i = 0; i < uart3_rx_index; i++) {
    sprintf(response, "%02X ", uart3_rx_buffer[i]);
    HAL_UART_Transmit(&huart3, (uint8_t *)response, strlen(response), 200);
  }
  
  sprintf(response, "] String: [%s] (len=%d, index=%d)\r\n", command, strlen(command), uart3_rx_index);
  HAL_UART_Transmit(&huart3, (uint8_t *)response, strlen(response), 200);
  
  // 解析 "on" 命令
  if (strncmp(command, "on", 2) == 0)
  {
    uint32_t duration = atoi(command + 2); // 提取数字部分
    sprintf(response, "Parsed duration: %lu\r\n", duration);
    HAL_UART_Transmit(&huart3, (uint8_t *)response, strlen(response), 200);
    
    if (duration > 0 && duration <= 9999) // 限制最大时间
    {
      relay_timer_count = duration;
      relay_timer_active = 1;
      relay_force_state = 1;
      relay_temp_control = 0; // 禁用温度控制
      RELAY_Control(1);       // 立即打开继电器
      
      sprintf(response, "Relay ON for %lu seconds\r\n", duration);
      HAL_UART_Transmit(&huart3, (uint8_t *)response, strlen(response), 200);
    }
    else
    {
      sprintf(response, "Invalid duration: %s (parsed: %lu)\r\n", command, duration);
      HAL_UART_Transmit(&huart3, (uint8_t *)response, strlen(response), 200);
    }
  }
  // 解析 "off" 命令
  else if (strncmp(command, "off", 3) == 0)
  {
    uint32_t duration = atoi(command + 3); // 提取数字部分
    if (duration > 0 && duration <= 9999) // 限制最大时间
    {
      relay_timer_count = duration;
      relay_timer_active = 1;
      relay_force_state = 0;
      relay_temp_control = 0; // 禁用温度控制
      RELAY_Control(0);       // 立即关闭继电器
      
      sprintf(response, "Relay OFF for %lu seconds\r\n", duration);
      HAL_UART_Transmit(&huart3, (uint8_t *)response, strlen(response), 200);
    }
    else
    {
      sprintf(response, "Invalid duration: %s\r\n", command);
      HAL_UART_Transmit(&huart3, (uint8_t *)response, strlen(response), 200);
    }
  }
  // 解析 "auto" 命令 - 恢复温度控制
  else if (strncmp(command, "auto", 4) == 0)
  {
    relay_timer_active = 0;
    relay_temp_control = 1; // 重新启用温度控制
    sprintf(response, "Auto temperature control enabled\r\n");
    HAL_UART_Transmit(&huart3, (uint8_t *)response, strlen(response), 200);
  }
  else
  {
    sprintf(response, "Unknown command: %s\r\n", command);
    HAL_UART_Transmit(&huart3, (uint8_t *)response, strlen(response), 200);
  }
  
  uart3_rx_complete = 0; // 重置完成标志
}

// --- 新增：更新继电器定时器 ---
void Update_Relay_Timer(void)
{
  if (relay_timer_active && relay_timer_count > 0)
  {
    relay_timer_count--;
    
    // 定时器到期
    if (relay_timer_count == 0)
    {
      relay_timer_active = 0;
      relay_temp_control = 1; // 重新启用温度控制
      
      char response[50];
      sprintf(response, "Timer expired, auto control enabled\r\n");
      HAL_UART_Transmit(&huart3, (uint8_t *)response, strlen(response), 200);
    }
  }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  char msg[100]; // 增加缓冲区大小以容纳ROM地址
  char msg2[100]; // lora发送信息
  char relay_msg[50]; // 继电器控制信息
  float temperature;
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_USART3_UART_Init();
  MX_I2C1_Init();
  /* USER CODE BEGIN 2 */  RELAY_Init();
  OLED_Init();  // 初始化OLED

  // --- 新增：初始化PA2/PA3为输入上拉 ---
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = GPIO_PIN_2|GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  HAL_Delay(100); // 等待总线稳定

  // 启动串口3接收中断
  HAL_UART_Receive_IT(&huart3, &uart3_current_char, 1);

  // 在OLED上显示欢迎信息
  OLED_ShowString(0, 0, "DS18B20 Temperature", 8);
  OLED_ShowString(0, 16, "System Initializing", 8);
  OLED_Refresh();
    HAL_Delay(1000); // 显示欢迎信息一段时间
  HAL_UART_Transmit(&huart1, (uint8_t *)"DS18B20 Multi-Sensor Test\r\n", strlen("DS18B20 Multi-Sensor Test\r\n"), 100);

  // --- 修改：扫描设备 ---
  DS18B20_ScanDevices();

    // 在OLED上显示传感器信息
  OLED_DisplaySensorInfo(g_num_sensors);
  HAL_Delay(2000);
  OLED_Clear(); // 清屏

  if (g_num_sensors == 0)
  {
    HAL_UART_Transmit(&huart1, (uint8_t *)"No DS18B20 sensors found!\r\n", strlen("No DS18B20 sensors found!\r\n"), 100);
    while (1)
      ; // 没有传感器则停止
  }
  // char msg[50];
  // float temperature;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */  
  uint32_t last_task_tick = 0;
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    // 处理串口3接收到的命令
    Parse_UART3_Command();
    
    // 更新继电器定时器（每秒调用一次）
    static uint32_t last_timer_update = 0;
    if (HAL_GetTick() - last_timer_update >= 1000) // 每1000ms更新一次
    {
      Update_Relay_Timer();
      last_timer_update = HAL_GetTick();
    }

    // --- 非阻塞定时任务：每2秒采集温度和控制继电器 ---
    if (HAL_GetTick() - last_task_tick >= 2000) {
      last_task_tick = HAL_GetTick();
      // 1. 启动所有传感器的温度转换
      if (DS18B20_StartConversionAll() == 0)
      {     
        HAL_Delay(750); // 等待转换完成（可进一步优化为非阻塞）
        // 3. 依次读取每个传感器的温度
        for (uint8_t i = 0; i < g_num_sensors; i++)
        {
          float temperature = DS18B20_GetTemp(g_ds18b20_roms[i]);
          temperatureArray[i] = temperature;
        }
        // 检查温度并控制继电器（仅在温度控制启用时）
        uint8_t relay_status = 0;
        if (relay_temp_control) 
        {
          for (uint8_t i = 0; i < g_num_sensors; i++)
          {
              if(temperatureArray[i] > temp_threshold)
              {
                RELAY_Control(1); // 温度超过阈值，打开继电器
                sprintf(relay_msg, "ON");
                relay_status = 1;
                break;
              }
          }
          if (!relay_status) {
            RELAY_Control(0); // 温度均低于阈值，关闭继电器
            sprintf(relay_msg, "OFF");
          }
        }
        else
        {
          relay_status = relay_force_state;
          sprintf(relay_msg, relay_status ? "ON" : "OFF");
        }
        // OLED显示温度和继电器状态，显示当前阈值
        OLED_DisplayTemperature(
            temperatureArray[0], 
            (g_num_sensors > 1) ? temperatureArray[1] : -999.0,
            (g_num_sensors > 2) ? temperatureArray[2] : -999.0,
            relay_status,
            temp_threshold
        );
        // 格式化并发送到Lora
        strcpy(msg2, "");
        for (uint8_t i = 0; i < 3; i++)
        {
            char temp_msg[50];
            sprintf(temp_msg, "%.2f;", temperatureArray[i]);
            strcat(msg2, temp_msg);
        }
        strcat(msg2, relay_msg); // 继电器状态
        strcat(msg2, ";\r\n");
        HAL_UART_Transmit(&huart3, (uint8_t *)msg2, strlen(msg2), 200); // 发送到Lora
      }
      else
      {
        sprintf(msg, "Failed to start conversion.\r\n");
        HAL_UART_Transmit(&huart1, (uint8_t *)msg, strlen(msg), 100);
      }
    }

    // --- 按键扫描与阈值调节（实时） ---
    uint8_t key_pa2 = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_2);
    uint8_t key_pa3 = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_3);
    uint32_t now = HAL_GetTick();
    // PA2: 阈值减1
    if (key_pa2 == 0 && key_pa2_last == 1 && (now - key_debounce_tick_pa2 > KEY_DEBOUNCE_MS)) {
      key_debounce_tick_pa2 = now;
      if (temp_threshold > TEMP_THRESHOLD_MIN) {
        temp_threshold -= TEMP_THRESHOLD_STEP;
        if (temp_threshold < TEMP_THRESHOLD_MIN) temp_threshold = TEMP_THRESHOLD_MIN;
        // 不再单独刷新OLED，统一在主定时任务里刷新
        // OLED_ShowString(0, 48, "Threshold-", 8);
        // OLED_ShowNum(80, 48, (int)temp_threshold, 2, 8);
        // OLED_Refresh();
        HAL_Delay(200); // 简单消抖
      }
    }
    key_pa2_last = key_pa2;
    // PA3: 阈值加1
    if (key_pa3 == 0 && key_pa3_last == 1 && (now - key_debounce_tick_pa3 > KEY_DEBOUNCE_MS)) {
      key_debounce_tick_pa3 = now;
      if (temp_threshold < TEMP_THRESHOLD_MAX) {
        temp_threshold += TEMP_THRESHOLD_STEP;
        if (temp_threshold > TEMP_THRESHOLD_MAX) temp_threshold = TEMP_THRESHOLD_MAX;
        // 不再单独刷新OLED，统一在主定时任务里刷新
        // OLED_ShowString(0, 48, "Threshold+", 8);
        // OLED_ShowNum(80, 48, (int)temp_threshold, 2, 8);
        // OLED_Refresh();
        HAL_Delay(200); // 简单消抖
      }
    }
    key_pa3_last = key_pa3;

    // 检查温度并控制继电器（仅在温度控制启用时）
    uint8_t relay_status = 0;
    if (relay_temp_control) 
    {
      for (uint8_t i = 0; i < g_num_sensors; i++)
      {
          if(temperatureArray[i] > temp_threshold)
          {
            RELAY_Control(1); // 温度超过阈值，打开继电器
            sprintf(relay_msg, "ON");
            relay_status = 1;
            break;
          }
      }
      if (!relay_status) {
        RELAY_Control(0); // 温度均低于阈值，关闭继电器
        sprintf(relay_msg, "OFF");
      }
    }
    else
    {
      // 使用强制状态
      relay_status = relay_force_state;
      sprintf(relay_msg, relay_status ? "ON" : "OFF");
    }
    // OLED显示温度和继电器状态，显示当前阈值
    OLED_DisplayTemperature(
        temperatureArray[0], 
        (g_num_sensors > 1) ? temperatureArray[1] : -999.0,
        (g_num_sensors > 2) ? temperatureArray[2] : -999.0,
        relay_status,
        temp_threshold
    );
    // 格式化并发送到Lora
    strcpy(msg2, "");
    for (uint8_t i = 0; i < 3; i++)
    {
        char temp_msg[50];
        sprintf(temp_msg, "%.2f;", temperatureArray[i]);
        strcat(msg2, temp_msg);
    }
    strcat(msg2, relay_msg); // 继电器状态
    strcat(msg2, ";\r\n");
    HAL_UART_Transmit(&huart3, (uint8_t *)msg2, strlen(msg2), 200); // 发送到Lora
//    }
//    else
//    {
//      sprintf(msg, "Failed to start conversion.\r\n");
//      HAL_UART_Transmit(&huart1, (uint8_t *)msg, strlen(msg), 100);
//    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
