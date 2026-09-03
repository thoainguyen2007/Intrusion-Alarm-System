/**
  ******************************************************************************
  * @file    keypad.c
  * @brief   4x4 keypad polling with 20 ms debounce and press-change reporting.
  ******************************************************************************
  */

#include "keypad.h"
#include "time_utils.h"

static GPIO_TypeDef* ROW_PORTS[4] = { R1_GPIO_Port, R2_GPIO_Port, R3_GPIO_Port, R4_GPIO_Port };
static const uint16_t ROW_PINS[4] = { R1_Pin, R2_Pin, R3_Pin, R4_Pin };

static GPIO_TypeDef* COL_PORTS[4] = { C1_GPIO_Port, C2_GPIO_Port, C3_GPIO_Port, C4_GPIO_Port };
static const uint16_t COL_PINS[4] = { C1_Pin, C2_Pin, C3_Pin, C4_Pin };

static const char KEYMAP[4][4] = {
    { '1', '2', '3', 'A' },
    { '4', '5', '6', 'B' },
    { '7', '8', '9', 'C' },
    { '*', '0', '#', 'D' }
};

/**
  * @brief  Initialize Keypad rows to HIGH
  */
void Keypad_Init(void)
{
    for (int r = 0; r < 4; r++) {
        HAL_GPIO_WritePin(ROW_PORTS[r], ROW_PINS[r], GPIO_PIN_SET);
    }
}

/**
  * @brief  Scan Keypad and return pressed character or KEYPAD_NO_KEY (Non-blocking)
  */
char Keypad_GetKey(void)
{
    static char last_stable_key = KEYPAD_NO_KEY;
    static char last_read_key = KEYPAD_NO_KEY;
    static uint32_t last_debounce_tick = 0;

    uint32_t now = HAL_GetTick();
    char current_raw_key = KEYPAD_NO_KEY;

    /* Quét nhanh qua 4 hàng và 4 cột */
    for (int r = 0; r < 4; r++) {
        HAL_GPIO_WritePin(ROW_PORTS[r], ROW_PINS[r], GPIO_PIN_RESET);

        for (int c = 0; c < 4; c++) {
            if (HAL_GPIO_ReadPin(COL_PORTS[c], COL_PINS[c]) == GPIO_PIN_RESET) {
                current_raw_key = KEYMAP[r][c];
                break;
            }
        }

        HAL_GPIO_WritePin(ROW_PORTS[r], ROW_PINS[r], GPIO_PIN_SET);
        if (current_raw_key != KEYPAD_NO_KEY) {
            break;
        }
    }

    /* Khử rung phím (Debounce) 20ms hoàn toàn Non-blocking */
    if (current_raw_key != last_read_key) {
        last_read_key = current_raw_key;
        last_debounce_tick = now;
    }

    if (Time_HasElapsed(now, last_debounce_tick, 20U)) {
        if (current_raw_key != last_stable_key) {
            last_stable_key = current_raw_key;
            /* Chỉ kích hoạt khi vừa nhấn phím xuống (Sườn xuống - Edge Triggered) */
            if (last_stable_key != KEYPAD_NO_KEY) {
                return last_stable_key;
            }
        }
    }

    return KEYPAD_NO_KEY;
}
