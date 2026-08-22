#include "sensors.h"
#include <stdio.h>

/* Updated in EXTI2 and consumed in the main loop. */
static volatile uint32_t vibPulseCount = 0;
static uint32_t last_vib_pulse_tick = 0;
static VibLevel_t lastVibLevel = VIB_NONE;

/**
 * @brief Tăng biến đếm xung khi có ngắt từ SW-420 (đã lọc nhiễu dội tối thiểu 5ms)
 */
void Sensors_Vib_EXTI_Callback(void)
{
    uint32_t now = HAL_GetTick();
    if (now - last_vib_pulse_tick >= VIB_GLITCH_FILTER_MS)
    {
        vibPulseCount++;
        last_vib_pulse_tick = now;
    }
}

/**
 * @brief Reset lại trạng thái đếm và mức rung khi được gọi (ví dụ lúc cửa vừa đóng)
 */
void Vibration_Reset(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    vibPulseCount = 0;
    __set_PRIMASK(primask);
    lastVibLevel = VIB_NONE;
}

/**
 * @brief Trả về cấp độ rung của cửa sổ vừa được xử lý
 */
VibLevel_t Vibration_GetLevel(void)
{
    return lastVibLevel;
}

/**
 * @brief Trả về số xung đếm được (dùng cho debug/log thêm nếu cần)
 */
uint32_t Vibration_GetPulseCount(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint32_t count = vibPulseCount;
    __set_PRIMASK(primask);
    return count;
}

/**
 * @brief Phân loại rung sau mỗi cửa sổ thời gian (1000ms). 
 *        Chỉ đánh giá khi Cửa đang ĐÓNG (isDoorClosed == true).
 */
void Sensors_Process_Window(bool isDoorClosed)
{
    /* Snapshot and clear atomically so an EXTI pulse cannot be lost. */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint32_t currentPulses = vibPulseCount;
    vibPulseCount = 0;
    __set_PRIMASK(primask);
    
    #if CALIBRATION_MODE == 1
    /* Chế độ Calibration: Chỉ in ra số xung thu được khi cửa đóng để khảo sát ngưỡng thực tế */
    if (isDoorClosed) {
        printf("[%lums] VIB window=%lu (CALIBRATION)\r\n", HAL_GetTick(), currentPulses);
    }
    #else
    /* Chế độ chạy thật: Phân loại theo ngưỡng quy định khi cửa đang đóng */
    if (isDoorClosed)
    {
        if (currentPulses >= VIB_HEAVY_MIN) {
            lastVibLevel = VIB_HEAVY;
            printf("[%lums] VIB window=%lu level=HEAVY\r\n", HAL_GetTick(), currentPulses);
        }
        else if (currentPulses >= VIB_LIGHT_MIN) {
            lastVibLevel = VIB_LIGHT;
            printf("[%lums] VIB window=%lu level=LIGHT\r\n", HAL_GetTick(), currentPulses);
        }
        else {
            lastVibLevel = VIB_NONE;
            /* Nếu số xung nhỏ hơn 5 (<= VIB_NOISE_MAX), ta bỏ qua noise */
        }
    }
    else
    {
        /* Nếu cửa đang mở, không cần đánh giá rung cạy cửa */
        lastVibLevel = VIB_NONE;
    }
    #endif

}
