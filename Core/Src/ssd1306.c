/**
  ******************************************************************************
  * @file    ssd1306.c
  * @brief   Driver for SSD1306 / SH1106 OLED display (I2C) with 2-pixel offset
  *          and DC-DC charge pump activation.
  ******************************************************************************
  */

#include "ssd1306.h"

/* Pointer to I2C peripheral handle */
static I2C_HandleTypeDef *SSD1306_I2C = NULL;
static uint16_t DevAddress = SSD1306_I2C_ADDR;

/* Framebuffer RAM (128x64 / 8 = 1024 bytes) */
static uint8_t SSD1306_Buffer[SSD1306_WIDTH * SSD1306_HEIGHT / 8];
static SSD1306_t SSD1306;

/**
  * @brief  Send command to OLED controller
  */
static void SSD1306_WriteCommand(uint8_t command)
{
    HAL_I2C_Mem_Write(SSD1306_I2C, DevAddress, 0x00, 1, &command, 1, 10);
}

/**
  * @brief  Send data stream to OLED controller
  */
static void SSD1306_WriteData(uint8_t *buffer, size_t size)
{
    HAL_I2C_Mem_Write(SSD1306_I2C, DevAddress, 0x40, 1, buffer, size, 100);
}

/**
  * @brief  Initialize SSD1306 / SH1106 OLED display
  */
uint8_t SSD1306_Init(I2C_HandleTypeDef *hi2c)
{
    SSD1306_I2C = hi2c;

    /* This board uses a fixed SH1106 address: 0x3C (HAL format 0x78). */
    DevAddress = SSD1306_I2C_ADDR;
    if (HAL_I2C_IsDeviceReady(SSD1306_I2C, DevAddress, 2, 5) != HAL_OK) {
        SSD1306.Initialized = 0;
        return 0; /* Device not connected, abort to prevent I2C delays */
    }

    SSD1306.Initialized = 1;
    HAL_Delay(50);

    /* SH1106 Initialization Sequence */
    SSD1306_WriteCommand(0xAE); // Display Off
    SSD1306_WriteCommand(0x02); // Set lower column address (Offset 2 for SH1106)
    SSD1306_WriteCommand(0x10); // Set higher column address
    SSD1306_WriteCommand(0x40); // Set start line address
    SSD1306_WriteCommand(0xB0); // Set page address

    SSD1306_WriteCommand(0x81); // Contrast control
    SSD1306_WriteCommand(0xCF);

    SSD1306_WriteCommand(0xA1); // Segment remap (A0/A1)
    SSD1306_WriteCommand(0xA6); // Normal display

    SSD1306_WriteCommand(0xA8); // Multiplex ratio
    SSD1306_WriteCommand(0x3F); // 1/64 Duty

    /* SH1106 DC-DC Booster ON (0xAD, 0x8B) */
    SSD1306_WriteCommand(0xAD);
    SSD1306_WriteCommand(0x8B);

    SSD1306_WriteCommand(0xC8); // COM Output scan direction

    SSD1306_WriteCommand(0xD3); // Display offset
    SSD1306_WriteCommand(0x00);

    SSD1306_WriteCommand(0xD5); // Display clock divide ratio / osc freq
    SSD1306_WriteCommand(0x80);

    SSD1306_WriteCommand(0xD9); // Pre-charge period
    SSD1306_WriteCommand(0x22);

    SSD1306_WriteCommand(0xDB); // VCOM deselect level
    SSD1306_WriteCommand(0x40);

    SSD1306_WriteCommand(0xAF); // Display ON

    /* Clear screen & reset cursor */
    SSD1306_Fill(SSD1306_COLOR_BLACK);
    SSD1306_UpdateScreen();

    SSD1306.CurrentX = 0;
    SSD1306.CurrentY = 0;
    SSD1306.Initialized = 1;

    return 1;
}

/**
  * @brief  Update screen RAM from internal buffer
  */
void SSD1306_UpdateScreen(void)
{
    if (!SSD1306.Initialized) return;
    uint8_t m;
    for (m = 0; m < 8; m++) {
        SSD1306_WriteCommand(0xB0 + m); // Set Page Address
        SSD1306_WriteCommand(0x02);     // Set Lower Column Address (2 pixel offset for SH1106)
        SSD1306_WriteCommand(0x10);     // Set Higher Column Address
        SSD1306_WriteData(&SSD1306_Buffer[SSD1306_WIDTH * m], SSD1306_WIDTH);
    }
}

/**
  * @brief  Fill buffer with single color
  */
void SSD1306_Fill(SSD1306_COLOR_t color)
{
    memset(SSD1306_Buffer, (color == SSD1306_COLOR_BLACK) ? 0x00 : 0xFF, sizeof(SSD1306_Buffer));
}

/**
  * @brief  Clear buffer (fill black)
  */
void SSD1306_Clear(void)
{
    SSD1306_Fill(SSD1306_COLOR_BLACK);
}

/**
  * @brief  Draw a single pixel in buffer
  */
void SSD1306_DrawPixel(uint16_t x, uint16_t y, SSD1306_COLOR_t color)
{
    if (x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT) {
        return;
    }

    if (color == SSD1306_COLOR_WHITE) {
        SSD1306_Buffer[x + (y / 8) * SSD1306_WIDTH] |= 1 << (y % 8);
    } else {
        SSD1306_Buffer[x + (y / 8) * SSD1306_WIDTH] &= ~(1 << (y % 8));
    }
}

/**
  * @brief  Set text cursor position
  */
void SSD1306_GotoXY(uint16_t x, uint16_t y)
{
    SSD1306.CurrentX = x;
    SSD1306.CurrentY = y;
}

/**
  * @brief  Write character to buffer
  */
char SSD1306_WriteChar(char ch, FontDef_t* Font, SSD1306_COLOR_t color)
{
    uint32_t i, b, j;

    if (ch < 32 || ch > 126) {
        ch = ' ';
    }

    if (SSD1306_WIDTH < (SSD1306.CurrentX + Font->FontWidth) ||
        SSD1306_HEIGHT < (SSD1306.CurrentY + Font->FontHeight))
    {
        return 0;
    }

    for (i = 0; i < Font->FontHeight; i++) {
        b = Font->data[(ch - 32) * Font->FontHeight + i];
        for (j = 0; j < Font->FontWidth; j++) {
            if ((b << j) & 0x8000) {
                SSD1306_DrawPixel(SSD1306.CurrentX + j, SSD1306.CurrentY + i, color);
            } else {
                SSD1306_DrawPixel(SSD1306.CurrentX + j, SSD1306.CurrentY + i, (SSD1306_COLOR_t)!color);
            }
        }
    }

    SSD1306.CurrentX += Font->FontWidth;
    return ch;
}

/**
  * @brief  Write string to buffer
  */
char SSD1306_Puts(char* str, FontDef_t* Font, SSD1306_COLOR_t color)
{
    while (*str) {
        if (SSD1306_WriteChar(*str, Font, color) != *str) {
            return *str;
        }
        str++;
    }
    return *str;
}

/**
  * @brief  Draw line using Bresenham algorithm
  */
void SSD1306_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, SSD1306_COLOR_t color)
{
    int16_t dx = abs((int)x1 - (int)x0);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t dy = -abs((int)y1 - (int)y0);
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx + dy;
    int16_t e2;

    while (1) {
        SSD1306_DrawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/**
  * @brief  Draw rectangle outline
  */
void SSD1306_DrawRectangle(uint16_t x, uint16_t y, uint16_t w, uint16_t h, SSD1306_COLOR_t color)
{
    SSD1306_DrawLine(x, y, x + w - 1, y, color);
    SSD1306_DrawLine(x, y + h - 1, x + w - 1, y + h - 1, color);
    SSD1306_DrawLine(x, y, x, y + h - 1, color);
    SSD1306_DrawLine(x + w - 1, y, x + w - 1, y + h - 1, color);
}
