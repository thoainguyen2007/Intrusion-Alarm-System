#ifndef __SENSORS_H
#define __SENSORS_H

#include "stm32f1xx_hal.h"
#include <stdbool.h>

/* BẬT/TẮT chế độ Calibration (1: Bật để lấy số liệu xung, 0: Tắt để chạy thật)
 */
#define CALIBRATION_MODE 0

#define VIB_NOISE_MAX 5 /* 0-5: nhiễu nền */
#define VIB_LIGHT_MIN 6 /* 6-19: rung nhẹ */
#define VIB_HEAVY_MIN 20 /* >=20 accepted pulses per window: HEAVY */
#define VIB_WINDOW_MS 1000 /* Nominal accumulation interval; not a sliding window */
#define VIB_GLITCH_FILTER_MS 8 /* Minimum spacing between accepted EXTI edges */

typedef enum { VIB_NONE = 0, VIB_LIGHT, VIB_HEAVY } VibLevel_t;

/* APIs */
void Sensors_Vib_EXTI_Callback(void);
void Sensors_Process_Window(bool isDoorClosed);
VibLevel_t Vibration_GetLevel(void);
uint32_t Vibration_GetPulseCount(void);
void Vibration_Reset(void);

#endif /* __SENSORS_H */
