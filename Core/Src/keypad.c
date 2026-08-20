/**
  ******************************************************************************
  * @file    keypad.c
  * @brief   4x4 Matrix Keypad Driver with debounce & release timeout.
  ******************************************************************************
  */

#include "keypad.h"

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
  * @brief  Scan Keypad and return pressed character or KEYPAD_NO_KEY
  */
char Keypad_GetKey(void)
{
    char key = KEYPAD_NO_KEY;

    for (int r = 0; r < 4; r++) {
        /* Set current row to LOW */
        HAL_GPIO_WritePin(ROW_PORTS[r], ROW_PINS[r], GPIO_PIN_RESET);

        for (int c = 0; c < 4; c++) {
            /* Check if column is pulled LOW by key press */
            if (HAL_GPIO_ReadPin(COL_PORTS[c], COL_PINS[c]) == GPIO_PIN_RESET) {
                /* 15ms Debounce */
                HAL_Delay(15);
                if (HAL_GPIO_ReadPin(COL_PORTS[c], COL_PINS[c]) == GPIO_PIN_RESET) {
                    key = KEYMAP[r][c];

                    /* Wait for key release with 300ms timeout */
                    uint32_t timeout = HAL_GetTick();
                    while ((HAL_GPIO_ReadPin(COL_PORTS[c], COL_PINS[c]) == GPIO_PIN_RESET) &&
                           (HAL_GetTick() - timeout < 300)) {
                        /* Non-blocking release wait up to 300ms */
                    }

                    /* Restore row to HIGH before exit */
                    HAL_GPIO_WritePin(ROW_PORTS[r], ROW_PINS[r], GPIO_PIN_SET);
                    return key;
                }
            }
        }

        /* Restore current row to HIGH */
        HAL_GPIO_WritePin(ROW_PORTS[r], ROW_PINS[r], GPIO_PIN_SET);
    }

    return KEYPAD_NO_KEY;
}
