#include "sd_spi.h"
#include "main.h"
#include "spi.h"

#define SD_SPI_TIMEOUT_MS       100U
#define SD_SPI_RESPONSE_POLLS   16U
#define SD_CMD0                 0U
#define SD_CMD8                 8U

static HAL_StatusTypeDef SD_SPI_TransferByte(uint8_t tx, uint8_t *rx)
{
    return HAL_SPI_TransmitReceive(&hspi1, &tx, rx, 1U, SD_SPI_TIMEOUT_MS);
}

static HAL_StatusTypeDef SD_SPI_SendIdleClocks(void)
{
    uint8_t rx;

    HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_SET);
    for (uint8_t i = 0U; i < 10U; ++i)
    {
        if (SD_SPI_TransferByte(0xFFU, &rx) != HAL_OK)
        {
            return HAL_ERROR;
        }
    }

    return HAL_OK;
}

static HAL_StatusTypeDef SD_SPI_SendCommand(uint8_t command,
                                            uint32_t argument,
                                            uint8_t crc,
                                            uint8_t *r1)
{
    uint8_t packet[6] = {
        (uint8_t)(0x40U | command),
        (uint8_t)(argument >> 24),
        (uint8_t)(argument >> 16),
        (uint8_t)(argument >> 8),
        (uint8_t)argument,
        crc
    };
    uint8_t rx;

    HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_RESET);

    if (SD_SPI_TransferByte(0xFFU, &rx) != HAL_OK ||
        HAL_SPI_Transmit(&hspi1, packet, sizeof(packet), SD_SPI_TIMEOUT_MS) != HAL_OK)
    {
        HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_SET);
        return HAL_ERROR;
    }

    for (uint8_t i = 0U; i < SD_SPI_RESPONSE_POLLS; ++i)
    {
        if (SD_SPI_TransferByte(0xFFU, &rx) != HAL_OK)
        {
            HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_SET);
            return HAL_ERROR;
        }

        if ((rx & 0x80U) == 0U)
        {
            *r1 = rx;
            return HAL_OK;
        }
    }

    *r1 = 0xFFU;
    return HAL_TIMEOUT;
}

static void SD_SPI_EndCommand(void)
{
    uint8_t rx;

    HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_SET);
    (void)SD_SPI_TransferByte(0xFFU, &rx);
}

SD_SPI_ProbeResult_t SD_SPI_Probe(void)
{
    SD_SPI_ProbeResult_t result = {
        .status = SD_SPI_PROBE_NO_RESPONSE,
        .cmd0_r1 = 0xFFU,
        .cmd8_r1 = 0xFFU,
        .cmd8_data = {0xFFU, 0xFFU, 0xFFU, 0xFFU}
    };

    HAL_Delay(2U);
    if (SD_SPI_SendIdleClocks() != HAL_OK)
    {
        result.status = SD_SPI_PROBE_SPI_ERROR;
        return result;
    }

    HAL_StatusTypeDef hal_status = SD_SPI_SendCommand(SD_CMD0, 0U, 0x95U,
                                                       &result.cmd0_r1);
    SD_SPI_EndCommand();
    if (hal_status == HAL_TIMEOUT)
    {
        result.status = SD_SPI_PROBE_NO_RESPONSE;
        return result;
    }
    if (hal_status != HAL_OK)
    {
        result.status = SD_SPI_PROBE_SPI_ERROR;
        return result;
    }
    if (result.cmd0_r1 != 0x01U)
    {
        result.status = SD_SPI_PROBE_CMD0_REJECTED;
        return result;
    }

    hal_status = SD_SPI_SendCommand(SD_CMD8, 0x000001AAU, 0x87U,
                                    &result.cmd8_r1);
    if (hal_status != HAL_OK)
    {
        SD_SPI_EndCommand();
        result.status = (hal_status == HAL_TIMEOUT) ? SD_SPI_PROBE_NO_RESPONSE
                                                    : SD_SPI_PROBE_SPI_ERROR;
        return result;
    }

    if ((result.cmd8_r1 & 0x04U) != 0U)
    {
        SD_SPI_EndCommand();
        result.status = SD_SPI_PROBE_CMD8_REJECTED;
        return result;
    }

    for (uint8_t i = 0U; i < 4U; ++i)
    {
        if (SD_SPI_TransferByte(0xFFU, &result.cmd8_data[i]) != HAL_OK)
        {
            SD_SPI_EndCommand();
            result.status = SD_SPI_PROBE_SPI_ERROR;
            return result;
        }
    }
    SD_SPI_EndCommand();

    if (result.cmd8_r1 != 0x01U ||
        result.cmd8_data[2] != 0x01U ||
        result.cmd8_data[3] != 0xAAU)
    {
        result.status = SD_SPI_PROBE_BAD_VOLTAGE_PATTERN;
        return result;
    }

    result.status = SD_SPI_PROBE_OK;
    return result;
}

const char *SD_SPI_ProbeStatusString(SD_SPI_ProbeStatus_t status)
{
    switch (status)
    {
        case SD_SPI_PROBE_OK:                  return "SD v2 detected";
        case SD_SPI_PROBE_SPI_ERROR:           return "STM32 SPI transfer error";
        case SD_SPI_PROBE_NO_RESPONSE:         return "No SD response (check wiring/power)";
        case SD_SPI_PROBE_CMD0_REJECTED:       return "CMD0 did not enter idle state";
        case SD_SPI_PROBE_CMD8_REJECTED:       return "CMD8 unsupported (possible SD v1)";
        case SD_SPI_PROBE_BAD_VOLTAGE_PATTERN: return "CMD8 voltage/check pattern mismatch";
        default:                               return "Unknown SD probe status";
    }
}
