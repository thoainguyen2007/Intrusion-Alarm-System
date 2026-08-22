#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include <stdbool.h>
#include <stdint.h>

void SD_Logger_Init(bool storage_ready);
bool SD_Logger_Enqueue(const char *message);
/* Performs blocking FatFs work only when allow_io is true (safe DISARM window). */
void SD_Logger_Process(bool allow_io);
bool SD_Logger_IsOnline(void);
uint32_t SD_Logger_GetDroppedCount(void);

#endif
