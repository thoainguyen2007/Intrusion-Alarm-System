#ifndef __SENSORS_H
#define __SENSORS_H

#include "stm32f1xx_hal.h"
#include <stdbool.h>

/* BẬT/TẮT chế độ Calibration (1: Bật để lấy số liệu xung, 0: Tắt để chạy thật)
 */
#define CALIBRATION_MODE 0

#define VIB_NOISE_MAX 4 /* <= 4: nhiễu nền, có vùng đệm an toàn */
#define VIB_LIGHT_MIN 6 /* 6-19: rung nhẹ, cách xa mức nhiễu nền tối thiểu 2 xung */
#define VIB_HEAVY_MIN 20 /* giữ nguyên, đã hợp lý */
#define VIB_WINDOW_MS 1000
#define VIB_GLITCH_FILTER_MS 8 /* đủ lọc dội lò xo cơ khí thật (5-15ms) */

typedef enum { VIB_NONE = 0, VIB_LIGHT, VIB_HEAVY } VibLevel_t;

/* APIs */
void Sensors_Vib_EXTI_Callback(void);
void Sensors_Process_Window(bool isDoorClosed);
VibLevel_t Vibration_GetLevel(void);
uint32_t Vibration_GetPulseCount(void);
void Vibration_Reset(void);

#endif /* __SENSORS_H */
