/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define RSRV_MODEM_PWRKEY_Pin GPIO_PIN_1
#define RSRV_MODEM_PWRKEY_GPIO_Port GPIOA
#define MODEM_TX_Pin GPIO_PIN_2
#define MODEM_TX_GPIO_Port GPIOA
#define MODEM_RX_Pin GPIO_PIN_3
#define MODEM_RX_GPIO_Port GPIOA
#define TP_MISO_Pin GPIO_PIN_4
#define TP_MISO_GPIO_Port GPIOC
#define TP_PEN_Pin GPIO_PIN_5
#define TP_PEN_GPIO_Port GPIOC
#define TP_PEN_EXTI_IRQn EXTI9_5_IRQn
#define SENSOR_PWR_EN_Pin GPIO_PIN_0
#define SENSOR_PWR_EN_GPIO_Port GPIOB
#define MODEM_PWR_EN_Pin GPIO_PIN_1
#define MODEM_PWR_EN_GPIO_Port GPIOB
#define LCD_D4_Pin GPIO_PIN_7
#define LCD_D4_GPIO_Port GPIOE
#define LCD_D5_Pin GPIO_PIN_8
#define LCD_D5_GPIO_Port GPIOE
#define LCD_D6_Pin GPIO_PIN_9
#define LCD_D6_GPIO_Port GPIOE
#define LCD_D7_Pin GPIO_PIN_10
#define LCD_D7_GPIO_Port GPIOE
#define LCD_D8_Pin GPIO_PIN_11
#define LCD_D8_GPIO_Port GPIOE
#define LCD_D9_Pin GPIO_PIN_12
#define LCD_D9_GPIO_Port GPIOE
#define LCD_D10_Pin GPIO_PIN_13
#define LCD_D10_GPIO_Port GPIOE
#define LCD_D11_Pin GPIO_PIN_14
#define LCD_D11_GPIO_Port GPIOE
#define LCD_D12_Pin GPIO_PIN_15
#define LCD_D12_GPIO_Port GPIOE
#define RS485_TX_Pin GPIO_PIN_10
#define RS485_TX_GPIO_Port GPIOB
#define RS485_RX_Pin GPIO_PIN_11
#define RS485_RX_GPIO_Port GPIOB
#define TP_CS_Pin GPIO_PIN_12
#define TP_CS_GPIO_Port GPIOB
#define TP_CLK_Pin GPIO_PIN_13
#define TP_CLK_GPIO_Port GPIOB
#define TP_MOSI_Pin GPIO_PIN_14
#define TP_MOSI_GPIO_Port GPIOB
#define LCD_BL_Pin GPIO_PIN_15
#define LCD_BL_GPIO_Port GPIOB
#define LCD_D13_Pin GPIO_PIN_8
#define LCD_D13_GPIO_Port GPIOD
#define LCD_D14_Pin GPIO_PIN_9
#define LCD_D14_GPIO_Port GPIOD
#define LCD_D15_Pin GPIO_PIN_10
#define LCD_D15_GPIO_Port GPIOD
#define LCD_RS_Pin GPIO_PIN_11
#define LCD_RS_GPIO_Port GPIOD
#define LCD_D0_Pin GPIO_PIN_14
#define LCD_D0_GPIO_Port GPIOD
#define LCD_D1_Pin GPIO_PIN_15
#define LCD_D1_GPIO_Port GPIOD
#define SDIO_D0_Pin GPIO_PIN_8
#define SDIO_D0_GPIO_Port GPIOC
#define RSRV_SDIO_D1_Pin GPIO_PIN_9
#define RSRV_SDIO_D1_GPIO_Port GPIOC
#define DEBUG_TX_Pin GPIO_PIN_9
#define DEBUG_TX_GPIO_Port GPIOA
#define DEBUG_RX_Pin GPIO_PIN_10
#define DEBUG_RX_GPIO_Port GPIOA
#define RSRV_USB_DM_Pin GPIO_PIN_11
#define RSRV_USB_DM_GPIO_Port GPIOA
#define RSRV_USB_DP_Pin GPIO_PIN_12
#define RSRV_USB_DP_GPIO_Port GPIOA
#define SWDIO_Pin GPIO_PIN_13
#define SWDIO_GPIO_Port GPIOA
#define SWCLK_Pin GPIO_PIN_14
#define SWCLK_GPIO_Port GPIOA
#define RSRV_SDIO_D2_Pin GPIO_PIN_10
#define RSRV_SDIO_D2_GPIO_Port GPIOC
#define RSRV_SDIO_D3_Pin GPIO_PIN_11
#define RSRV_SDIO_D3_GPIO_Port GPIOC
#define SDIO_CK_Pin GPIO_PIN_12
#define SDIO_CK_GPIO_Port GPIOC
#define LCD_D2_Pin GPIO_PIN_0
#define LCD_D2_GPIO_Port GPIOD
#define LCD_D3_Pin GPIO_PIN_1
#define LCD_D3_GPIO_Port GPIOD
#define SDIO_CMD_Pin GPIO_PIN_2
#define SDIO_CMD_GPIO_Port GPIOD
#define LCD_RD_Pin GPIO_PIN_4
#define LCD_RD_GPIO_Port GPIOD
#define LCD_WR_Pin GPIO_PIN_5
#define LCD_WR_GPIO_Port GPIOD
#define LCD_RESET_Pin GPIO_PIN_6
#define LCD_RESET_GPIO_Port GPIOD
#define LCD_CS_Pin GPIO_PIN_7
#define LCD_CS_GPIO_Port GPIOD
#define RSRV_I2C1_SCL_INA219_Pin GPIO_PIN_8
#define RSRV_I2C1_SCL_INA219_GPIO_Port GPIOB
#define RSRV_I2C1_SDA_INA219_Pin GPIO_PIN_9
#define RSRV_I2C1_SDA_INA219_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
