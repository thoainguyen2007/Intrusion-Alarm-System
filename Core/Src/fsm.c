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
static bool entry_pir_ready_tracking = false;
static uint32_t entry_pir_ready_tick = 0;
static bool entry_vib_quiet_tracking = false;
static uint32_t entry_vib_quiet_tick = 0;
typedef enum {
    ENTRY_TRIGGER_NONE = 0,
    ENTRY_TRIGGER_PIR,
    ENTRY_TRIGGER_VIBRATION
} EntryTrigger_t;
static EntryTrigger_t entry_trigger = ENTRY_TRIGGER_NONE;
static bool log_door_open = false;
static bool log_pir_ready = false;
static bool log_pir_motion = false;
static VibLevel_t log_vib_level = VIB_NONE;

/* Bộ đệm nhập mã PIN từ Keypad */
static char pin_buffer[PIN_MAX_LEN + 1];
static uint8_t pin_idx = 0;
static uint8_t failed_pin_attempts = 0;
static bool pin_locked = false;
static uint32_t pin_lockout_deadline = 0;

/* Biến thông báo lỗi tạm thời lên màn hình OLED */
static char error_banner[32] = "";
static uint32_t error_banner_timeout = 0;

/* Key feedback is an overlay owned by the FSM, never a blocking direct write. */
static bool key_beep_active = false;
static uint32_t key_beep_deadline = 0U;
static bool temp_alarm_output_on = true;
static uint32_t temp_alarm_toggle_tick = 0U;

#define TEMP_ALARM_HALF_PERIOD_START_MS 125U
#define TEMP_ALARM_HALF_PERIOD_END_MS   750U

/* ==================================================================== */
/*                  HÀM GHI NHẬT KÝ THẺ NHỚ SD (FATFS)                  */
/* ==================================================================== */
static void SD_Log_Event(const char *event_msg)
{
    char record[128];
    const char *pir = !log_pir_ready ? "WARMUP" :
                      (log_pir_motion ? "ACTIVE" : "READY");
    const char *vib = (log_vib_level == VIB_HEAVY) ? "HEAVY" :
                      (log_vib_level == VIB_LIGHT ? "LIGHT" : "NONE");
    const char *entry = (entry_trigger == ENTRY_TRIGGER_PIR) ? "PIR" :
                        (entry_trigger == ENTRY_TRIGGER_VIBRATION ? "VIB" : "NONE");

    printf("[LOG] %s\r\n", event_msg);
    (void)snprintf(record, sizeof(record), "%s | D=%s P=%s V=%s E=%s",
                   event_msg, log_door_open ? "OPEN" : "CLOSED",
                   pir, vib, entry);
    if (!SD_Logger_Enqueue(record))
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

    /* Log before clearing entry_trigger so the record retains the source that
       led into, or is currently leaving, ENTRY_DELAY. */
    SD_Log_Event(reason);
    currentState = next_state;
    state_start_tick = now;
    if (next_state == STATE_TEMP_ALARM)
    {
        temp_alarm_output_on = true;
        temp_alarm_toggle_tick = now;
    }
    error_banner[0] = '\0';
    FSM_ClearPin();
    Vibration_Reset();
    entry_pir_ready_tracking = false;
    entry_vib_quiet_tracking = false;
    if (next_state != STATE_ENTRY_DELAY) entry_trigger = ENTRY_TRIGGER_NONE;

    printf("\r\n[FSM] %s\r\n", reason);
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
    entry_pir_ready_tracking = false;
    entry_pir_ready_tick = 0U;
    entry_vib_quiet_tracking = false;
    entry_vib_quiet_tick = 0U;
    entry_trigger = ENTRY_TRIGGER_NONE;
    error_banner[0] = '\0';
    error_banner_timeout = 0;
    key_beep_active = false;
    key_beep_deadline = 0U;
    temp_alarm_output_on = true;
    temp_alarm_toggle_tick = state_start_tick;

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

void Buzzer_RequestKeyBeep(void)
{
    /* ENTRY_DELAY already has a fast warning pattern. TEMP_DISARM is an
       automatic 30s safety verification and ignores further PIN input.
       Alarm states own the siren and must not be interrupted by key beeps. */
    if (currentState == STATE_ENTRY_DELAY ||
        currentState == STATE_TEMP_DISARM ||
        currentState == STATE_ALARM_EMERGE ||
        currentState == STATE_TEMP_ALARM)
        return;

    key_beep_active = true;
    key_beep_deadline = HAL_GetTick() + 40U;
}

/* ==================================================================== */
/*            ĐIỀU KHIỂN CÒI BUZZER & LED THEO TỪNG TRẠNG THÁI          */
/* ==================================================================== */
static void FSM_Update_Outputs(uint32_t now)
{
    uint32_t phase = now - state_start_tick;
    bool buzzer_requested = false;
    bool led_on = false;

    switch (currentState)
    {
        case STATE_DISARM:
            /* 0.5 Hz: 1s ON / 1s OFF. */
            led_on = ((phase % 2000U) < 1000U);
            break;

        case STATE_EXIT_DELAY:
            /* 1 Hz: buzzer 100ms ON; LED 500ms ON / 500ms OFF. */
            buzzer_requested = ((phase % 1000U) < 100U);
            led_on = ((phase % 1000U) < 500U);
            break;

        case STATE_ARMED:
            /* Patrol pulse: 50ms ON every 1.5s. */
            led_on = ((phase % 1500U) < 50U);
            break;

        case STATE_ENTRY_DELAY:
            /* 4 Hz warning: 100ms ON / 150ms OFF, LED synchronized. */
            buzzer_requested = ((phase % 250U) < 100U);
            led_on = buzzer_requested;
            break;

        case STATE_TEMP_DISARM:
        {
            /* Two 80ms pulses at the beginning of each 1s window. */
            uint32_t double_blink_phase = phase % 1000U;
            led_on = (double_blink_phase < 80U) ||
                     (double_blink_phase >= 180U && double_blink_phase < 260U);
            break;
        }

        case STATE_ALARM_EMERGE:
            /* Continuous siren; LED 10 Hz (50ms ON / 50ms OFF). */
            buzzer_requested = true;
            led_on = ((phase % 100U) < 50U);
            break;

        case STATE_TEMP_ALARM:
        {
            /* Synchronized fade-out rhythm: each ON/OFF half-period grows
               linearly from 125ms to 750ms over the 30s verification window. */
            uint32_t fade_elapsed = (phase < TEMP_ALARM_MS) ? phase : TEMP_ALARM_MS;
            uint32_t half_period = TEMP_ALARM_HALF_PERIOD_START_MS +
                (uint32_t)(((uint64_t)fade_elapsed *
                           (TEMP_ALARM_HALF_PERIOD_END_MS -
                            TEMP_ALARM_HALF_PERIOD_START_MS)) / TEMP_ALARM_MS);

            if (Time_HasElapsed(now, temp_alarm_toggle_tick, half_period))
            {
                temp_alarm_toggle_tick = now;
                temp_alarm_output_on = !temp_alarm_output_on;
            }
            buzzer_requested = temp_alarm_output_on;
            led_on = temp_alarm_output_on;
            break;
        }
    }

    if (key_beep_active && Time_DeadlineReached(now, key_beep_deadline))
        key_beep_active = false;

    if (key_beep_active && currentState != STATE_ENTRY_DELAY &&
        currentState != STATE_TEMP_DISARM &&
        currentState != STATE_ALARM_EMERGE && currentState != STATE_TEMP_ALARM)
        buzzer_requested = true;

    Buzzer_SetState(buzzer_requested);
    HAL_GPIO_WritePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin,
                      led_on ? GPIO_PIN_RESET : GPIO_PIN_SET);
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

#define OLED_TEXT_COLUMNS 18U

static void OLED_PutsCentered(uint16_t y, const char *text)
{
    char line[OLED_TEXT_COLUMNS + 1U];
    size_t length = strlen(text);
    if (length > OLED_TEXT_COLUMNS) length = OLED_TEXT_COLUMNS;
    memcpy(line, text, length);
    line[length] = '\0';

    uint16_t width = (uint16_t)(length * Font_7x10.FontWidth);
    uint16_t x = (width < SSD1306_WIDTH) ? (uint16_t)((SSD1306_WIDTH - width) / 2U) : 0U;
    SSD1306_GotoXY(x, y);
    SSD1306_Puts(line, &Font_7x10, SSD1306_COLOR_WHITE);
}

static void OLED_PutsColumn(uint16_t x, uint16_t width, uint16_t y,
                            const char *text)
{
    char line[9];
    size_t length = strlen(text);
    if (length > 8U) length = 8U;
    memcpy(line, text, length);
    line[length] = '\0';

    uint16_t text_width = (uint16_t)(length * Font_7x10.FontWidth);
    uint16_t text_x = x;
    if (text_width < width) text_x = (uint16_t)(x + (width - text_width) / 2U);
    SSD1306_GotoXY(text_x, y);
    SSD1306_Puts(line, &Font_7x10, SSD1306_COLOR_WHITE);
}

static void OLED_DrawProgress(uint16_t y, uint32_t now,
                              uint32_t start, uint32_t duration)
{
    const uint16_t outer_width = 124U;
    const uint16_t inner_width = 120U;
    uint32_t elapsed = now - start;
    if (elapsed > duration) elapsed = duration;

    uint16_t fill = (uint16_t)(((uint64_t)elapsed * inner_width) / duration);
    SSD1306_DrawRectangle(2U, y, outer_width, 8U, SSD1306_COLOR_WHITE);
    if (fill > 0U)
    {
        for (uint16_t row = y + 2U; row <= y + 5U; ++row)
            SSD1306_DrawLine(4U, row, (uint16_t)(3U + fill), row, SSD1306_COLOR_WHITE);
    }
}

static uint32_t OLED_RemainingFrom(uint32_t now, uint32_t start, uint32_t duration)
{
    uint32_t elapsed = now - start;
    if (elapsed >= duration) return 0U;
    return (duration - elapsed + 999U) / 1000U;
}

static void OLED_DrawNotice(const char *message, bool locked)
{
    char first[OLED_TEXT_COLUMNS + 1U] = "";
    char second[OLED_TEXT_COLUMNS + 1U] = "";
    size_t length = strlen(message);
    size_t split = (length > OLED_TEXT_COLUMNS) ? OLED_TEXT_COLUMNS : length;

    if (length > OLED_TEXT_COLUMNS)
    {
        while (split > 0U && message[split] != ' ') --split;
        if (split == 0U) split = OLED_TEXT_COLUMNS;
    }

    memcpy(first, message, split);
    first[split] = '\0';
    while (split < length && message[split] == ' ') ++split;
    size_t second_length = length - split;
    if (second_length > OLED_TEXT_COLUMNS) second_length = OLED_TEXT_COLUMNS;
    memcpy(second, &message[split], second_length);
    second[second_length] = '\0';

    OLED_PutsCentered(18U, first);
    if (second[0] != '\0') OLED_PutsCentered(31U, second);
    OLED_PutsCentered(47U, locked ? "PLEASE WAIT" : "CHECK AND RETRY");
}

static void FSM_Render_OLED(uint32_t now, bool door_open, bool pir_ready,
                            bool pir_motion, VibLevel_t vib_level)
{
    static uint32_t last_render_tick = 0;
    if (!Time_HasElapsed(now, last_render_tick, 500U)) return; /* 2 FPS: giảm I2C blocking */
    last_render_tick = now;

    SSD1306_Fill(SSD1306_COLOR_BLACK);
    char buf[32];
    char pin_display[8];
    GetMaskedPin(pin_display, sizeof(pin_display));

    OLED_PutsCentered(1U, FSM_GetStateName(currentState));
    SSD1306_DrawLine(0U, 12U, 127U, 12U, SSD1306_COLOR_WHITE);

    /* 2. Kiểm tra nếu đang có banner lỗi thì ưu tiên hiển thị */
    if (error_banner[0] != '\0' && !Time_DeadlineReached(now, error_banner_timeout))
    {
        OLED_DrawNotice(error_banner, pin_locked);
        SSD1306_UpdateScreen();
        return;
    }

    /* 3. Hiển thị nội dung chuyên biệt cho từng trạng thái (< 18 ký tự/dòng) */
    switch (currentState)
    {
        case STATE_DISARM:
            OLED_PutsCentered(15U, "SYSTEM STANDBY");
            snprintf(buf, sizeof(buf), "PIN [%s]  #=ARM", pin_display);
            OLED_PutsCentered(28U, buf);
            snprintf(buf, sizeof(buf), "D:%s  V:%s", door_open ? "OPEN" : "OK",
                     (vib_level == VIB_HEAVY) ? "HEAVY" : (vib_level == VIB_LIGHT ? "LIGHT" : "OK"));
            OLED_PutsCentered(44U, buf);
            break;

        case STATE_EXIT_DELAY:
        {
            uint32_t remain = FSM_RemainingSeconds(now, EXIT_DELAY_MS);
            OLED_PutsCentered(15U, "EXIT COUNTDOWN");
            snprintf(buf, sizeof(buf), "ARM IN %2lus", remain);
            OLED_PutsCentered(28U, buf);
            snprintf(buf, sizeof(buf), "D:%s  #=CANCEL", door_open ? "OPEN" : "OK");
            OLED_PutsCentered(41U, buf);
            OLED_DrawProgress(55U, now, state_start_tick, EXIT_DELAY_MS);
            break;
        }

        case STATE_ARMED:
            OLED_PutsCentered(15U, "SECURITY ACTIVE");
            snprintf(buf, sizeof(buf), "PIN [%s] #=OFF", pin_display);
            OLED_PutsCentered(28U, buf);
            snprintf(buf, sizeof(buf), "D:%s PIR:%s", door_open ? "OPEN" : "OK",
                     !pir_ready ? "WARM" : (pir_motion ? "ACTIVE" : "READY"));
            OLED_PutsCentered(44U, buf);
            break;

        case STATE_ENTRY_DELAY:
        {
            uint32_t remain = FSM_RemainingSeconds(now, ENTRY_DELAY_MS);
            uint32_t pir_quiet = entry_pir_ready_tracking
                ? OLED_RemainingFrom(now, entry_pir_ready_tick,
                                     ENTRY_PIR_READY_REARM_MS)
                : ENTRY_PIR_READY_REARM_MS / 1000U;
            uint32_t vib_quiet = entry_vib_quiet_tracking
                ? OLED_RemainingFrom(now, entry_vib_quiet_tick,
                                     ENTRY_VIB_QUIET_REARM_MS)
                : ENTRY_VIB_QUIET_REARM_MS / 1000U;
            bool pir_ok = entry_pir_ready_tracking && (pir_quiet == 0U);
            bool vib_ok = entry_vib_quiet_tracking && (vib_quiet == 0U);
            char pir_col[9];
            char vib_col[9];

            OLED_PutsCentered(15U, "VERIFY EVENT");
            snprintf(buf, sizeof(buf), "PIN [%s] %2lus", pin_display, remain);
            OLED_PutsCentered(28U, buf);

            if (pir_ok) snprintf(pir_col, sizeof(pir_col), "PIR:OK");
            else if (!pir_ready) snprintf(pir_col, sizeof(pir_col), "PIR:WARM");
            else if (pir_motion) snprintf(pir_col, sizeof(pir_col), "PIR:ACT");
            else snprintf(pir_col, sizeof(pir_col), "PIR:%lus", pir_quiet);

            if (vib_ok) snprintf(vib_col, sizeof(vib_col), "VIB:OK");
            else if (vib_level == VIB_LIGHT) snprintf(vib_col, sizeof(vib_col), "VIB:LGT");
            else snprintf(vib_col, sizeof(vib_col), "VIB:%lus", vib_quiet);

            SSD1306_DrawLine(63U, 40U, 63U, 52U, SSD1306_COLOR_WHITE);
            OLED_PutsColumn(0U, 62U, 41U, pir_col);
            OLED_PutsColumn(65U, 63U, 41U, vib_col);
            OLED_DrawProgress(55U, now, state_start_tick, ENTRY_DELAY_MS);
            break;
        }

        case STATE_TEMP_DISARM:
        {
            uint32_t remain = FSM_RemainingSeconds(now, TEMP_DISARM_MS);
            OLED_PutsCentered(15U, "VERIFYING SAFE");
            snprintf(buf, sizeof(buf), "DISARM IN %2lus", remain);
            OLED_PutsCentered(28U, buf);
            snprintf(buf, sizeof(buf), "DOOR:%s", door_open ? "OPEN - ALARM" : "CLOSED");
            OLED_PutsCentered(41U, buf);
            OLED_DrawProgress(55U, now, state_start_tick, TEMP_DISARM_MS);
            break;
        }

        case STATE_ALARM_EMERGE:
            OLED_PutsCentered(15U, "INTRUSION ALERT");
            snprintf(buf, sizeof(buf), "PIN [%s]", pin_display);
            OLED_PutsCentered(28U, buf);
            OLED_PutsCentered(44U, "PIN + # = VERIFY");
            break;

        case STATE_TEMP_ALARM:
        {
            uint32_t remain = FSM_RemainingSeconds(now, TEMP_ALARM_MS);
            OLED_PutsCentered(15U, "VERIFYING EVENT");
            snprintf(buf, sizeof(buf), "SIREN ON  %2lus", remain);
            OLED_PutsCentered(28U, buf);
            snprintf(buf, sizeof(buf), "DOOR:%s", door_open ? "OPEN - CLOSE" : "CLOSED");
            OLED_PutsCentered(41U, buf);
            OLED_DrawProgress(55U, now, state_start_tick, TEMP_ALARM_MS);
            break;
        }
    }

    SSD1306_UpdateScreen();
}

/* ==================================================================== */
/*                   XỬ LÝ CHUYỂN DỊCH TRẠNG THÁI CHÍNH                 */
/* ==================================================================== */
void FSM_Process(char key_pressed, bool door_open, bool pir_ready,
                 bool pir_motion, VibLevel_t vib_level)
{
    uint32_t now = HAL_GetTick();
    bool pin_submitted = false;
    bool is_pin_correct = false;

    /* One coherent sensor snapshot is attached to every event produced during
       this FSM cycle. Periodic raw sensor samples remain UART-only. */
    log_door_open = door_open;
    log_pir_ready = pir_ready;
    log_pir_motion = pir_motion;
    log_vib_level = vib_level;

    /* Lockout chỉ chặn bàn phím; cảm biến và timeout vẫn luôn hoạt động. */
    if (pin_locked && Time_DeadlineReached(now, pin_lockout_deadline))
    {
        pin_locked = false;
        failed_pin_attempts = 0;
        error_banner[0] = '\0';
        FSM_ClearPin();
        SD_Log_Event("PIN keypad lockout expired");
    }

    if (!pin_locked && currentState != STATE_TEMP_DISARM &&
        currentState != STATE_TEMP_ALARM &&
        key_pressed != 0 && key_pressed != '-')
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
     * 1) Cảm biến an ninh và timeout.
     * 2) PIN chỉ được xét sau khi chu kỳ hiện tại vượt qua lớp an ninh.
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
            if (vib_level == VIB_HEAVY)
            {
                FSM_TransitionTo(STATE_ALARM_EMERGE, now,
                                 "ARMED -> ALARM_EMERGE (heavy vibration)");
            }
            else if (door_open)
            {
                /* Cửa mở khi đã ARM là xâm nhập trực tiếp. */
                FSM_TransitionTo(STATE_ALARM_EMERGE, now,
                                 "ARMED -> ALARM_EMERGE (door opened)");
            }
            else if (pir_motion)
            {
                entry_trigger = ENTRY_TRIGGER_PIR;
                FSM_TransitionTo(STATE_ENTRY_DELAY, now,
                                 "ARMED -> ENTRY_DELAY (PIR motion)");
            }
            else if (vib_level == VIB_LIGHT)
            {
                entry_trigger = ENTRY_TRIGGER_VIBRATION;
                FSM_TransitionTo(STATE_ENTRY_DELAY, now,
                                 "ARMED -> ENTRY_DELAY (light vibration)");
            }
            else if (is_pin_correct)
            {
                FSM_TransitionTo(STATE_DISARM, now,
                                 "ARMED -> DISARM (valid PIN)");
            }
            break;

        case STATE_ENTRY_DELAY:
            /* Hai cột xác minh độc lập. Mỗi cảm biến chỉ reset bộ đếm của
               chính nó; cả PIR và rung phải cùng OK mới hủy cảnh báo giả. */
            if (!pir_ready || pir_motion)
            {
                if (entry_pir_ready_tracking)
                    printf("[FSM] ENTRY_DELAY: PIR READY timer cancelled.\r\n");
                entry_pir_ready_tracking = false;
            }
            else if (!entry_pir_ready_tracking)
            {
                entry_pir_ready_tracking = true;
                entry_pir_ready_tick = now;
                printf("[FSM] ENTRY_DELAY: PIR READY timer started (10s).\r\n");
            }

            if (vib_level == VIB_LIGHT)
            {
                if (entry_vib_quiet_tracking)
                    printf("[FSM] ENTRY_DELAY: VIB quiet timer reset by LIGHT.\r\n");
                entry_vib_quiet_tracking = false;
            }
            else if (vib_level == VIB_NONE && !entry_vib_quiet_tracking)
            {
                entry_vib_quiet_tracking = true;
                entry_vib_quiet_tick = now;
                printf("[FSM] ENTRY_DELAY: VIB quiet timer started (5s).\r\n");
            }

            if (vib_level == VIB_HEAVY)
            {
                FSM_TransitionTo(STATE_ALARM_EMERGE, now,
                                 "ENTRY_DELAY -> ALARM_EMERGE (heavy vibration)");
            }
            else if (door_open)
            {
                FSM_TransitionTo(STATE_ALARM_EMERGE, now,
                                 "ENTRY_DELAY -> ALARM_EMERGE (door opened)");
            }
            else if (entry_pir_ready_tracking &&
                     Time_HasElapsed(now, entry_pir_ready_tick,
                                     ENTRY_PIR_READY_REARM_MS) &&
                     entry_vib_quiet_tracking &&
                     Time_HasElapsed(now, entry_vib_quiet_tick,
                                     ENTRY_VIB_QUIET_REARM_MS))
            {
                FSM_TransitionTo(STATE_ARMED, now,
                                 "ENTRY_DELAY -> ARMED (PIR/VIB verification OK)");
            }
            else if (Time_HasElapsed(now, state_start_tick, ENTRY_DELAY_MS))
            {
                FSM_TransitionTo(STATE_ALARM_EMERGE, now,
                                 "ENTRY_DELAY -> ALARM_EMERGE (timeout)");
            }
            else if (is_pin_correct)
            {
                FSM_TransitionTo(STATE_TEMP_DISARM, now,
                                 "ENTRY_DELAY -> TEMP_DISARM (valid PIN)");
            }
            /* PIR ACTIVE và VIB LIGHT chỉ reset cột tương ứng. */
            break;

        case STATE_TEMP_DISARM:
            if (vib_level == VIB_HEAVY)
            {
                FSM_TransitionTo(STATE_ALARM_EMERGE, now,
                                 "TEMP_DISARM -> ALARM_EMERGE (heavy vibration)");
            }
            else if (door_open)
            {
                FSM_TransitionTo(STATE_ALARM_EMERGE, now,
                                 "TEMP_DISARM -> ALARM_EMERGE (door opened)");
            }
            else if (Time_HasElapsed(now, state_start_tick, TEMP_DISARM_MS))
            {
                FSM_TransitionTo(STATE_DISARM, now,
                                 "TEMP_DISARM -> DISARM (30s safe)");
            }
            break;

        case STATE_ALARM_EMERGE:
            if (is_pin_correct && !door_open)
            {
                FSM_TransitionTo(STATE_TEMP_ALARM, now,
                                 "ALARM_EMERGE -> TEMP_ALARM (door closed, valid PIN)");
            }
            else if (is_pin_correct)
            {
                FSM_ClearPin();
                FSM_SetError("CLOSE DOOR FIRST", 2000U);
                SD_Log_Event("ALARM_EMERGE unlock rejected: door open");
            }
            break;

        case STATE_TEMP_ALARM:
            if (door_open)
            {
                FSM_TransitionTo(STATE_ALARM_EMERGE, now,
                                 "TEMP_ALARM -> ALARM_EMERGE (door reopened)");
            }
            else if (vib_level == VIB_HEAVY)
            {
                FSM_TransitionTo(STATE_ALARM_EMERGE, now,
                                 "TEMP_ALARM -> ALARM_EMERGE (heavy vibration)");
            }
            else if (Time_HasElapsed(now, state_start_tick, TEMP_ALARM_MS))
            {
                FSM_TransitionTo(STATE_ARMED, now,
                                 "TEMP_ALARM -> ARMED (30s safe, door closed)");
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
    FSM_Render_OLED(now, door_open, pir_ready, pir_motion, vib_level);
}
