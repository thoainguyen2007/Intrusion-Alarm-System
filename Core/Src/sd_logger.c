#include "sd_logger.h"
#include "fatfs.h"
#include "sd_spi.h"
#include "time_utils.h"
#include "stm32f1xx_hal.h"
#include <stdio.h>
#include <string.h>

#define SD_LOG_QUEUE_DEPTH 32U
#define SD_LOG_MESSAGE_SIZE 128U
#define SD_RETRY_INTERVAL_MS 5000U
#define SD_WRITE_INTERVAL_MS 100U

typedef struct {
    uint32_t sequence;
    uint32_t tick;
    char message[SD_LOG_MESSAGE_SIZE];
} SD_LogEntry_t;

static SD_LogEntry_t queue[SD_LOG_QUEUE_DEPTH];
static uint8_t head;
static uint8_t tail;
static uint8_t count;
static uint32_t dropped_count;
static uint32_t reported_dropped_count;
static uint32_t last_retry_tick;
static uint32_t last_write_tick;
static uint32_t next_sequence;
static bool online;

static void mark_offline(FRESULT reason)
{
    char message[48];
    online = false;
    SD_SPI_InvalidateCard();
    (void)f_mount(NULL, USERPath, 0);
    last_retry_tick = HAL_GetTick();
    printf("[FATFS] Logger offline (FR=%u), queued=%u\r\n",
           (unsigned int)reason, count);
    (void)snprintf(message, sizeof(message), "SD_OFFLINE fatfs_result=%u",
                   (unsigned int)reason);
    (void)SD_Logger_Enqueue(message);
}

static FRESULT append_entry(const SD_LogEntry_t *entry, UINT *bytes_written)
{
    FIL file;
    FRESULT result = f_open(&file, "0:LOG.TXT", FA_OPEN_ALWAYS | FA_WRITE);
    if (result != FR_OK) return result;

    char line[160];
    int length = snprintf(line, sizeof(line), "[#%06lu][%lums] %s\r\n",
                          entry->sequence, entry->tick, entry->message);
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
    SD_Logger_ResetSession(storage_ready);
}

void SD_Logger_ResetSession(bool storage_ready)
{
    /* No heap is used. Clear every slot as well as all ring-buffer counters so
       a software/session reset cannot retain stale messages or drop counts. */
    memset(queue, 0, sizeof(queue));
    head = 0U;
    tail = 0U;
    count = 0U;
    dropped_count = 0U;
    reported_dropped_count = 0U;
    last_retry_tick = HAL_GetTick();
    last_write_tick = HAL_GetTick();
    next_sequence = 1U;
    online = storage_ready;
}

bool SD_Logger_Enqueue(const char *message)
{
    if (message == NULL || message[0] == '\0') return false;
    if (count >= SD_LOG_QUEUE_DEPTH) {
        ++dropped_count;
        return false;
    }

    queue[head].sequence = next_sequence++;
    queue[head].tick = HAL_GetTick();
    (void)snprintf(queue[head].message, sizeof(queue[head].message), "%s", message);
    head = (uint8_t)((head + 1U) % SD_LOG_QUEUE_DEPTH);
    ++count;
    return true;
}

void SD_Logger_BeginSession(const char *reset_reason)
{
    char message[SD_LOG_MESSAGE_SIZE];
    const char *reason = (reset_reason != NULL && reset_reason[0] != '\0')
                       ? reset_reason : "UNKNOWN";

    /* Delimit sessions without erasing LOG.TXT, so earlier security history is
       preserved even though HAL_GetTick() restarts from zero after every boot. */
    (void)SD_Logger_Enqueue("========== NEW BOOT SESSION ==========");
    (void)snprintf(message, sizeof(message), "BOOT reset_cause=%s", reason);
    (void)SD_Logger_Enqueue(message);
    (void)SD_Logger_Enqueue("FW version=2.3 fsm=7-state dual-pin log=physical-sync");
}

void SD_Logger_Process(bool allow_write, bool allow_recovery)
{
    uint32_t now = HAL_GetTick();
    if (!online) {
        if (!allow_recovery) return;
        if (!Time_HasElapsed(now, last_retry_tick, SD_RETRY_INTERVAL_MS)) return;

        last_retry_tick = now;
        SD_SPI_Result_t sd_result = SD_SPI_InitCard();
        FRESULT mount_result = (sd_result == SD_SPI_OK)
                             ? f_mount(&USERFatFS, USERPath, 1) : FR_NOT_READY;
        if (mount_result == FR_OK) {
            online = true;
            printf("[FATFS] Logger storage recovered, queued=%u\r\n", count);
            (void)SD_Logger_Enqueue("SD_ONLINE storage recovered");
        } else {
            printf("[FATFS] Logger retry failed (SD=%s, FR=%u)\r\n",
                   SD_SPI_ResultString(sd_result), (unsigned int)mount_result);
        }
        return;
    }

    if (!allow_write || count == 0U) return;
    if (!Time_HasElapsed(now, last_write_tick, SD_WRITE_INTERVAL_MS)) return;
    last_write_tick = now;

    UINT bytes_written;
    uint32_t written_sequence = queue[tail].sequence;
    FRESULT result = append_entry(&queue[tail], &bytes_written);
    if (result != FR_OK) {
        mark_offline(result);
        return;
    }

    tail = (uint8_t)((tail + 1U) % SD_LOG_QUEUE_DEPTH);
    --count;
    printf("[FATFS] Physical sync LOG.TXT: OK (seq=%lu, %u bytes), queued=%u\r\n",
           written_sequence, bytes_written, count);

    if (dropped_count != reported_dropped_count && count < SD_LOG_QUEUE_DEPTH)
    {
        char message[48];
        (void)snprintf(message, sizeof(message), "LOGGER dropped_total=%lu",
                       dropped_count);
        reported_dropped_count = dropped_count;
        (void)SD_Logger_Enqueue(message);
    }
}

bool SD_Logger_IsOnline(void) { return online; }
uint32_t SD_Logger_GetDroppedCount(void) { return dropped_count; }
uint8_t SD_Logger_GetQueuedCount(void) { return count; }
