/**
  ******************************************************************************
  * @file    ssd1306.h
  * @brief   Header for SSD1306 / SH1106 OLED display driver (I2C).
  ******************************************************************************
  */

#ifndef __SSD1306_H
#define __SSD1306_H

#include "stm32f1xx_hal.h"
#include "fonts.h"
#include <string.h>
#include <stdlib.h>

/* SH1106 1.3": 7-bit address 0x3C, shifted HAL address 0x78. */
#ifndef SSD1306_I2C_ADDR
#define SSD1306_I2C_ADDR        (0x3CU << 1)
#endif

/* OLED dimensions */
#ifndef SSD1306_WIDTH
#define SSD1306_WIDTH           128
#endif

#ifndef SSD1306_HEIGHT
#define SSD1306_HEIGHT          64
#endif

/* Color definitions */
typedef enum {
    SSD1306_COLOR_BLACK = 0x00, /* Black color, no pixel */
    SSD1306_COLOR_WHITE = 0x01  /* Pixel is set. Color depends on LCD */
} SSD1306_COLOR_t;

/* Display state struct */
typedef struct {
    uint16_t CurrentX;
    uint16_t CurrentY;
    uint8_t Inverted;
    uint8_t Initialized;
} SSD1306_t;

/* Function prototypes */
uint8_t SSD1306_Init(I2C_HandleTypeDef *hi2c);
void SSD1306_UpdateScreen(void);
void SSD1306_Fill(SSD1306_COLOR_t color);
void SSD1306_Clear(void);
void SSD1306_DrawPixel(uint16_t x, uint16_t y, SSD1306_COLOR_t color);
void SSD1306_GotoXY(uint16_t x, uint16_t y);
char SSD1306_WriteChar(char ch, FontDef_t* Font, SSD1306_COLOR_t color);
char SSD1306_Puts(char* str, FontDef_t* Font, SSD1306_COLOR_t color);
void SSD1306_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, SSD1306_COLOR_t color);
void SSD1306_DrawRectangle(uint16_t x, uint16_t y, uint16_t w, uint16_t h, SSD1306_COLOR_t color);

#endif /* __SSD1306_H */
