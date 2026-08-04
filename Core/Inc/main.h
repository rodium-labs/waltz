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
extern ADC_HandleTypeDef hadc1;
extern SPI_HandleTypeDef hspi1;
extern TIM_HandleTypeDef htim2;
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
#define INTERNAL_LED_Pin GPIO_PIN_13
#define INTERNAL_LED_GPIO_Port GPIOC

/* ST7789P3 wiring - 8 pin module, 4-wire SPI.
 *
 *   module   Black Pill      alternate function
 *   ------   ------------    ------------------
 *   GND      G
 *   VCC      3V3             (2.8 - 3.3 V, do NOT feed 5 V)
 *   SCL      PA5             SPI1_SCK   (AF5)
 *   SDA      PA7             SPI1_MOSI  (AF5)
 *   RES      PB1             GPIO out
 *   DC       PB0             GPIO out
 *   CS       PA4             GPIO out
 *   BLK      PB10            TIM2_CH3   (AF1, PWM brightness)
 */
#define LCD_CS_Pin GPIO_PIN_4
#define LCD_CS_GPIO_Port GPIOA
#define LCD_SCK_Pin GPIO_PIN_5
#define LCD_SCK_GPIO_Port GPIOA
#define LCD_MOSI_Pin GPIO_PIN_7
#define LCD_MOSI_GPIO_Port GPIOA
#define LCD_DC_Pin GPIO_PIN_0
#define LCD_DC_GPIO_Port GPIOB
#define LCD_RST_Pin GPIO_PIN_1
#define LCD_RST_GPIO_Port GPIOB
#define LCD_BLK_Pin GPIO_PIN_10
#define LCD_BLK_GPIO_Port GPIOB

/* Five buttons, all on GPIOA so one IDR read scans the lot. Each button ties
 * its pin to GND; the pins run with internal pull-ups, so pressed reads low.
 * None of these clash with the I2S2 (PB12/13/15) or SPI3 SD (PB3/4/5, PA15)
 * pins that the audio side will want later.
 *
 *   VOL-   PREV   PLAY   NEXT   VOL+
 *   PA0    PA1    PA2    PA3    PA6
 *
 * PA6 is SPI1_MISO, free because the panel is write-only. On Black Pill
 * revisions that wire an on-board KEY button to PA0, that button can serve as
 * VOL- directly.
 */
#define BTN_GPIO_Port GPIOA
#define BTN_VOL_DOWN_Pin GPIO_PIN_0
#define BTN_PREV_Pin GPIO_PIN_1
#define BTN_PLAY_Pin GPIO_PIN_2
#define BTN_NEXT_Pin GPIO_PIN_3
#define BTN_VOL_UP_Pin GPIO_PIN_6
#define BTN_ALL_Pins                                                           \
  (BTN_VOL_DOWN_Pin | BTN_PREV_Pin | BTN_PLAY_Pin | BTN_NEXT_Pin |             \
   BTN_VOL_UP_Pin)

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
