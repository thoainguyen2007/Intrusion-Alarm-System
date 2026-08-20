/**
  ******************************************************************************
  * @file    keypad.h
  * @brief   Header for 4x4 Matrix Keypad driver.
  ******************************************************************************
  */

#ifndef __KEYPAD_H
#define __KEYPAD_H

#include "stm32f1xx_hal.h"
#include "main.h"

#define KEYPAD_NO_KEY '\0'

void Keypad_Init(void);
char Keypad_GetKey(void);

#endif /* __KEYPAD_H */
