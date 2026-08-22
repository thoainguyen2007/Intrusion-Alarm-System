#ifndef __SD_SPI_H
#define __SD_SPI_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

typedef enum
{
    SD_SPI_PROBE_OK = 0,
    SD_SPI_PROBE_SPI_ERROR,
    SD_SPI_PROBE_NO_RESPONSE,
    SD_SPI_PROBE_CMD0_REJECTED,
    SD_SPI_PROBE_CMD8_REJECTED,
    SD_SPI_PROBE_BAD_VOLTAGE_PATTERN
} SD_SPI_ProbeStatus_t;

typedef struct
{
    SD_SPI_ProbeStatus_t status;
    uint8_t cmd0_r1;
    uint8_t cmd8_r1;
    uint8_t cmd8_data[4];
} SD_SPI_ProbeResult_t;

SD_SPI_ProbeResult_t SD_SPI_Probe(void);
const char *SD_SPI_ProbeStatusString(SD_SPI_ProbeStatus_t status);

#endif /* __SD_SPI_H */
