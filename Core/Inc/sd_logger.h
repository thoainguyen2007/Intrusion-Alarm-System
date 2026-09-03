#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include <stdbool.h>
#include <stdint.h>

void SD_Logger_Init(bool storage_ready);
/* Clears pending RAM records and counters; does not erase LOG.TXT on the card. */
void SD_Logger_ResetSession(bool storage_ready);
/* Adds a visible delimiter and reset cause before normal event records. */
void SD_Logger_BeginSession(const char *reset_reason);
bool SD_Logger_Enqueue(const char *message);
/* Writes at most one record after the rate limit; dequeue follows write,
   sync and close success. The caller separately permits recovery in
   DISARM, EXIT_DELAY and TEMP_DISARM. I/O remains synchronous. */
void SD_Logger_Process(bool allow_write, bool allow_recovery);
bool SD_Logger_IsOnline(void);
uint32_t SD_Logger_GetDroppedCount(void);
uint8_t SD_Logger_GetQueuedCount(void);

#endif
