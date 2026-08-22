#include "sd_spi.h"
#include "main.h"
#include "spi.h"

#define BYTE_TIMEOUT_MS 100U
#define INIT_TIMEOUT_MS 1500U
#define READ_TIMEOUT_MS 300U
#define WRITE_TIMEOUT_MS 600U
#define RESPONSE_POLLS 16U
#define CMD0 0U
#define CMD8 8U
#define CMD16 16U
#define CMD17 17U
#define CMD24 24U
#define CMD55 55U
#define CMD58 58U
#define ACMD41 41U
#define R1_IDLE 0x01U
#define R1_ILLEGAL_CMD 0x04U
#define DATA_TOKEN 0xFEU
#define DATA_ACCEPTED 0x05U

static SD_SPI_CardInfo_t card = {false, SD_SPI_CARD_NONE, 0U, 0xFFU};

static HAL_StatusTypeDef transfer(uint8_t tx, uint8_t *rx)
{
    return HAL_SPI_TransmitReceive(&hspi1, &tx, rx, 1U, BYTE_TIMEOUT_MS);
}

static void deselect(void)
{
    uint8_t rx;
    HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_SET);
    (void)transfer(0xFFU, &rx);
}

static SD_SPI_Result_t wait_ready(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint8_t rx;
    do {
        if (transfer(0xFFU, &rx) != HAL_OK) return SD_SPI_HAL_ERROR;
        if (rx == 0xFFU) return SD_SPI_OK;
    } while ((HAL_GetTick() - start) < timeout_ms);
    return SD_SPI_BUSY_TIMEOUT;
}

static SD_SPI_Result_t select_card(void)
{
    uint8_t rx;
    HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_RESET);
    if (transfer(0xFFU, &rx) != HAL_OK) {
        deselect();
        return SD_SPI_HAL_ERROR;
    }
    SD_SPI_Result_t result = wait_ready(BYTE_TIMEOUT_MS);
    if (result != SD_SPI_OK) deselect();
    return result;
}

/* Leaves CS asserted when an R1 response is received. */
static SD_SPI_Result_t command(uint8_t cmd, uint32_t arg, uint8_t crc, uint8_t *r1)
{
    uint8_t packet[6] = {(uint8_t)(0x40U | cmd), (uint8_t)(arg >> 24),
        (uint8_t)(arg >> 16), (uint8_t)(arg >> 8), (uint8_t)arg, crc};
    uint8_t rx;
    SD_SPI_Result_t result = select_card();
    if (result != SD_SPI_OK) return result;
    if (HAL_SPI_Transmit(&hspi1, packet, sizeof(packet), BYTE_TIMEOUT_MS) != HAL_OK) {
        deselect();
        return SD_SPI_HAL_ERROR;
    }
    for (uint8_t i = 0U; i < RESPONSE_POLLS; ++i) {
        if (transfer(0xFFU, &rx) != HAL_OK) {
            deselect();
            return SD_SPI_HAL_ERROR;
        }
        if ((rx & 0x80U) == 0U) {
            *r1 = rx;
            card.last_r1 = rx;
            return SD_SPI_OK;
        }
    }
    *r1 = card.last_r1 = 0xFFU;
    deselect();
    return SD_SPI_NO_RESPONSE;
}

static SD_SPI_Result_t set_prescaler(uint32_t prescaler)
{
    HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_SET);
    if (HAL_SPI_DeInit(&hspi1) != HAL_OK) return SD_SPI_HAL_ERROR;
    hspi1.Init.BaudRatePrescaler = prescaler;
    return (HAL_SPI_Init(&hspi1) == HAL_OK) ? SD_SPI_OK : SD_SPI_HAL_ERROR;
}

static SD_SPI_Result_t idle_clocks(void)
{
    uint8_t rx;
    HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_SET);
    for (uint8_t i = 0U; i < 10U; ++i)
        if (transfer(0xFFU, &rx) != HAL_OK) return SD_SPI_HAL_ERROR;
    return SD_SPI_OK;
}

static SD_SPI_Result_t read_bytes(uint8_t *buffer, uint16_t length)
{
    for (uint16_t i = 0U; i < length; ++i)
        if (transfer(0xFFU, &buffer[i]) != HAL_OK) return SD_SPI_HAL_ERROR;
    return SD_SPI_OK;
}

static SD_SPI_Result_t wait_token(uint8_t expected, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint8_t value;
    do {
        if (transfer(0xFFU, &value) != HAL_OK) return SD_SPI_HAL_ERROR;
        if (value == expected) return SD_SPI_OK;
        if (value != 0xFFU) return SD_SPI_DATA_TOKEN_TIMEOUT;
    } while ((HAL_GetTick() - start) < timeout_ms);
    return SD_SPI_DATA_TOKEN_TIMEOUT;
}

static bool sector_address(uint32_t sector, uint32_t *address)
{
    if (card.type == SD_SPI_CARD_SDHC_SDXC) {
        *address = sector;
        return true;
    }
    if (sector > UINT32_MAX / SD_SPI_BLOCK_SIZE) return false;
    *address = sector * SD_SPI_BLOCK_SIZE;
    return true;
}

SD_SPI_Result_t SD_SPI_InitCard(void)
{
    SD_SPI_Result_t result;
    uint8_t r1 = 0xFFU, response[4];
    bool v2 = false;
    card = (SD_SPI_CardInfo_t){false, SD_SPI_CARD_NONE, 0U, 0xFFU};

    result = set_prescaler(SPI_BAUDRATEPRESCALER_256);
    if (result != SD_SPI_OK) return result;
    HAL_Delay(2U);
    result = idle_clocks();
    if (result != SD_SPI_OK) return result;

    for (uint8_t attempt = 0U; attempt < 10U; ++attempt) {
        result = command(CMD0, 0U, 0x95U, &r1);
        deselect();
        if (result == SD_SPI_OK && r1 == R1_IDLE) break;
        HAL_Delay(2U);
    }
    if (result == SD_SPI_NO_RESPONSE) return result;
    if (result != SD_SPI_OK || r1 != R1_IDLE) return SD_SPI_CMD0_ERROR;

    result = command(CMD8, 0x1AAU, 0x87U, &r1);
    if (result != SD_SPI_OK) return result;
    if ((r1 & R1_ILLEGAL_CMD) != 0U) {
        card.type = SD_SPI_CARD_SDSC_V1;
        deselect();
    } else {
        result = read_bytes(response, sizeof(response));
        deselect();
        if (result != SD_SPI_OK || r1 != R1_IDLE ||
            response[2] != 0x01U || response[3] != 0xAAU)
            return SD_SPI_CMD8_ERROR;
        v2 = true;
        card.type = SD_SPI_CARD_SDSC_V2;
    }

    uint32_t start = HAL_GetTick();
    do {
        result = command(CMD55, 0U, 0x01U, &r1);
        deselect();
        if (result != SD_SPI_OK) return result;
        result = command(ACMD41, v2 ? 0x40000000U : 0U, 0x01U, &r1);
        deselect();
        if (result != SD_SPI_OK) return result;
        if (r1 == 0U) break;
        HAL_Delay(10U);
    } while ((HAL_GetTick() - start) < INIT_TIMEOUT_MS);
    if (r1 != 0U) return SD_SPI_ACMD41_TIMEOUT;

    result = command(CMD58, 0U, 0x01U, &r1);
    if (result != SD_SPI_OK) return result;
    result = read_bytes(response, sizeof(response));
    deselect();
    if (result != SD_SPI_OK || r1 != 0U) return SD_SPI_CMD58_ERROR;
    card.ocr = ((uint32_t)response[0] << 24) | ((uint32_t)response[1] << 16) |
               ((uint32_t)response[2] << 8) | response[3];

    if (v2 && (card.ocr & 0x40000000U) != 0U) {
        card.type = SD_SPI_CARD_SDHC_SDXC;
    } else {
        result = command(CMD16, SD_SPI_BLOCK_SIZE, 0x01U, &r1);
        deselect();
        if (result != SD_SPI_OK || r1 != 0U) return SD_SPI_CMD16_ERROR;
    }

    result = set_prescaler(SPI_BAUDRATEPRESCALER_8);
    if (result != SD_SPI_OK) return result;
    card.initialized = true;
    return SD_SPI_OK;
}

SD_SPI_Result_t SD_SPI_ReadBlock(uint32_t sector, uint8_t *buffer)
{
    uint32_t address;
    uint8_t r1, crc[2];
    if (!card.initialized) return SD_SPI_NOT_INITIALIZED;
    if (buffer == NULL || !sector_address(sector, &address)) return SD_SPI_PARAMETER_ERROR;
    SD_SPI_Result_t result = command(CMD17, address, 0x01U, &r1);
    if (result != SD_SPI_OK) return result;
    if (r1 != 0U) { deselect(); return SD_SPI_DATA_TOKEN_TIMEOUT; }
    result = wait_token(DATA_TOKEN, READ_TIMEOUT_MS);
    if (result == SD_SPI_OK) result = read_bytes(buffer, SD_SPI_BLOCK_SIZE);
    if (result == SD_SPI_OK) result = read_bytes(crc, sizeof(crc));
    deselect();
    return result;
}

SD_SPI_Result_t SD_SPI_WriteBlock(uint32_t sector, const uint8_t *buffer)
{
    uint32_t address;
    uint8_t r1, rx, token = DATA_TOKEN, crc[2] = {0xFFU, 0xFFU};
    if (!card.initialized) return SD_SPI_NOT_INITIALIZED;
    if (buffer == NULL || !sector_address(sector, &address)) return SD_SPI_PARAMETER_ERROR;
    SD_SPI_Result_t result = command(CMD24, address, 0x01U, &r1);
    if (result != SD_SPI_OK) return result;
    if (r1 != 0U || transfer(0xFFU, &rx) != HAL_OK ||
        HAL_SPI_Transmit(&hspi1, &token, 1U, BYTE_TIMEOUT_MS) != HAL_OK ||
        HAL_SPI_Transmit(&hspi1, (uint8_t *)buffer, SD_SPI_BLOCK_SIZE, WRITE_TIMEOUT_MS) != HAL_OK ||
        HAL_SPI_Transmit(&hspi1, crc, sizeof(crc), BYTE_TIMEOUT_MS) != HAL_OK ||
        transfer(0xFFU, &rx) != HAL_OK) {
        deselect();
        return SD_SPI_HAL_ERROR;
    }
    if ((rx & 0x1FU) != DATA_ACCEPTED) { deselect(); return SD_SPI_WRITE_REJECTED; }
    result = wait_ready(WRITE_TIMEOUT_MS);
    deselect();
    return result;
}

SD_SPI_Result_t SD_SPI_Sync(void)
{
    if (!card.initialized) return SD_SPI_NOT_INITIALIZED;
    SD_SPI_Result_t result = select_card();
    if (result == SD_SPI_OK) result = wait_ready(WRITE_TIMEOUT_MS);
    deselect();
    return result;
}

bool SD_SPI_IsReady(void) { return card.initialized; }
const SD_SPI_CardInfo_t *SD_SPI_GetCardInfo(void) { return &card; }

const char *SD_SPI_ResultString(SD_SPI_Result_t result)
{
    switch (result) {
        case SD_SPI_OK: return "OK";
        case SD_SPI_NOT_INITIALIZED: return "Card not initialized";
        case SD_SPI_PARAMETER_ERROR: return "Invalid parameter";
        case SD_SPI_HAL_ERROR: return "STM32 SPI transfer error";
        case SD_SPI_NO_RESPONSE: return "No SD response (check wiring/power)";
        case SD_SPI_CMD0_ERROR: return "CMD0 failed";
        case SD_SPI_CMD8_ERROR: return "CMD8 pattern failed";
        case SD_SPI_ACMD41_TIMEOUT: return "ACMD41 timeout";
        case SD_SPI_CMD58_ERROR: return "CMD58 failed";
        case SD_SPI_CMD16_ERROR: return "CMD16 failed";
        case SD_SPI_DATA_TOKEN_TIMEOUT: return "Data token timeout/error";
        case SD_SPI_WRITE_REJECTED: return "Card rejected data";
        case SD_SPI_BUSY_TIMEOUT: return "Card busy timeout";
        default: return "Unknown SD error";
    }
}

const char *SD_SPI_CardTypeString(SD_SPI_CardType_t type)
{
    switch (type) {
        case SD_SPI_CARD_SDSC_V1: return "SDSC v1";
        case SD_SPI_CARD_SDSC_V2: return "SDSC v2";
        case SD_SPI_CARD_SDHC_SDXC: return "SDHC/SDXC";
        default: return "No card";
    }
}
