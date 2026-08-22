/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "fatfs.h"
#include "i2c.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ssd1306.h"
#include "fonts.h"
#include "keypad.h"
#include "sensors.h"
#include "fsm.h"
#include "sd_spi.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define PIR_WARMUP_MS         30000   /* Thời gian khởi động cảm biến PIR (30 giây) */
#define REED_DEBOUNCE_MS      50      /* Chống dội tiếp điểm từ cửa (50ms) */
#define PIR_DEBOUNCE_MS       200     /* Chống dội PIR (200ms) */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* Retarget printf qua UART1 */
#ifdef __GNUC__
int __io_putchar(int ch)
#else
int fputc(int ch, FILE *f)
#endif
{
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
  return ch;
}

/* Biến cảm biến Rung SW-420 (đã chuyển logic sang sensors.c) */
uint32_t last_vib_window_tick = 0;
uint32_t vib_alert_clear_tick = 0;

/* Biến cảm biến Từ Cửa (Reed Switch) có Debounce */
volatile uint8_t reed_triggered = 0;
volatile uint32_t last_reed_tick = 0;
uint8_t was_reed_triggered = 0; /* Lưu trạng thái cửa ở chu kỳ trước */

/* Biến cảm biến Chuyển động PIR có Warm-up & Lọc giữ trạng thái (Hold Latch) */
volatile uint8_t pir_triggered = 0;
volatile uint32_t pir_hold_tick = 0;
uint8_t was_pir_triggered = 0;
uint8_t pir_warmup_done_logged = 0;

/* Trạng thái phím vừa bấm */
char last_key = '-';
static uint8_t sd_sector_buffer[SD_SPI_BLOCK_SIZE];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/**
  * @brief  EXTI line detection callbacks for SW-420, Reed switch, PIR.
  */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  uint32_t now = HAL_GetTick();

  /* 1. Cảm biến Rung SW-420: Chuyển logic đếm vào module sensors */
  if (GPIO_Pin == VIR_IN_Pin)
  {
    Sensors_Vib_EXTI_Callback();
  }
  /* 2. Cảm biến Từ Cửa Reed: Chống dội tiếp điểm cơ khí 50ms */
  else if (GPIO_Pin == REED_IN_Pin)
  {
    if (now - last_reed_tick >= REED_DEBOUNCE_MS)
    {
      last_reed_tick = now;
      reed_triggered = (HAL_GPIO_ReadPin(REED_IN_GPIO_Port, REED_IN_Pin) == GPIO_PIN_SET) ? 1 : 0;
    }
  }
  /* 3. Cảm biến Chuyển Động PIR: Khóa trong giai đoạn Warm-up 30s & Giữ trạng thái ổn định */
  else if (GPIO_Pin == PIR_IN_Pin)
  {
    if (now >= PIR_WARMUP_MS)
    {
      if (HAL_GPIO_ReadPin(PIR_IN_GPIO_Port, PIR_IN_Pin) == GPIO_PIN_SET)
      {
        pir_triggered = 1;
        pir_hold_tick = now + 1500; /* Khóa giữ trạng thái phát hiện ít nhất 1.5 giây */
      }
    }
  }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_SPI1_Init();
  MX_TIM1_Init();
  MX_TIM2_Init();
  MX_USART1_UART_Init();
  MX_FATFS_Init();
  /* USER CODE BEGIN 2 */
  /* Đăng ký FatFs; việc truy cập vật lý được hoãn đến lúc mount chủ động. */
  f_mount(&USERFatFS, USERPath, 0);

  /* Khởi tạo bàn phím ma trận Keypad 4x4 */
  Keypad_Init();

  /* Đọc trạng thái ban đầu của Cửa (Reed Switch) khi vừa cấp nguồn */
  reed_triggered = (HAL_GPIO_ReadPin(REED_IN_GPIO_Port, REED_IN_Pin) == GPIO_PIN_SET) ? 1 : 0;
  was_reed_triggered = reed_triggered;

  /* Log khởi động qua UART1 */
  printf("\r\n========================================\r\n");
  printf("  INTRUSION ALARM SYSTEM - STM32F103\r\n");
  printf("  Firmware Ver 2.0 (Debounced & Classified)\r\n");
  printf("  PIR Warm-up Time: %d seconds...\r\n", PIR_WARMUP_MS / 1000);
  printf("========================================\r\n");

  /* Initialize the card and read sector 0 without modifying it. */
  SD_SPI_Result_t sd_result = SD_SPI_InitCard();
  printf("[SD] Init: %s (R1=0x%02X)\r\n",
         SD_SPI_ResultString(sd_result), SD_SPI_GetCardInfo()->last_r1);
  printf("[SD] SPI RX bytes: FF=%lu, 00=%lu, other=%lu\r\n",
         SD_SPI_GetBusStats()->ff_bytes,
         SD_SPI_GetBusStats()->zero_bytes,
         SD_SPI_GetBusStats()->other_bytes);
  if (sd_result == SD_SPI_NO_RESPONSE &&
      SD_SPI_GetBusStats()->ff_bytes != 0U &&
      SD_SPI_GetBusStats()->zero_bytes == 0U &&
      SD_SPI_GetBusStats()->other_bytes == 0U)
  {
    printf("[SD] DIAG: MISO stayed HIGH; check PA6/module DO, 5V input and 3.3V regulator output.\r\n");
  }
  if (sd_result == SD_SPI_OK)
  {
    printf("[SD] Type: %s, OCR=0x%08lX\r\n",
           SD_SPI_CardTypeString(SD_SPI_GetCardInfo()->type),
           SD_SPI_GetCardInfo()->ocr);
    sd_result = SD_SPI_ReadBlock(0U, sd_sector_buffer);
    printf("[SD] Read sector 0: %s\r\n", SD_SPI_ResultString(sd_result));
    if (sd_result == SD_SPI_OK)
    {
      printf("[SD] First bytes: %02X %02X %02X %02X, signature: %02X %02X\r\n",
             sd_sector_buffer[0], sd_sector_buffer[1],
             sd_sector_buffer[2], sd_sector_buffer[3],
             sd_sector_buffer[510], sd_sector_buffer[511]);
    }

    FRESULT mount_result = f_mount(&USERFatFS, USERPath, 1);
    printf("[FATFS] Mount: %s (FR=%u)\r\n",
           (mount_result == FR_OK) ? "OK" : "FAILED",
           (unsigned int)mount_result);
    if (mount_result == FR_OK)
    {
      DWORD free_clusters;
      FATFS *mounted_fs;
      FRESULT free_result = f_getfree(USERPath, &free_clusters, &mounted_fs);
      if (free_result == FR_OK)
      {
        uint32_t total_kib = (uint32_t)((mounted_fs->n_fatent - 2U) *
                             mounted_fs->csize / 2U);
        uint32_t free_kib = (uint32_t)(free_clusters * mounted_fs->csize / 2U);
        printf("[FATFS] Capacity: %lu KiB, free: %lu KiB\r\n",
               total_kib, free_kib);
      }
      else
      {
        printf("[FATFS] Space query failed (FR=%u)\r\n",
               (unsigned int)free_result);
      }
    }
  }

  /* Khởi tạo màn hình OLED SH1106 1.3 inch */
  SSD1306_Init(&hi2c1);
  SSD1306_Fill(SSD1306_COLOR_BLACK);
  SSD1306_GotoXY(10, 8);
  SSD1306_Puts("INTRUSION ALARM", &Font_7x10, SSD1306_COLOR_WHITE);
  SSD1306_GotoXY(16, 26);
  SSD1306_Puts("SYSTEM READY", &Font_7x10, SSD1306_COLOR_WHITE);
  SSD1306_GotoXY(12, 44);
  SSD1306_Puts("7-STATE FSM ACTIVE", &Font_7x10, SSD1306_COLOR_WHITE);
  SSD1306_UpdateScreen();
  HAL_Delay(1000);

  /* Khởi tạo Máy trạng thái hữu hạn FSM 7 trạng thái */
  FSM_Init();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    uint32_t now = HAL_GetTick();

    /* --- TÁC VỤ 1: Quét phím Keypad 4x4 --- */
    char key = Keypad_GetKey();
    if (key != KEYPAD_NO_KEY)
    {
      last_key = key;
      printf("[KEYPAD] Pressed: %c\r\n", key);

      /* Bíp còi PWM phản hồi ngắn nếu không ở trạng thái còi đang kêu */
      if (FSM_GetState() != STATE_ALARM_EMERGE && FSM_GetState() != STATE_ENTRY_DELAY)
      {
        Buzzer_SetState(true);
        HAL_Delay(10);
        Buzzer_SetState(false);
      }
    }

    /* --- TÁC VỤ 2: Xử lý Cảm biến Từ Cửa (Reed Switch) --- */
    if (reed_triggered == 0 && was_reed_triggered == 1)
    {
      /* Cửa vừa đóng: Reset xung do chấn động lúc sập cửa, bắt đầu giám sát rung */
      Vibration_Reset();
      printf("[SENSOR] REED: Door CLOSED. Vibration monitoring active.\r\n");
    }
    else if (reed_triggered == 1 && was_reed_triggered == 0)
    {
      printf("[SENSOR] REED: Door OPEN!\r\n");
    }
    was_reed_triggered = reed_triggered;

    /* --- TÁC VỤ 3: Xử lý Cảm biến Thân nhiệt PIR (HC-SR501) --- */
    if (now >= PIR_WARMUP_MS && !pir_warmup_done_logged)
    {
      pir_warmup_done_logged = 1;
      printf("[SENSOR] PIR: Warm-up Complete (30s). Motion monitoring ACTIVE!\r\n");
    }

    /* Tự động xóa trạng thái PIR khi chân đã về mức LOW và đã hết thời gian hold */
    if (pir_triggered && (now >= pir_hold_tick))
    {
      if (HAL_GPIO_ReadPin(PIR_IN_GPIO_Port, PIR_IN_Pin) == GPIO_PIN_RESET)
      {
        pir_triggered = 0;
      }
      else
      {
        /* Nếu người vẫn đang chuyển động trước cảm biến, gia hạn tiếp 1s */
        pir_hold_tick = now + 1000;
      }
    }

    if (pir_triggered == 1 && was_pir_triggered == 0)
    {
      printf("[SENSOR] PIR: Motion DETECTED!\r\n");
    }
    else if (pir_triggered == 0 && was_pir_triggered == 1)
    {
      printf("[SENSOR] PIR: Motion Ended (Quiet).\r\n");
    }
    was_pir_triggered = pir_triggered;

    /* --- TÁC VỤ 4: Đánh giá cửa sổ phân loại rung SW-420 mỗi 1.0 giây --- */
    if (now - last_vib_window_tick >= VIB_WINDOW_MS)
    {
      last_vib_window_tick = now;
      /* Chỉ phân tích rung khi CỬA ĐANG ĐÓNG (reed_triggered == 0) */
      Sensors_Process_Window(reed_triggered == 0);
    }

    /* --- TÁC VỤ 5: ĐIỀU PHỐI MÁY TRẠNG THÁI FSM 7 TRẠNG THÁI --- */
    /* FSM sẽ tự động quản lý còi Buzzer, LED Heartbeat/Siren, OLED UI và chuyển trạng thái */
    bool is_door_open = (reed_triggered == 1);
    bool is_pir_active = (now >= PIR_WARMUP_MS && pir_triggered == 1);
    VibLevel_t current_vib = Vibration_GetLevel();

    FSM_Process(key, is_door_open, is_pir_active, current_vib);

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
