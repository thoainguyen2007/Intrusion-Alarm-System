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
static bool buzzer_on = false;

void Buzzer_Init(void)
{
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
    buzzer_on = false;
}

void Buzzer_SetState(bool on)
{
    if (on == buzzer_on) return;

    if (on)
    {
        if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) == HAL_OK) buzzer_on = true;
    }
    else
    {
        if (HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1) == HAL_OK) buzzer_on = false;
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
static uint8_t failed_pin_attempts = 0;
static bool pin_locked = false;
static uint32_t pin_lockout_deadline = 0;

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

static void FSM_ClearPin(void)
{
    pin_idx = 0;
    pin_buffer[0] = '\0';
}

static void FSM_TransitionTo(SystemState_t next_state, uint32_t now, const char *reason)
{
    if (next_state == currentState) return;

    currentState = next_state;
    state_start_tick = now;
    last_buz_tick = now;
    last_led_tick = now;
    error_banner[0] = '\0';
    FSM_ClearPin();
    Vibration_Reset();

    printf("\r\n[FSM] %s\r\n", reason);
    SD_Log_Event(reason);
}

static void FSM_HandleWrongPin(uint32_t now)
{
    char message[32];

    FSM_ClearPin();
    failed_pin_attempts++;
    snprintf(message, sizeof(message), "Wrong PIN attempt %u/%u",
             failed_pin_attempts, (unsigned int)PIN_MAX_FAILED_ATTEMPTS);
    SD_Log_Event(message);

    if (failed_pin_attempts >= PIN_MAX_FAILED_ATTEMPTS)
    {
        failed_pin_attempts = 0;
        pin_locked = true;
        pin_lockout_deadline = now + PIN_LOCKOUT_MS;
        FSM_SetError("PIN LOCKED 30s", PIN_LOCKOUT_MS);
        SD_Log_Event("PIN keypad locked for 30s");
    }
    else
    {
        snprintf(message, sizeof(message), "WRONG PIN %u/%u",
                 failed_pin_attempts, (unsigned int)PIN_MAX_FAILED_ATTEMPTS);
        FSM_SetError(message, 1500U);
    }
}

static uint32_t FSM_RemainingSeconds(uint32_t now, uint32_t duration_ms)
{
    uint32_t elapsed = now - state_start_tick;
    if (elapsed >= duration_ms) return 0U;
    return (duration_ms - elapsed + 999U) / 1000U;
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
    failed_pin_attempts = 0;
    pin_locked = false;
    pin_lockout_deadline = 0;
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
    for (i = 0; i < pin_idx && i < PIN_LENGTH && i < dest_size - 1; i++) {
        dest[i] = '*';
    }
    for (; i < PIN_LENGTH && i < dest_size - 1; i++) {
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
        SSD1306_Puts(pin_locked ? "Please wait..." : "Press any key...", &Font_7x10, SSD1306_COLOR_WHITE);
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
            uint32_t remain = FSM_RemainingSeconds(now, EXIT_DELAY_MS);
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
            uint32_t remain = FSM_RemainingSeconds(now, ENTRY_DELAY_MS);
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
            uint32_t remain = FSM_RemainingSeconds(now, TEMP_DISARM_MS);
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
            uint32_t remain = FSM_RemainingSeconds(now, TEMP_ALARM_MS);
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
    bool pin_submitted = false;
    bool is_pin_correct = false;

    /* Lockout chỉ chặn bàn phím; cảm biến và timeout vẫn luôn hoạt động. */
    if (pin_locked && Time_DeadlineReached(now, pin_lockout_deadline))
    {
        pin_locked = false;
        failed_pin_attempts = 0;
        error_banner[0] = '\0';
        FSM_ClearPin();
        SD_Log_Event("PIN keypad lockout expired");
    }

    if (!pin_locked && key_pressed != 0 && key_pressed != '-')
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
            FSM_ClearPin();
        }
        else if (key_pressed == '#')
        {
            pin_submitted = true;
            is_pin_correct = (pin_idx == PIN_LENGTH &&
                              strcmp(pin_buffer, DEFAULT_PIN) == 0);
            if (is_pin_correct) failed_pin_attempts = 0;
        }
    }

    /*
     * Thứ tự ưu tiên:
     * 1) PIN đúng để chủ nhà luôn dừng được hệ thống.
     * 2) Rung mạnh, cảm biến và timeout.
     * 3) Ghi nhận PIN sai sau cùng để không che mất sự kiện an ninh.
     */
    switch (currentState)
    {
        case STATE_DISARM:
            if (is_pin_correct)
            {
                if (door_open)
                {
                    FSM_ClearPin();
                    FSM_SetError("ARM FAILED: Door Open!", 2000U);
                    SD_Log_Event("DISARM arm rejected: door open");
                }
                else
                {
                    FSM_TransitionTo(STATE_EXIT_DELAY, now,
                                     "DISARM -> EXIT_DELAY (valid PIN)");
                }
            }
            break;

        case STATE_EXIT_DELAY:
            if (is_pin_correct)
            {
                FSM_TransitionTo(STATE_DISARM, now,
                                 "EXIT_DELAY -> DISARM (cancelled by PIN)");
            }
            else if (Time_HasElapsed(now, state_start_tick, EXIT_DELAY_MS))
            {
                if (door_open)
                {
                    FSM_TransitionTo(STATE_DISARM, now,
                                     "EXIT_DELAY -> DISARM (door left open)");
                    FSM_SetError("ARM FAILED: Door Open!", 2500U);
                }
                else
                {
                    FSM_TransitionTo(STATE_ARMED, now,
                                     "EXIT_DELAY -> ARMED");
                }
            }
            break;

        case STATE_ARMED:
            if (is_pin_correct)
            {
                FSM_TransitionTo(STATE_DISARM, now,
                                 "ARMED -> DISARM (valid PIN)");
            }
            else if (vib_level == VIB_HEAVY)
            {
                FSM_TransitionTo(STATE_ALARM_EMERGE, now,
                                 "ARMED -> ALARM_EMERGE (heavy vibration)");
            }
            else if (door_open)
            {
                /* Cửa mở hợp lệ bắt đầu thời gian cho chủ nhà nhập PIN. */
                FSM_TransitionTo(STATE_ENTRY_DELAY, now,
                                 "ARMED -> ENTRY_DELAY (door opened)");
            }
            else if (pir_motion)
            {
                /* PIR khi cửa chưa mở là chuyển động bất thường trong vùng bảo vệ. */
                FSM_TransitionTo(STATE_ALARM_EMERGE, now,
                                 "ARMED -> ALARM_EMERGE (interior PIR)");
            }
            else if (vib_level == VIB_LIGHT)
            {
                FSM_TransitionTo(STATE_ENTRY_DELAY, now,
                                 "ARMED -> ENTRY_DELAY (light vibration)");
            }
            break;

        case STATE_ENTRY_DELAY:
            if (is_pin_correct)
            {
                FSM_TransitionTo(STATE_TEMP_DISARM, now,
                                 "ENTRY_DELAY -> TEMP_DISARM (valid PIN)");
            }
            else if (vib_level == VIB_HEAVY)
            {
                FSM_TransitionTo(STATE_ALARM_EMERGE, now,
                                 "ENTRY_DELAY -> ALARM_EMERGE (heavy vibration)");
            }
            else if (Time_HasElapsed(now, state_start_tick, ENTRY_DELAY_MS))
            {
                FSM_TransitionTo(STATE_ALARM_EMERGE, now,
                                 "ENTRY_DELAY -> ALARM_EMERGE (timeout)");
            }
            /* Door/PIR được bỏ qua: đây là chuyển động dự kiến trong lối vào. */
            break;

        case STATE_TEMP_DISARM:
            if (is_pin_correct)
            {
                FSM_TransitionTo(STATE_DISARM, now,
                                 "TEMP_DISARM -> DISARM (valid PIN)");
            }
            else if (vib_level == VIB_HEAVY)
            {
                FSM_TransitionTo(STATE_ALARM_EMERGE, now,
                                 "TEMP_DISARM -> ALARM_EMERGE (heavy vibration)");
            }
            else if (Time_HasElapsed(now, state_start_tick, TEMP_DISARM_MS))
            {
                FSM_TransitionTo(door_open ? STATE_ALARM_EMERGE : STATE_ARMED, now,
                                 door_open
                                     ? "TEMP_DISARM -> ALARM_EMERGE (door left open)"
                                     : "TEMP_DISARM -> ARMED (auto-rearm)");
            }
            break;

        case STATE_ALARM_EMERGE:
            if (is_pin_correct)
            {
                FSM_TransitionTo(STATE_TEMP_ALARM, now,
                                 "ALARM_EMERGE -> TEMP_ALARM (valid PIN)");
            }
            break;

        case STATE_TEMP_ALARM:
            if (is_pin_correct)
            {
                FSM_TransitionTo(STATE_DISARM, now,
                                 "TEMP_ALARM -> DISARM (valid PIN)");
            }
            else if (vib_level == VIB_HEAVY)
            {
                FSM_TransitionTo(STATE_ALARM_EMERGE, now,
                                 "TEMP_ALARM -> ALARM_EMERGE (heavy vibration)");
            }
            else if (Time_HasElapsed(now, state_start_tick, TEMP_ALARM_MS))
            {
                FSM_TransitionTo(door_open ? STATE_ALARM_EMERGE : STATE_ARMED, now,
                                 door_open
                                     ? "TEMP_ALARM -> ALARM_EMERGE (door still open)"
                                     : "TEMP_ALARM -> ARMED (scene safe)");
            }
            break;

        default:
            FSM_TransitionTo(STATE_DISARM, now,
                             "Invalid FSM state -> DISARM");
            break;
    }

    if (pin_submitted && !is_pin_correct)
    {
        FSM_HandleWrongPin(now);
    }

    FSM_Update_Outputs(now);
    FSM_Render_OLED(now, door_open, pir_motion, vib_level);
}
