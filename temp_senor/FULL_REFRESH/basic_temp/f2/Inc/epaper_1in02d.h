#ifndef EPAPER_1IN02D_H
#define EPAPER_1IN02D_H

#include <stdint.h>
#include "stm32f3xx_hal.h"
#include "main.h"

/*
 * 顯示給使用者看的邏輯座標：
 * x: 0~127
 * y: 0~79
 */
#define EPD_LOG_WIDTH       128
#define EPD_LOG_HEIGHT      80

/*
 * E-paper controller RAM 實際方向：
 * Source = 80
 * Gate   = 128
 */
#define EPD_RAM_WIDTH       80
#define EPD_RAM_HEIGHT      128

#define EPD_BUF_SIZE        (EPD_RAM_WIDTH * EPD_RAM_HEIGHT / 8)

#define EPD_BUSY_GPIO_Port  GPIOA
#define EPD_BUSY_Pin        GPIO_PIN_0

#define EPD_RST_GPIO_Port   GPIOA
#define EPD_RST_Pin         GPIO_PIN_1

#define EPD_DC_GPIO_Port    GPIOA
#define EPD_DC_Pin          GPIO_PIN_4

#define EPD_CS_GPIO_Port    GPIOA
#define EPD_CS_Pin          GPIO_PIN_6

void EPD_Init(void);
void EPD_ClearWhite(void);
void EPD_ShowString(const char *text);
void EPD_ShowTestPattern(void);
void EPD_Sleep(void);

#endif
