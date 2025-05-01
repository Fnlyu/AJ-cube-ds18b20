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
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>

#define DS18B20_PORT GPIOA
#define DS18B20_PIN GPIO_PIN_0
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

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */


// 微秒级延时函数（72MHz时钟下粗略延时）
void Delay_us(uint16_t us) {
    us *= 6;  // 粗略校准值
    while(us--) {
        __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP();
    }
}

// 写1位数据（修正参数类型）
void DS18B20_WriteBit(uint8_t bit) {
    HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_RESET);
    Delay_us(1);
    HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, bit ? GPIO_PIN_SET : GPIO_PIN_RESET);
    Delay_us(60);
    HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_SET);
}

// 读1位数据（修正返回类型）
uint8_t DS18B20_ReadBit(void) {
    uint8_t bit = 0;
    HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_RESET);
    Delay_us(1);
    HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_SET);
    Delay_us(10);
    bit = HAL_GPIO_ReadPin(DS18B20_PORT, DS18B20_PIN);
    Delay_us(50);
    return bit; // 返回实际读取值（0或1）
}

// 向DS18B20写入1字节（逐位写入）
void DS18B20_WriteByte(uint8_t data) {
    for(uint8_t i = 0; i < 8; i++) {
        DS18B20_WriteBit(data & 0x01);  // 从最低位开始写入
        data >>= 1;                     // 准备下一位
    }
}

// 从DS18B20读取1字节（逐位读取）
uint8_t DS18B20_ReadByte(void) {
    uint8_t data = 0;
    for(uint8_t i = 0; i < 8; i++) {
        data >>= 1;                     // 先右移，从最低位开始接收
        if(DS18B20_ReadBit()) {         // 读取位值
            data |= 0x80;               // 如果读到1，设置最高位
        }
    }
    return data;
}
// 复位DS18B20
uint8_t DS18B20_Reset(void) {
    uint8_t status;
    HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_RESET);
    Delay_us(480);       // 保持480us低电平
    HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_SET);
    Delay_us(60);        // 等待15-60us
    status = HAL_GPIO_ReadPin(DS18B20_PORT, DS18B20_PIN);
    Delay_us(420);       // 等待存在脉冲结束
    return status;       // 0=存在脉冲成功
}


// 读取温度值
float DS18B20_GetTemp(void) {
    uint8_t tempL, tempH;
    uint16_t temp;
    float result;

    DS18B20_Reset();
    DS18B20_WriteByte(0xCC);  // 跳过ROM
    DS18B20_WriteByte(0x44);  // 启动温度转换
    HAL_Delay(750);           // 等待转换完成

    DS18B20_Reset();
    DS18B20_WriteByte(0xCC);
    DS18B20_WriteByte(0xBE);  // 读取温度寄存器

    tempL = DS18B20_ReadByte();
    tempH = DS18B20_ReadByte();
    temp = (tempH << 8) | tempL;
    result = temp / 16.0;
    return result;
}
/* USER CODE END 0 */
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

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
  /* USER CODE BEGIN 2 */
  char msg[50];
  float temperature;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */


	      temperature = DS18B20_GetTemp();
	      sprintf(msg, "Temperature: %.2f C\r\n", temperature);
	      HAL_UART_Transmit(&huart1, (uint8_t*)msg, strlen(msg), 100);
	      HAL_Delay(1000);
    /* USER CODE BEGIN 3 */
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
