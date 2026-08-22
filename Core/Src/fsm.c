#include "fsm.h"
#include "main.h"
#include "tim.h"
#include "ssd1306.h"
#include "fonts.h"
#include "sd_logger.h"
#include "time_utils.h"
#include <stdio.h>
#include <string.h>

/* ==================================================================== */
/*                    ĐIỀU KHIỂN CÒI BUZZER TIMER PWM (PA8)             */
/* ==================================================================== */
void Buzzer_Init(void)
{
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
}

void Buzzer_SetState(bool on)
{
    if (on)
    {
        HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    }
    else
    {
        HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
    }
}

/* ==================================================================== */
/*                       BIẾN NỘI BỘ QUẢN LÝ FSM                        */
/* ==================================================================== */
static SystemState_t currentState = STATE_DISARM;
static uint32_t state_start_tick = 0;

/* Bộ đệm nhập mã PIN từ Keypad */
static char pin_buffer[PIN_MAX_LEN + 1];
static uint8_t pin_idx = 0;

/* Biến thông báo lỗi tạm thời lên màn hình OLED */
static char error_banner[32] = "";
static uint32_t error_banner_timeout = 0;

/* Biến điều khiển nhịp còi Buzzer và nháy LED */
static uint32_t last_buz_tick = 0;
static uint32_t last_led_tick = 0;

/* ==================================================================== */
/*                  HÀM GHI NHẬT KÝ THẺ NHỚ SD (FATFS)                  */
/* ==================================================================== */
static void SD_Log_Event(const char *event_msg)
{
    printf("[LOG] %s\r\n", event_msg);
    if (!SD_Logger_Enqueue(event_msg))
        printf("[FATFS] Log queue full/invalid, dropped=%lu\r\n",
               SD_Logger_GetDroppedCount());
}

/* ==================================================================== */
/*                      HÀM ĐẶT THÔNG BÁO BANNER LỖI                    */
/* ==================================================================== */
static void FSM_SetError(const char *msg, uint32_t duration_ms)
{
    snprintf(error_banner, sizeof(error_banner), "%s", msg);
    error_banner_timeout = HAL_GetTick() + duration_ms;
    printf("[ERROR] %s\r\n", msg);
}

/* ==================================================================== */
/*                         KHỞI TẠO HỆ THỐNG FSM                        */
/* ==================================================================== */
void FSM_Init(void)
{
    currentState = STATE_DISARM;
    state_start_tick = HAL_GetTick();
    pin_idx = 0;
    pin_buffer[0] = '\0';
    error_banner[0] = '\0';
    error_banner_timeout = 0;

    Buzzer_Init();
    Buzzer_SetState(false);
    HAL_GPIO_WritePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin, GPIO_PIN_SET); /* LED PC13 Tắt */
    
    printf("\r\n[FSM] System Initialized in state: %s\r\n", FSM_GetStateName(currentState));
    SD_Log_Event("FSM Initialized: STATE_DISARM");
}

/* ==================================================================== */
/*                     LẤY TÊN TRẠNG THÁI HIỆN TẠI                      */
/* ==================================================================== */
SystemState_t FSM_GetState(void)
{
    return currentState;
}

const char* FSM_GetStateName(SystemState_t state)
{
    switch (state)
    {
        case STATE_DISARM:       return "DISARM";
        case STATE_EXIT_DELAY:  return "EXIT DELAY";
        case STATE_ARMED:        return "ARMED";
        case STATE_ENTRY_DELAY: return "ENTRY DELAY";
        case STATE_TEMP_DISARM: return "TEMP DISARM";
        case STATE_ALARM_EMERGE:return "ALARM EMERGE";
        case STATE_TEMP_ALARM:  return "TEMP ALARM";
        default:                return "UNKNOWN";
    }
}

const char* FSM_GetPinBuffer(void)
{
    return pin_buffer;
}

/* ==================================================================== */
/*            ĐIỀU KHIỂN CÒI BUZZER & LED THEO TỪNG TRẠNG THÁI          */
/* ==================================================================== */
static void FSM_Update_Outputs(uint32_t now)
{
    switch (currentState)
    {
        case STATE_DISARM:
            /* Còi tắt. LED nháy nhịp tim chậm 0.5Hz (1000ms chu kỳ) */
            Buzzer_SetState(false);
            if (Time_HasElapsed(now, last_led_tick, 1000U))
            {
                last_led_tick = now;
                HAL_GPIO_TogglePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin);
            }
            break;

        case STATE_EXIT_DELAY:
            /* Còi bíp chậm 1Hz (100ms ON / 900ms OFF). LED nhấp nháy 1Hz */
            if (Time_HasElapsed(now, last_buz_tick, 1000U))
            {
                last_buz_tick = now;
                Buzzer_SetState(true);
            }
            else if (Time_HasElapsed(now, last_buz_tick, 100U))
            {
                Buzzer_SetState(false);
            }
            if (Time_HasElapsed(now, last_led_tick, 500U))
            {
                last_led_tick = now;
                HAL_GPIO_TogglePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin);
            }
            break;

        case STATE_ARMED:
            /* Còi tắt. LED nháy chớp ngắn tuần tra (50ms ON mỗi 1500ms) */
            Buzzer_SetState(false);
            if (Time_HasElapsed(now, last_led_tick, 1500U))
            {
                last_led_tick = now;
                HAL_GPIO_WritePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin, GPIO_PIN_RESET); /* BẬT LED */
            }
            else if (Time_HasElapsed(now, last_led_tick, 50U))
            {
                HAL_GPIO_WritePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin, GPIO_PIN_SET);   /* TẮT LED */
            }
            break;

        case STATE_ENTRY_DELAY:
            /* Còi bíp dồn dập 4Hz (100ms ON / 150ms OFF). LED chớp nhanh */
            if (Time_HasElapsed(now, last_buz_tick, 250U))
            {
                last_buz_tick = now;
                Buzzer_SetState(true);
                HAL_GPIO_WritePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin, GPIO_PIN_RESET);
            }
            else if (Time_HasElapsed(now, last_buz_tick, 100U))
            {
                Buzzer_SetState(false);
                HAL_GPIO_WritePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin, GPIO_PIN_SET);
            }
            break;

        case STATE_TEMP_DISARM:
            /* Còi tắt. LED chớp đúp 2 nhịp */
            Buzzer_SetState(false);
            if (Time_HasElapsed(now, last_led_tick, 1000U))
            {
                last_led_tick = now;
                HAL_GPIO_TogglePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin);
            }
            break;

        case STATE_ALARM_EMERGE:
            /* CÒI HÚ CỰC ĐẠI: Bật liên tục bằng PWM. LED chớp 10Hz */
            Buzzer_SetState(true);
            if (Time_HasElapsed(now, last_led_tick, 50U))
            {
                last_led_tick = now;
                HAL_GPIO_TogglePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin);
            }
            break;

        case STATE_TEMP_ALARM:
            /* Còi bíp ngắt quãng 1s Kêu / 1s Nghỉ để kiểm tra hiện trường */
            if (Time_HasElapsed(now, last_buz_tick, 2000U))
            {
                last_buz_tick = now;
                Buzzer_SetState(true);
            }
            else if (Time_HasElapsed(now, last_buz_tick, 1000U))
            {
                Buzzer_SetState(false);
            }
            if (Time_HasElapsed(now, last_led_tick, 500U))
            {
                last_led_tick = now;
                HAL_GPIO_TogglePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin);
            }
            break;
    }
}

/* ==================================================================== */
/*                   HIỂN THỊ GIAO DIỆN OLED THEO FSM                   */
/* ==================================================================== */
static void GetMaskedPin(char *dest, size_t dest_size)
{
    uint8_t i;
    for (i = 0; i < pin_idx && i < 4 && i < dest_size - 1; i++) {
        dest[i] = '*';
    }
    for (; i < 4 && i < dest_size - 1; i++) {
        dest[i] = '_';
    }
    dest[i] = '\0';
}

static void FSM_Render_OLED(uint32_t now, bool door_open, bool pir_motion, VibLevel_t vib_level)
{
    static uint32_t last_render_tick = 0;
    if (!Time_HasElapsed(now, last_render_tick, 100U)) return; /* 10 FPS làm mới */
    last_render_tick = now;

    SSD1306_Fill(SSD1306_COLOR_BLACK);
    char buf[32];
    char pin_display[8];
    GetMaskedPin(pin_display, sizeof(pin_display));

    /* 1. Thanh tiêu đề trạng thái */
    SSD1306_GotoXY(4, 2);
    snprintf(buf, sizeof(buf), "[%s]", FSM_GetStateName(currentState));
    SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);
    SSD1306_DrawLine(0, 13, 127, 13, SSD1306_COLOR_WHITE);

    /* 2. Kiểm tra nếu đang có banner lỗi thì ưu tiên hiển thị */
    if (error_banner[0] != '\0' && !Time_DeadlineReached(now, error_banner_timeout))
    {
        SSD1306_GotoXY(2, 22);
        SSD1306_Puts(error_banner, &Font_7x10, SSD1306_COLOR_WHITE);
        SSD1306_GotoXY(2, 44);
        SSD1306_Puts("Press any key...", &Font_7x10, SSD1306_COLOR_WHITE);
        SSD1306_UpdateScreen();
        return;
    }

    /* 3. Hiển thị nội dung chuyên biệt cho từng trạng thái (< 18 ký tự/dòng) */
    switch (currentState)
    {
        case STATE_DISARM:
            SSD1306_GotoXY(2, 16);
            SSD1306_Puts("PIN + [#] to ARM", &Font_7x10, SSD1306_COLOR_WHITE);
            SSD1306_GotoXY(2, 30);
            snprintf(buf, sizeof(buf), "PIN: [%s]", pin_display);
            SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);
            SSD1306_GotoXY(2, 46);
            snprintf(buf, sizeof(buf), "Door:%-4s Vib:%s", door_open ? "OPEN" : "OK",
                     (vib_level == VIB_HEAVY) ? "HVY" : (vib_level == VIB_LIGHT ? "LGT" : "OK"));
            SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);
            break;

        case STATE_EXIT_DELAY:
        {
            uint32_t elapsed = now - state_start_tick;
            uint32_t remain = (elapsed < EXIT_DELAY_MS) ? ((EXIT_DELAY_MS - elapsed) / 1000) : 0;
            SSD1306_GotoXY(2, 16);
            snprintf(buf, sizeof(buf), "LEAVE HOME: %2lus", remain);
            SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);
            SSD1306_GotoXY(2, 30);
            snprintf(buf, sizeof(buf), "PIN:[%s] Door:%-4s", pin_display, door_open ? "OPEN" : "OK");
            SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);
            SSD1306_GotoXY(2, 46);
            SSD1306_Puts("PIN+[#] to Cancel", &Font_7x10, SSD1306_COLOR_WHITE);
            break;
        }

        case STATE_ARMED:
            SSD1306_GotoXY(4, 16);
            SSD1306_Puts("SYSTEM ARMED 24/7", &Font_7x10, SSD1306_COLOR_WHITE);
            SSD1306_GotoXY(2, 30);
            snprintf(buf, sizeof(buf), "PIN: [%s]", pin_display);
            SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);
            SSD1306_GotoXY(2, 46);
            snprintf(buf, sizeof(buf), "Door:%-4s PIR:%-3s", door_open ? "OPEN" : "OK", pir_motion ? "MOV" : "OK");
            SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);
            break;

        case STATE_ENTRY_DELAY:
        {
            uint32_t elapsed = now - state_start_tick;
            uint32_t remain = (elapsed < ENTRY_DELAY_MS) ? ((ENTRY_DELAY_MS - elapsed) / 1000) : 0;
            SSD1306_GotoXY(2, 16);
            snprintf(buf, sizeof(buf), "ENTER PIN: %2lus", remain);
            SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);
            SSD1306_GotoXY(2, 30);
            snprintf(buf, sizeof(buf), "PIN: [%s]", pin_display);
            SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);
            SSD1306_GotoXY(2, 46);
            SSD1306_Puts("Press [#] to OK", &Font_7x10, SSD1306_COLOR_WHITE);
            break;
        }

        case STATE_TEMP_DISARM:
        {
            uint32_t elapsed = now - state_start_tick;
            uint32_t remain = (elapsed < TEMP_DISARM_MS) ? ((TEMP_DISARM_MS - elapsed) / 1000) : 0;
            SSD1306_GotoXY(2, 16);
            SSD1306_Puts("WELCOME HOME (60s)", &Font_7x10, SSD1306_COLOR_WHITE);
            SSD1306_GotoXY(2, 30);
            snprintf(buf, sizeof(buf), "Auto-Arm in: %2lus", remain);
            SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);
            SSD1306_GotoXY(2, 46);
            snprintf(buf, sizeof(buf), "Door:%-4s(Close it)", door_open ? "OPEN" : "OK");
            SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);
            break;
        }

        case STATE_ALARM_EMERGE:
            SSD1306_GotoXY(2, 16);
            SSD1306_Puts("!! SIREN ALARM !!", &Font_7x10, SSD1306_COLOR_WHITE);
            SSD1306_GotoXY(2, 30);
            snprintf(buf, sizeof(buf), "PIN: [%s]", pin_display);
            SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);
            SSD1306_GotoXY(2, 46);
            SSD1306_Puts("PIN+[#] TO STOP!", &Font_7x10, SSD1306_COLOR_WHITE);
            break;

        case STATE_TEMP_ALARM:
        {
            uint32_t elapsed = now - state_start_tick;
            uint32_t remain = (elapsed < TEMP_ALARM_MS) ? ((TEMP_ALARM_MS - elapsed) / 1000) : 0;
            SSD1306_GotoXY(2, 16);
            SSD1306_Puts("INSPECT AREA (30s)", &Font_7x10, SSD1306_COLOR_WHITE);
            SSD1306_GotoXY(2, 30);
            snprintf(buf, sizeof(buf), "Time Left: %2lus", remain);
            SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);
            SSD1306_GotoXY(2, 46);
            snprintf(buf, sizeof(buf), "Door:%-4s(Close it)", door_open ? "OPEN" : "OK");
            SSD1306_Puts(buf, &Font_7x10, SSD1306_COLOR_WHITE);
            break;
        }
    }

    SSD1306_UpdateScreen();
}

/* ==================================================================== */
/*                   XỬ LÝ CHUYỂN DỊCH TRẠNG THÁI CHÍNH                 */
/* ==================================================================== */
void FSM_Process(char key_pressed, bool door_open, bool pir_motion, VibLevel_t vib_level)
{
    uint32_t now = HAL_GetTick();

    /* ------------------------------------------------------------- */
    /* 1. Xử lý nhập phím từ Keypad                                  */
    /* ------------------------------------------------------------- */
    bool pin_submitted = false;
    if (key_pressed != 0 && key_pressed != '-')
    {
        if (key_pressed >= '0' && key_pressed <= '9')
        {
            if (pin_idx < PIN_MAX_LEN)
            {
                pin_buffer[pin_idx++] = key_pressed;
                pin_buffer[pin_idx] = '\0';
            }
        }
        else if (key_pressed == '*')
        {
            /* Phím * : Xóa mã PIN */
            pin_idx = 0;
            pin_buffer[0] = '\0';
        }
        else if (key_pressed == '#')
        {
            /* Phím # : Xác nhận mã PIN */
            pin_submitted = true;
        }
    }

    bool is_pin_correct = (pin_submitted && strcmp(pin_buffer, DEFAULT_PIN) == 0);

    /* ------------------------------------------------------------- */
    /* 2. Máy trạng thái 7-State FSM Logic                           */
    /* ------------------------------------------------------------- */
    switch (currentState)
    {
        /* --- 1. STATE_DISARM --- */
        case STATE_DISARM:
            if (pin_submitted)
            {
                if (is_pin_correct)
                {
                    if (door_open)
                    {
                        FSM_SetError("ARM FAILED: Door Open!", 2000);
                        SD_Log_Event("DISARM -> ARM Failed: Door Open");
                    }
                    else
                    {
                        currentState = STATE_EXIT_DELAY;
                        state_start_tick = now;
                        Vibration_Reset();
                        printf("\r\n[FSM] State -> EXIT_DELAY (15s)\r\n");
                        SD_Log_Event("State -> EXIT_DELAY (15s)");
                    }
                }
                else
                {
                    FSM_SetError("WRONG PIN!", 1500);
                }
                pin_idx = 0; pin_buffer[0] = '\0';
            }
            break;

        /* --- 2. STATE_EXIT_DELAY (15s) --- */
        case STATE_EXIT_DELAY:
            if (pin_submitted)
            {
                if (is_pin_correct)
                {
                    currentState = STATE_DISARM;
                    state_start_tick = now;
                    Vibration_Reset();
                    printf("\r\n[FSM] EXIT_DELAY Cancelled by PIN -> DISARM\r\n");
                    SD_Log_Event("EXIT_DELAY Cancelled -> DISARM");
                }
                else
                {
                    FSM_SetError("WRONG PIN!", 1500);
                }
                pin_idx = 0; pin_buffer[0] = '\0';
            }
            else if (Time_HasElapsed(now, state_start_tick, EXIT_DELAY_MS))
            {
                if (!door_open)
                {
                    currentState = STATE_ARMED;
                    state_start_tick = now;
                    Vibration_Reset();
                    printf("\r\n[FSM] 15s Elapsed & Door Closed -> ARMED\r\n");
                    SD_Log_Event("EXIT_DELAY Complete -> ARMED");
                }
                else
                {
                    currentState = STATE_DISARM;
                    state_start_tick = now;
                    Vibration_Reset();
                    FSM_SetError("ARM FAILED: Door Open!", 2500);
                    printf("\r\n[FSM] 15s Elapsed & Door OPEN -> ARM FAILED -> DISARM\r\n");
                    SD_Log_Event("EXIT_DELAY Failed (Door Open) -> DISARM");
                }
                pin_idx = 0; pin_buffer[0] = '\0';
            }
            break;

        /* --- 3. STATE_ARMED (Bảo vệ 24/7) --- */
        case STATE_ARMED:
            if (pin_submitted)
            {
                if (is_pin_correct)
                {
                    currentState = STATE_DISARM;
                    state_start_tick = now;
                    Vibration_Reset();
                    printf("\r\n[FSM] Disarmed by PIN -> DISARM\r\n");
                    SD_Log_Event("ARMED -> DISARM (By PIN)");
                }
                else
                {
                    FSM_SetError("WRONG PIN!", 1500);
                }
                pin_idx = 0; pin_buffer[0] = '\0';
            }
            else
            {
                /* Nhánh Khẩn cấp: Cạy cửa hoặc Rung mạnh -> ALARM EMERGE */
                if (door_open || vib_level == VIB_HEAVY)
                {
                    currentState = STATE_ALARM_EMERGE;
                    state_start_tick = now;
                    Vibration_Reset();
                    printf("\r\n[FSM] CRITICAL INTRUSION (Door/Heavy Vib) -> ALARM_EMERGE\r\n");
                    SD_Log_Event("ARMED -> ALARM_EMERGE (Door Open / Heavy Vib)");
                    pin_idx = 0; pin_buffer[0] = '\0';
                }
                /* Nhánh Cảnh báo nhẹ: PIR hoặc Rung nhẹ -> ENTRY_DELAY (30s) */
                else if (pir_motion || vib_level == VIB_LIGHT)
                {
                    currentState = STATE_ENTRY_DELAY;
                    state_start_tick = now;
                    Vibration_Reset();
                    printf("\r\n[FSM] Motion/Light Vib Detected -> ENTRY_DELAY (30s)\r\n");
                    SD_Log_Event("ARMED -> ENTRY_DELAY (PIR / Light Vib)");
                    pin_idx = 0; pin_buffer[0] = '\0';
                }
            }
            break;

        /* --- 4. STATE_ENTRY_DELAY (30s) --- */
        case STATE_ENTRY_DELAY:
            if (pin_submitted)
            {
                if (is_pin_correct)
                {
                    currentState = STATE_TEMP_DISARM;
                    state_start_tick = now;
                    Vibration_Reset();
                    printf("\r\n[FSM] PIN Correct -> TEMP_DISARM (60s)\r\n");
                    SD_Log_Event("ENTRY_DELAY -> TEMP_DISARM (60s)");
                }
                else
                {
                    FSM_SetError("WRONG PIN!", 1500);
                }
                pin_idx = 0; pin_buffer[0] = '\0';
            }
            else
            {
                /* Xâm nhập bạo lực hoặc hết 30s -> ALARM EMERGE */
                if (vib_level == VIB_HEAVY || door_open)
                {
                    currentState = STATE_ALARM_EMERGE;
                    state_start_tick = now;
                    Vibration_Reset();
                    printf("\r\n[FSM] Forced entry during Entry Delay -> ALARM_EMERGE\r\n");
                    SD_Log_Event("ENTRY_DELAY -> ALARM_EMERGE (Forced Entry)");
                    pin_idx = 0; pin_buffer[0] = '\0';
                }
                else if (Time_HasElapsed(now, state_start_tick, ENTRY_DELAY_MS))
                {
                    currentState = STATE_ALARM_EMERGE;
                    state_start_tick = now;
                    Vibration_Reset();
                    printf("\r\n[FSM] 30s Entry Delay Timeout -> ALARM_EMERGE\r\n");
                    SD_Log_Event("ENTRY_DELAY -> ALARM_EMERGE (Timeout 30s)");
                    pin_idx = 0; pin_buffer[0] = '\0';
                }
            }
            break;

        /* --- 5. STATE_TEMP_DISARM (60s) --- */
        case STATE_TEMP_DISARM:
            if (pin_submitted)
            {
                if (is_pin_correct)
                {
                    currentState = STATE_DISARM;
                    state_start_tick = now;
                    Vibration_Reset();
                    printf("\r\n[FSM] User Manual Disarm -> DISARM\r\n");
                    SD_Log_Event("TEMP_DISARM -> DISARM (Manual)");
                }
                else
                {
                    FSM_SetError("WRONG PIN!", 1500);
                }
                pin_idx = 0; pin_buffer[0] = '\0';
            }
            else if (Time_HasElapsed(now, state_start_tick, TEMP_DISARM_MS))
            {
                if (!door_open)
                {
                    /* Tự động kích hoạt lại (Auto-Rearm) */
                    currentState = STATE_ARMED;
                    state_start_tick = now;
                    Vibration_Reset();
                    printf("\r\n[FSM] 60s Elapsed & Door Closed -> AUTO-REARMED\r\n");
                    SD_Log_Event("TEMP_DISARM -> ARMED (Auto-Rearm)");
                }
                else
                {
                    /* Hết 60s mà quên đóng cửa -> Báo động */
                    currentState = STATE_ALARM_EMERGE;
                    state_start_tick = now;
                    Vibration_Reset();
                    printf("\r\n[FSM] 60s Elapsed but Door OPEN -> ALARM_EMERGE\r\n");
                    SD_Log_Event("TEMP_DISARM -> ALARM_EMERGE (Door Left Open)");
                }
                pin_idx = 0; pin_buffer[0] = '\0';
            }
            break;

        /* --- 6. STATE_ALARM_EMERGE --- */
        case STATE_ALARM_EMERGE:
            if (pin_submitted)
            {
                if (is_pin_correct)
                {
                    currentState = STATE_TEMP_ALARM;
                    state_start_tick = now;
                    Vibration_Reset();
                    printf("\r\n[FSM] Alarm Verified with PIN -> TEMP_ALARM (30s Inspection)\r\n");
                    SD_Log_Event("ALARM_EMERGE -> TEMP_ALARM (30s Inspection)");
                }
                else
                {
                    FSM_SetError("WRONG PIN!", 1500);
                }
                pin_idx = 0; pin_buffer[0] = '\0';
            }
            break;

        /* --- 7. STATE_TEMP_ALARM (30s) --- */
        case STATE_TEMP_ALARM:
            if (pin_submitted)
            {
                if (is_pin_correct)
                {
                    currentState = STATE_DISARM;
                    state_start_tick = now;
                    Vibration_Reset();
                    printf("\r\n[FSM] Inspection finished -> DISARM\r\n");
                    SD_Log_Event("TEMP_ALARM -> DISARM (By PIN)");
                }
                else
                {
                    FSM_SetError("WRONG PIN!", 1500);
                }
                pin_idx = 0; pin_buffer[0] = '\0';
            }
            else if (Time_HasElapsed(now, state_start_tick, TEMP_ALARM_MS))
            {
                if (!door_open)
                {
                    currentState = STATE_ARMED;
                    state_start_tick = now;
                    Vibration_Reset();
                    printf("\r\n[FSM] 30s Inspection finished & Door Closed -> ARMED\r\n");
                    SD_Log_Event("TEMP_ALARM -> ARMED (Scene Safe)");
                }
                else
                {
                    currentState = STATE_ALARM_EMERGE;
                    state_start_tick = now;
                    Vibration_Reset();
                    printf("\r\n[FSM] 30s Inspection finished but Door OPEN -> Resume ALARM_EMERGE\r\n");
                    SD_Log_Event("TEMP_ALARM -> ALARM_EMERGE (Door Still Open)");
                }
                pin_idx = 0; pin_buffer[0] = '\0';
            }
            break;
    }

    /* ------------------------------------------------------------- */
    /* 3. Cập nhật đầu ra Còi Buzzer, LED và Màn hình OLED           */
    /* ------------------------------------------------------------- */
    FSM_Update_Outputs(now);
    FSM_Render_OLED(now, door_open, pir_motion, vib_level);
}
