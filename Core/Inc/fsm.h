#ifndef __FSM_H
#define __FSM_H

#include "stm32f1xx_hal.h"
#include "sensors.h"
#include <stdbool.h>
#include <stdint.h>

/* ==================================================================== */
/*                        CẤU HÌNH THAM SỐ HỆ THỐNG                     */
/* ==================================================================== */
#define PIN_TEMP_DISARM         "1234"      /* Mã PIN Tạm thời (Vào TEMP_DISARM 60s lấy đồ) */
#define PIN_MASTER_DISARM       "6789"      /* Mã PIN Master (Giải trừ an ninh hoàn toàn về DISARM) */
#define DEFAULT_PIN             PIN_TEMP_DISARM
#define PIN_LENGTH              4U          /* Mã PIN hệ thống có đúng 4 chữ số */
#define PIN_MAX_LEN             PIN_LENGTH
#define PIN_MAX_FAILED_ATTEMPTS 5U          /* Khóa bàn phím sau 5 lần nhập sai */
#define PIN_LOCKOUT_MS          30000U       /* Thời gian khóa bàn phím: 30 giây */

#define EXIT_DELAY_MS           15000       /* Thời gian trễ rời nhà (15 giây) */
#define ENTRY_DELAY_MS          30000U      /* Timeout tối đa để xác minh sự kiện (30 giây) */
#define ENTRY_PIR_READY_REARM_MS 10000U      /* PIR READY liên tục 10s: xác nhận an toàn */
#define ENTRY_VIB_QUIET_REARM_MS  5000U      /* Không còn rung nhẹ liên tục 5s: xác nhận an toàn */
#define TEMP_DISARM_MS          60000U      /* Bốc dỡ hàng & Lấy đồ nhanh (60 giây) -> Tự về ARMED */
#define TEMP_DISARM_WARN_MS     45000U      /* Mốc cảnh báo 15s cuối trước khi hết giờ (45 giây) */
#define TEMP_ALARM_MS           30000       /* Thời gian kiểm tra hiện trường Cooldown (30 giây) */

/* ==================================================================== */
/*                  DANH SÁCH 6 TRẠNG THÁI FSM PHÂN CẤP                 */
/* ==================================================================== */
typedef enum {
    STATE_DISARM = 0,       /* 1. Giải trừ / Chế độ nghỉ */
    STATE_EXIT_DELAY,       /* 2. Đếm ngược rời nhà (15s) */
    STATE_ARMED,            /* 3. Canh gác / Bảo vệ an ninh toàn diện */
    STATE_ENTRY_DELAY,      /* 4. Đếm ngược vào nhà & nhập mã (30s) */
    STATE_TEMP_DISARM,      /* 5. Chờ giải trừ an toàn, cửa phải luôn đóng (60s) */
    STATE_ALARM_EMERGE      /* 6. Báo động phân cấp: Pha 1 (Siren) & Pha 2 (Cooldown 30s) */
} SystemState_t;

/* ==================================================================== */
/*                             HÀM API FSM                              */
/* ==================================================================== */
void FSM_Init(void);
void FSM_Process(char key_pressed, bool door_open, bool pir_ready,
                 bool pir_motion, VibLevel_t vib_level);
SystemState_t FSM_GetState(void);
const char* FSM_GetStateName(SystemState_t state);
const char* FSM_GetPinBuffer(void);

/* API điều khiển còi PWM phần cứng */
void Buzzer_Init(void);
void Buzzer_SetState(bool on);
void Buzzer_RequestKeyBeep(void);

#endif /* __FSM_H */
