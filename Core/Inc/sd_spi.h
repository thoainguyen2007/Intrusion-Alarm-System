#ifndef __SD_SPI_H
#define __SD_SPI_H

#include "stm32f1xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#define SD_SPI_BLOCK_SIZE 512U

typedef enum {
    SD_SPI_OK = 0, SD_SPI_NOT_INITIALIZED, SD_SPI_PARAMETER_ERROR,
    SD_SPI_HAL_ERROR, SD_SPI_NO_RESPONSE, SD_SPI_CMD0_ERROR,
    SD_SPI_CMD8_ERROR, SD_SPI_ACMD41_TIMEOUT, SD_SPI_CMD58_ERROR,
    SD_SPI_CMD16_ERROR, SD_SPI_DATA_TOKEN_TIMEOUT,
    SD_SPI_WRITE_REJECTED, SD_SPI_BUSY_TIMEOUT, SD_SPI_STATUS_ERROR
} SD_SPI_Result_t;

typedef enum {
    SD_SPI_CARD_NONE = 0, SD_SPI_CARD_SDSC_V1,
    SD_SPI_CARD_SDSC_V2, SD_SPI_CARD_SDHC_SDXC
} SD_SPI_CardType_t;

typedef struct {
    bool initialized;
    SD_SPI_CardType_t type;
    uint32_t ocr;
    uint8_t last_r1;
} SD_SPI_CardInfo_t;

typedef struct {
    uint32_t ff_bytes;
    uint32_t zero_bytes;
    uint32_t other_bytes;
} SD_SPI_BusStats_t;

SD_SPI_Result_t SD_SPI_InitCard(void);
SD_SPI_Result_t SD_SPI_ReadBlock(uint32_t sector, uint8_t *buffer);
SD_SPI_Result_t SD_SPI_WriteBlock(uint32_t sector, const uint8_t *buffer);
SD_SPI_Result_t SD_SPI_Sync(void);
SD_SPI_Result_t SD_SPI_GetSectorCount(uint32_t *sector_count);
void SD_SPI_InvalidateCard(void);
bool SD_SPI_IsReady(void);
const SD_SPI_CardInfo_t *SD_SPI_GetCardInfo(void);
const SD_SPI_BusStats_t *SD_SPI_GetBusStats(void);
const char *SD_SPI_ResultString(SD_SPI_Result_t result);
const char *SD_SPI_CardTypeString(SD_SPI_CardType_t type);

#endif
