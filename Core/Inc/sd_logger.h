#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include <stdbool.h>
#include <stdint.h>

void SD_Logger_Init(bool storage_ready);
/* Starts a fresh RAM logging session: clears queue indexes and counters. */
void SD_Logger_ResetSession(bool storage_ready);
/* Adds a visible delimiter and reset cause before normal event records. */
void SD_Logger_BeginSession(const char *reset_reason);
bool SD_Logger_Enqueue(const char *message);
/* Performs blocking FatFs work only in an authenticated safe window. */
void SD_Logger_Process(bool allow_io);
bool SD_Logger_IsOnline(void);
uint32_t SD_Logger_GetDroppedCount(void);

#endif
