#ifndef __SENSORS_H
#define __SENSORS_H

#include "stm32f1xx_hal.h"
#include <stdbool.h>

/* BẬT/TẮT chế độ Calibration (1: Bật để lấy số liệu xung, 0: Tắt để chạy thật)
 */
#define CALIBRATION_MODE 0

/* Các ngưỡng phân loại rung (Đã tinh chỉnh tăng độ nhạy) */
#define VIB_NOISE_MAX 2  /* <= 2: Nhiễu nền, bỏ qua */
#define VIB_LIGHT_MIN 3  /* 3 - 14: Rung nhẹ / va chạm nhẹ */
#define VIB_HEAVY_MIN 20 /* >= 15: Rung mạnh / giật / Cạy phá */

/* Các tham số thời gian */
#define VIB_WINDOW_MS 1000 /* Cửa sổ đếm xung 1 giây */
#define VIB_GLITCH_FILTER_MS                                                   \
  2 /* Lọc chống nhiễu 2ms (bắt nhạy hơn xung va đập) */

typedef enum { VIB_NONE = 0, VIB_LIGHT, VIB_HEAVY } VibLevel_t;

/* APIs */
void Sensors_Vib_EXTI_Callback(void);
void Sensors_Process_Window(bool isDoorClosed);
VibLevel_t Vibration_GetLevel(void);
uint32_t Vibration_GetPulseCount(void);
void Vibration_Reset(void);

#endif /* __SENSORS_H */
