/**
  ******************************************************************************
  * @file    fonts.h
  * @brief   Font definitions for OLED SSD1306 / SH1106 driver.
  ******************************************************************************
  */

#ifndef __FONTS_H
#define __FONTS_H

#include <stdint.h>

typedef struct {
    const uint8_t FontWidth;    /*!< Font width in pixels */
    uint8_t FontHeight;         /*!< Font height in pixels */
    const uint16_t *data;       /*!< Pointer to data font array */
} FontDef_t;

extern FontDef_t Font_7x10;
extern FontDef_t Font_11x18;
extern FontDef_t Font_16x26;

#endif /* __FONTS_H */
