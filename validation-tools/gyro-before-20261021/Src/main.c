/**
  ******************************************************************************
  * @file    main.c
  * @author  MCU Application Team
  * @brief   Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2023 Puya Semiconductor Co.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by Puya under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2016 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "py32_jacdac_board.h"
#include "py32_jacdac_runtime.h"
#include "qmi8656_probe.h"

/* Private variables ---------------------------------------------------------*/
static I2C_HandleTypeDef g_qmi_i2c;

/* Private function prototypes -----------------------------------------------*/
static void APP_JacdacBusConfig(void);
static void APP_QmiI2cBusRecover(void);
static void APP_QmiI2cBusConfig(void);
static HAL_StatusTypeDef APP_QmiI2cInit(void);
static void APP_QmiProbeStartupScan(void);
static void APP_SystemClockConfig(void);

/**
  * @brief  Main program.
  * @retval int
  */
int main(void)
{
  HAL_Init();
  if (APP_ENABLE_48MHZ_CLOCK != 0u)
  {
    APP_SystemClockConfig();
  }

  APP_JacdacBusConfig();
  APP_QmiI2cBusRecover();
  APP_QmiI2cBusConfig();
  if (APP_QmiI2cInit() != HAL_OK)
  {
    APP_ErrorHandler();
  }
  APP_QmiProbeBindBus(&g_qmi_i2c);
  APP_QmiProbeStartupScan();

  APP_JacdacRuntimeInit();

  while (1)
  {
    APP_JacdacRuntimeProcess();
  }
}

/**
  * @brief  Run the PY32 from 48MHz HSI to improve software Jacdac timing margin.
  * @param  None
  * @retval None
  */
static void APP_SystemClockConfig(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  RCC_OscInitStruct.OscillatorType =
      RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_HSI | RCC_OSCILLATORTYPE_LSI | RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_48MHz;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS_DISABLE;
  RCC_OscInitStruct.LSIState = RCC_LSI_OFF;
  RCC_OscInitStruct.LSEState = RCC_LSE_OFF;

  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    APP_ErrorHandler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSISYS;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    APP_ErrorHandler();
  }
}

/**
  * @brief  Jacdac bus pin configuration.
  * @param  None
  * @retval None
  */
static void APP_JacdacBusConfig(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  APP_JACDAC_GPIO_CLK_ENABLE();

  GPIO_InitStruct.Pin = APP_JACDAC_GPIO_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

  HAL_GPIO_Init(APP_JACDAC_GPIO_PORT, &GPIO_InitStruct);
  HAL_NVIC_SetPriority(EXTI4_15_IRQn, 0u, 0u);
  HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);
}

static void APP_QmiI2cRecoveryDelay(void)
{
  volatile uint32_t i;

  for (i = 0u; i < 120u; ++i)
  {
    __NOP();
  }
}

static void APP_QmiI2cBusRecover(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  uint32_t pulse;

  APP_QMI_I2C_GPIO_CLK_ENABLE();

  GPIO_InitStruct.Pin = APP_QMI_I2C_SCL_PIN | APP_QMI_I2C_SDA_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(APP_QMI_I2C_GPIO_PORT, &GPIO_InitStruct);

  HAL_GPIO_WritePin(APP_QMI_I2C_GPIO_PORT, APP_QMI_I2C_SCL_PIN | APP_QMI_I2C_SDA_PIN, GPIO_PIN_SET);
  APP_QmiI2cRecoveryDelay();

  for (pulse = 0u; pulse < 16u; ++pulse)
  {
    if (HAL_GPIO_ReadPin(APP_QMI_I2C_GPIO_PORT, APP_QMI_I2C_SDA_PIN) == GPIO_PIN_SET)
    {
      break;
    }

    HAL_GPIO_WritePin(APP_QMI_I2C_GPIO_PORT, APP_QMI_I2C_SCL_PIN, GPIO_PIN_RESET);
    APP_QmiI2cRecoveryDelay();
    HAL_GPIO_WritePin(APP_QMI_I2C_GPIO_PORT, APP_QMI_I2C_SCL_PIN, GPIO_PIN_SET);
    APP_QmiI2cRecoveryDelay();
  }

  HAL_GPIO_WritePin(APP_QMI_I2C_GPIO_PORT, APP_QMI_I2C_SDA_PIN, GPIO_PIN_RESET);
  APP_QmiI2cRecoveryDelay();
  HAL_GPIO_WritePin(APP_QMI_I2C_GPIO_PORT, APP_QMI_I2C_SCL_PIN, GPIO_PIN_SET);
  APP_QmiI2cRecoveryDelay();
  HAL_GPIO_WritePin(APP_QMI_I2C_GPIO_PORT, APP_QMI_I2C_SDA_PIN, GPIO_PIN_SET);
  APP_QmiI2cRecoveryDelay();
}

/**
  * @brief  QMI8656 I2C bus GPIO configuration.
  * @param  None
  * @retval None
  */
static void APP_QmiI2cBusConfig(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  APP_QMI_I2C_GPIO_CLK_ENABLE();

  GPIO_InitStruct.Pin = APP_QMI_I2C_SCL_PIN | APP_QMI_I2C_SDA_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF6_I2C1;

  HAL_GPIO_Init(APP_QMI_I2C_GPIO_PORT, &GPIO_InitStruct);
}

/**
  * @brief  Initialize I2C1 on PB3/PB4 for QMI8656 access.
  * @param  None
  * @retval HAL status
  */
static HAL_StatusTypeDef APP_QmiI2cInit(void)
{
  APP_QMI_I2C_CLK_ENABLE();
  HAL_SYSCFG_EnableI2CFastModePlus(SYSCFG_I2C_FMP_PB3 | SYSCFG_I2C_FMP_PB4);

  g_qmi_i2c.Instance = APP_QMI_I2C_INSTANCE;
  g_qmi_i2c.Init.ClockSpeed = 100000;
  g_qmi_i2c.Init.DutyCycle = I2C_DUTYCYCLE_16_9;
  g_qmi_i2c.Init.OwnAddress1 = 0;
  g_qmi_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  g_qmi_i2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

  return HAL_I2C_Init(&g_qmi_i2c);
}

static void APP_QmiProbeStartupScan(void)
{
  APP_QmiProbeResult result;

  APP_QmiProbeScan(&g_qmi_i2c, &result);
}

/**
  * @brief  Error executing function.
  * @param  None
  * @retval None
  */
void APP_ErrorHandler(void)
{
  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;

  while (1)
  {
  }
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT Puya *****END OF FILE******************/
