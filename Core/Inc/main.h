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
#include "stm32l4xx_hal.h"

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
#define Batt_Sense_Pin GPIO_PIN_0
#define Batt_Sense_GPIO_Port GPIOA
#define Brake_In_Pin GPIO_PIN_1
#define Brake_In_GPIO_Port GPIOA
#define Five_Sense_Pin GPIO_PIN_2
#define Five_Sense_GPIO_Port GPIOA
#define SDC_1_Pin GPIO_PIN_3
#define SDC_1_GPIO_Port GPIOA
#define SDC_3_Pin GPIO_PIN_4
#define SDC_3_GPIO_Port GPIOA
#define LV_Curr_Pin GPIO_PIN_5
#define LV_Curr_GPIO_Port GPIOA
#define HC_Curr_Pin GPIO_PIN_6
#define HC_Curr_GPIO_Port GPIOA
#define RAD_Ctrl_Pin GPIO_PIN_7
#define RAD_Ctrl_GPIO_Port GPIOA
#define SDC_2_Pin GPIO_PIN_0
#define SDC_2_GPIO_Port GPIOB
#define BMS_Pin GPIO_PIN_1
#define BMS_GPIO_Port GPIOB
#define BSPD_Pin GPIO_PIN_8
#define BSPD_GPIO_Port GPIOA
#define IMD_Pin GPIO_PIN_9
#define IMD_GPIO_Port GPIOA
#define FANS_Ctrl_Pin GPIO_PIN_10
#define FANS_Ctrl_GPIO_Port GPIOA
#define LD3_Pin GPIO_PIN_3
#define LD3_GPIO_Port GPIOB
#define PUMP_Ctrl_Pin GPIO_PIN_4
#define PUMP_Ctrl_GPIO_Port GPIOB
#define DRS_Ctrl_Pin GPIO_PIN_5
#define DRS_Ctrl_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
