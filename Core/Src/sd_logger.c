#include "sd_logger.h"
#include "fatfs.h"
#include "sd_spi.h"
#include "time_utils.h"
#include "stm32f1xx_hal.h"
#include <stdio.h>
#include <string.h>

#define SD_LOG_QUEUE_DEPTH 16U
#define SD_LOG_MESSAGE_SIZE 96U
#define SD_RETRY_INTERVAL_MS 5000U

typedef struct {
    uint32_t tick;
    char message[SD_LOG_MESSAGE_SIZE];
} SD_LogEntry_t;

static SD_LogEntry_t queue[SD_LOG_QUEUE_DEPTH];
static uint8_t head;
static uint8_t tail;
static uint8_t count;
static uint32_t dropped_count;
static uint32_t last_retry_tick;
static bool online;

static void mark_offline(FRESULT reason)
{
    online = false;
    SD_SPI_InvalidateCard();
    (void)f_mount(NULL, USERPath, 0);
    last_retry_tick = HAL_GetTick();
    printf("[FATFS] Logger offline (FR=%u), queued=%u\r\n",
           (unsigned int)reason, count);
}

static FRESULT append_entry(const SD_LogEntry_t *entry, UINT *bytes_written)
{
    FIL file;
    FRESULT result = f_open(&file, "0:LOG.TXT", FA_OPEN_ALWAYS | FA_WRITE);
    if (result != FR_OK) return result;

    char line[128];
    int length = snprintf(line, sizeof(line), "[%lums] %s\r\n",
                          entry->tick, entry->message);
    if (length <= 0 || length >= (int)sizeof(line)) {
        (void)f_close(&file);
        return FR_INVALID_PARAMETER;
    }

    result = f_lseek(&file, f_size(&file));
    *bytes_written = 0U;
    if (result == FR_OK)
        result = f_write(&file, line, (UINT)length, bytes_written);
    if (result == FR_OK && *bytes_written != (UINT)length)
        result = FR_DISK_ERR;
    if (result == FR_OK) result = f_sync(&file);

    FRESULT close_result = f_close(&file);
    return (result == FR_OK) ? close_result : result;
}

void SD_Logger_Init(bool storage_ready)
{
    head = 0U;
    tail = 0U;
    count = 0U;
    dropped_count = 0U;
    last_retry_tick = HAL_GetTick();
    online = storage_ready;
}

bool SD_Logger_Enqueue(const char *message)
{
    if (message == NULL || message[0] == '\0') return false;
    if (count >= SD_LOG_QUEUE_DEPTH) {
        ++dropped_count;
        return false;
    }

    queue[head].tick = HAL_GetTick();
    (void)snprintf(queue[head].message, sizeof(queue[head].message), "%s", message);
    head = (uint8_t)((head + 1U) % SD_LOG_QUEUE_DEPTH);
    ++count;
    return true;
}

void SD_Logger_Process(bool allow_io)
{
    /* Never enter blocking FatFs/SPI code while the alarm is protecting. */
    if (!allow_io) return;

    uint32_t now = HAL_GetTick();
    if (!online) {
        if (!Time_HasElapsed(now, last_retry_tick, SD_RETRY_INTERVAL_MS)) return;

        last_retry_tick = now;
        SD_SPI_Result_t sd_result = SD_SPI_InitCard();
        FRESULT mount_result = (sd_result == SD_SPI_OK)
                             ? f_mount(&USERFatFS, USERPath, 1) : FR_NOT_READY;
        if (mount_result == FR_OK) {
            online = true;
            printf("[FATFS] Logger storage recovered, queued=%u\r\n", count);
        } else {
            printf("[FATFS] Logger retry failed (SD=%s, FR=%u)\r\n",
                   SD_SPI_ResultString(sd_result), (unsigned int)mount_result);
        }
        return;
    }

    if (count == 0U) return;

    UINT bytes_written;
    FRESULT result = append_entry(&queue[tail], &bytes_written);
    if (result != FR_OK) {
        mark_offline(result);
        return;
    }

    tail = (uint8_t)((tail + 1U) % SD_LOG_QUEUE_DEPTH);
    --count;
    printf("[FATFS] Append LOG.TXT: OK (%u bytes), queued=%u\r\n",
           bytes_written, count);
}

bool SD_Logger_IsOnline(void) { return online; }
uint32_t SD_Logger_GetDroppedCount(void) { return dropped_count; }
