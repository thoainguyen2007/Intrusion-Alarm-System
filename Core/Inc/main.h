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
#include "stm32f1xx_hal.h"

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
#define LED_STATUS_Pin GPIO_PIN_13
#define LED_STATUS_GPIO_Port GPIOC
/* REED_IN is the legacy label for the KY-003 Hall door input on PA0. */
#define REED_IN_Pin GPIO_PIN_0
#define REED_IN_GPIO_Port GPIOA
#define REED_IN_EXTI_IRQn EXTI0_IRQn
#define PIR_IN_Pin GPIO_PIN_1
#define PIR_IN_GPIO_Port GPIOA
#define PIR_IN_EXTI_IRQn EXTI1_IRQn
#define VIR_IN_Pin GPIO_PIN_2
#define VIR_IN_GPIO_Port GPIOA
#define VIR_IN_EXTI_IRQn EXTI2_IRQn
#define CS_Pin GPIO_PIN_4
#define CS_GPIO_Port GPIOA
#define R1_Pin GPIO_PIN_0
#define R1_GPIO_Port GPIOB
#define R2_Pin GPIO_PIN_1
#define R2_GPIO_Port GPIOB
#define R3_Pin GPIO_PIN_10
#define R3_GPIO_Port GPIOB
#define R4_Pin GPIO_PIN_11
#define R4_GPIO_Port GPIOB
#define C1_Pin GPIO_PIN_12
#define C1_GPIO_Port GPIOB
#define C2_Pin GPIO_PIN_13
#define C2_GPIO_Port GPIOB
#define C3_Pin GPIO_PIN_14
#define C3_GPIO_Port GPIOB
#define C4_Pin GPIO_PIN_15
#define C4_GPIO_Port GPIOB
#define BUZ_Pin GPIO_PIN_8
#define BUZ_GPIO_Port GPIOA

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
