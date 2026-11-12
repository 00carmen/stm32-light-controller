#include "ConfigStore.h"
#include "ConfigFlash.h"
#include <string.h>

#define RECORD_SIZE 24U
#define RECORD_MAGIC 0x4C434647UL
#define RECORD_VERSION 1U

typedef struct
{
    AppConfig config;
    uint32_t sequence;
    uint8_t slot;
} StoredRecord;

static uint16_t Get16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t Get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void Put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void Put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static uint32_t Crc32(const uint8_t *data, uint16_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint16_t i;
    uint8_t bit;
    for (i = 0; i < length; ++i)
    {
        crc ^= data[i];
        for (bit = 0; bit < 8U; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
    }
    return ~crc;
}

static void Encode(uint8_t *record, const AppConfig *config, uint32_t sequence)
{
    Put32(record, RECORD_MAGIC);
    Put16(record + 4, RECORD_VERSION);
    Put16(record + 6, RECORD_SIZE);
    Put32(record + 8, sequence);
    Put16(record + 12, config->dark);
    Put16(record + 14, config->bright);
    Put16(record + 16, config->rampMs);
    Put16(record + 18, 0);
    Put32(record + 20, Crc32(record + 4, 16));
}

static uint8_t Decode(const uint8_t *record, StoredRecord *out)
{
    AppConfig candidate;
    if (Get32(record) != RECORD_MAGIC || Get16(record + 4) != RECORD_VERSION ||
        Get16(record + 6) != RECORD_SIZE || Get16(record + 18) != 0 ||
        Get32(record + 20) != Crc32(record + 4, 16)) return 0;
    candidate.dark = Get16(record + 12);
    candidate.bright = Get16(record + 14);
    candidate.rampMs = Get16(record + 16);
    if (!AppConfig_IsValid(&candidate)) return 0;
    out->config = candidate;
    out->sequence = Get32(record + 8);
    return 1;
}

static uint8_t Newer(uint32_t a, uint32_t b)
{
    uint32_t difference = a - b;
    return difference != 0 && difference < 0x80000000UL;
}

static ConfigStoreResult Latest(StoredRecord *out)
{
    uint8_t bytes[RECORD_SIZE];
    StoredRecord a;
    StoredRecord b;
    uint8_t readA = ConfigFlash_Read(CONFIG_FLASH_PAGE_A, bytes, RECORD_SIZE);
    uint8_t validA = readA && Decode(bytes, &a);
    uint8_t readB = ConfigFlash_Read(CONFIG_FLASH_PAGE_B, bytes, RECORD_SIZE);
    uint8_t validB = readB && Decode(bytes, &b);
    if (!validA && !validB)
        return (readA && readB) ? CONFIG_STORE_NO_DATA : CONFIG_STORE_IO_ERROR;
    if (validB && (!validA || Newer(b.sequence, a.sequence)))
    {
        *out = b;
        out->slot = 1;
    }
    else
    {
        *out = a;
        out->slot = 0;
    }
    return CONFIG_STORE_OK;
}

ConfigStoreResult ConfigStore_Load(AppConfig *config, uint32_t *sequence)
{
    StoredRecord record;
    ConfigStoreResult result;
    if (!ConfigFlash_IsSupported()) return CONFIG_STORE_UNSUPPORTED;
    result = Latest(&record);
    if (result != CONFIG_STORE_OK) return result;
    *config = record.config;
    if (sequence != 0) *sequence = record.sequence;
    return CONFIG_STORE_OK;
}

ConfigStoreResult ConfigStore_Save(const AppConfig *config, uint32_t *sequence)
{
    StoredRecord previous;
    StoredRecord verify;
    ConfigStoreResult result;
    uint8_t bytes[RECORD_SIZE];
    uint8_t readback[RECORD_SIZE];
    uint32_t address;
    uint32_t nextSequence;
    uint16_t offset;

    if (!AppConfig_IsValid(config)) return CONFIG_STORE_INVALID;
    if (!ConfigFlash_IsSupported()) return CONFIG_STORE_UNSUPPORTED;
    result = Latest(&previous);
    if (result == CONFIG_STORE_IO_ERROR) return result;
    if (result == CONFIG_STORE_OK && AppConfig_Equals(config, &previous.config))
    {
        if (sequence != 0) *sequence = previous.sequence;
        return CONFIG_STORE_UNCHANGED;
    }
    address = (result == CONFIG_STORE_OK && previous.slot == 0) ? CONFIG_FLASH_PAGE_B : CONFIG_FLASH_PAGE_A;
    nextSequence = (result == CONFIG_STORE_OK) ? previous.sequence + 1U : 1U;
    Encode(bytes, config, nextSequence);
    if (!ConfigFlash_ErasePage(address)) return CONFIG_STORE_IO_ERROR;
    /* 先写版本、参数与 CRC；提交标记最后写，旧页始终保留。 */
    for (offset = 4; offset < RECORD_SIZE; offset += 2)
        if (!ConfigFlash_ProgramHalfWord(address + offset, Get16(bytes + offset)))
            return CONFIG_STORE_IO_ERROR;
    if (!ConfigFlash_Read(address, readback, RECORD_SIZE) ||
        memcmp(readback + 4, bytes + 4, RECORD_SIZE - 4U) != 0) return CONFIG_STORE_IO_ERROR;
    if (!ConfigFlash_ProgramHalfWord(address, Get16(bytes)) ||
        !ConfigFlash_ProgramHalfWord(address + 2U, Get16(bytes + 2))) return CONFIG_STORE_IO_ERROR;
    if (!ConfigFlash_Read(address, readback, RECORD_SIZE) || !Decode(readback, &verify) ||
        verify.sequence != nextSequence || !AppConfig_Equals(&verify.config, config)) return CONFIG_STORE_IO_ERROR;
    if (sequence != 0) *sequence = nextSequence;
    return CONFIG_STORE_OK;
}
