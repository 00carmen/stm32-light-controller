#include "ConfigStore.h"
#include "ConfigFlash.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t memory[2U * CONFIG_FLASH_PAGE_SIZE];
static uint8_t snapshot[sizeof(memory)];
static int allowedOperations = -1;
static uint8_t supported = 1;
static uint8_t failRead;
static uint32_t corruptWriteAddress;
static unsigned erases;
static unsigned programs;
static unsigned cases;

static unsigned Offset(uint32_t address, unsigned length)
{
    assert(address >= CONFIG_FLASH_PAGE_A && address < CONFIG_FLASH_END);
    assert(length <= CONFIG_FLASH_END - address);
    return (unsigned)(address - CONFIG_FLASH_PAGE_A);
}

static uint8_t Allow(void)
{
    if (allowedOperations == 0) return 0;
    if (allowedOperations > 0) --allowedOperations;
    return 1;
}

uint8_t ConfigFlash_IsSupported(void) { return supported; }

uint8_t ConfigFlash_Read(uint32_t address, uint8_t *data, uint16_t length)
{
    unsigned offset = Offset(address, length);
    if (failRead) return 0;
    memcpy(data, memory + offset, length);
    return 1;
}

uint8_t ConfigFlash_ErasePage(uint32_t address)
{
    unsigned offset = Offset(address, CONFIG_FLASH_PAGE_SIZE);
    assert(address == CONFIG_FLASH_PAGE_A || address == CONFIG_FLASH_PAGE_B);
    if (!Allow()) return 0;
    memset(memory + offset, 0xff, CONFIG_FLASH_PAGE_SIZE);
    ++erases;
    return 1;
}

uint8_t ConfigFlash_ProgramHalfWord(uint32_t address, uint16_t value)
{
    unsigned offset = Offset(address, 2);
    uint16_t old;
    assert(!(address & 1U));
    if (!Allow()) return 0;
    old = (uint16_t)(memory[offset] | ((uint16_t)memory[offset + 1U] << 8));
    if (address == corruptWriteAddress) value ^= 1U;
    assert((old & value) == value);
    memory[offset] = (uint8_t)value;
    memory[offset + 1U] = (uint8_t)(value >> 8);
    ++programs;
    return 1;
}

static void Reset(void)
{
    memset(memory, 0xff, sizeof(memory));
    allowedOperations = -1;
    supported = 1;
    failRead = 0;
    corruptWriteAddress = 0;
    erases = programs = 0;
}

static void Loaded(const AppConfig *expected, uint32_t expectedSequence)
{
    AppConfig loaded;
    uint32_t sequence = 123;
    assert(ConfigStore_Load(&loaded, &sequence) == CONFIG_STORE_OK);
    assert(AppConfig_Equals(&loaded, expected));
    assert(sequence == expectedSequence);
}

int main(void)
{
    AppConfig base;
    AppConfig changed;
    AppConfig third;
    AppConfig loaded;
    uint32_t sequence;
    unsigned i;
    unsigned bit;
    unsigned oldErases;
    unsigned oldPrograms;
    /* Independent fixture generated using Python struct.pack and zlib.crc32. */
    const uint8_t lastSequenceRecord[24] = {
        0x47,0x46,0x43,0x4c,0x01,0x00,0x18,0x00,0xff,0xff,0xff,0xff,
        0xb4,0x0e,0xe1,0x01,0x28,0x00,0x00,0x00,0x48,0x04,0x6f,0x14
    };

    AppConfig_Defaults(&base);
    changed = base; changed.rampMs = 100;
    third = base; third.dark = 4000; third.bright = 1000;
    Reset();
    loaded = changed; sequence = 123;
    assert(ConfigStore_Load(&loaded, &sequence) == CONFIG_STORE_NO_DATA);
    assert(AppConfig_Equals(&loaded, &changed) && sequence == 123); ++cases;
    assert(ConfigStore_Save(&base, &sequence) == CONFIG_STORE_OK && sequence == 1);
    Loaded(&base, 1); ++cases;
    oldErases = erases; oldPrograms = programs;
    assert(ConfigStore_Save(&base, &sequence) == CONFIG_STORE_UNCHANGED && sequence == 1);
    assert(erases == oldErases && programs == oldPrograms); ++cases;

    memcpy(snapshot, memory, sizeof(memory));
    /* Simulate interruption before each erase/program operation of a save. */
    for (i = 0; i < 13U; ++i)
    {
        memcpy(memory, snapshot, sizeof(memory));
        allowedOperations = (int)i;
        sequence = 123;
        assert(ConfigStore_Save(&changed, &sequence) == CONFIG_STORE_IO_ERROR);
        assert(sequence == 123);
        allowedOperations = -1;
        Loaded(&base, 1);
        assert(memcmp(memory, snapshot, CONFIG_FLASH_PAGE_SIZE) == 0);
        ++cases;
    }
    memcpy(memory, snapshot, sizeof(memory));
    allowedOperations = 13;
    assert(ConfigStore_Save(&changed, &sequence) == CONFIG_STORE_OK && sequence == 2);
    allowedOperations = -1;
    Loaded(&changed, 2); ++cases;
    assert(ConfigStore_Save(&third, &sequence) == CONFIG_STORE_OK && sequence == 3);
    Loaded(&third, 3); ++cases;
    memory[20] ^= 1U;
    Loaded(&changed, 2); ++cases;
    memory[CONFIG_FLASH_PAGE_SIZE] ^= 1U;
    assert(ConfigStore_Load(&loaded, 0) == CONFIG_STORE_NO_DATA); ++cases;
    assert(ConfigStore_Save(&base, 0) == CONFIG_STORE_OK);
    Loaded(&base, 1); ++cases;

    /* Every individual bit in a record is protected by magic, metadata or CRC. */
    memcpy(snapshot, memory, sizeof(memory));
    for (i = 0; i < 24U; ++i)
        for (bit = 0; bit < 8U; ++bit)
        {
            memcpy(memory, snapshot, sizeof(memory));
            memory[i] ^= (uint8_t)(1U << bit);
            assert(ConfigStore_Load(&loaded, 0) == CONFIG_STORE_NO_DATA);
            ++cases;
        }

    /* On the first save, a partial record must never become a valid record. */
    for (i = 0; i < 13U; ++i)
    {
        Reset(); allowedOperations = (int)i;
        assert(ConfigStore_Save(&base, 0) == CONFIG_STORE_IO_ERROR);
        allowedOperations = -1;
        assert(ConfigStore_Load(&loaded, 0) == CONFIG_STORE_NO_DATA); ++cases;
    }

    Reset();
    memcpy(memory, lastSequenceRecord, sizeof(lastSequenceRecord));
    Loaded(&base, 0xffffffffU); ++cases;
    assert(ConfigStore_Save(&changed, &sequence) == CONFIG_STORE_OK && sequence == 0);
    Loaded(&changed, 0); ++cases;
    assert(ConfigStore_Save(&base, &sequence) == CONFIG_STORE_OK && sequence == 1);
    Loaded(&base, 1); ++cases;

    Reset();
    assert(ConfigStore_Save(&base, 0) == CONFIG_STORE_OK);
    corruptWriteAddress = CONFIG_FLASH_PAGE_B + 12U;
    assert(ConfigStore_Save(&changed, 0) == CONFIG_STORE_IO_ERROR);
    corruptWriteAddress = 0;
    Loaded(&base, 1); ++cases;

    oldErases = erases; oldPrograms = programs;
    loaded = base; loaded.rampMs = 0;
    assert(ConfigStore_Save(&loaded, 0) == CONFIG_STORE_INVALID);
    assert(erases == oldErases && programs == oldPrograms); ++cases;
    supported = 0;
    assert(ConfigStore_Load(&loaded, 0) == CONFIG_STORE_UNSUPPORTED);
    assert(ConfigStore_Save(&base, 0) == CONFIG_STORE_UNSUPPORTED);
    assert(erases == oldErases && programs == oldPrograms); ++cases;
    supported = 1; failRead = 1;
    assert(ConfigStore_Load(&loaded, 0) == CONFIG_STORE_IO_ERROR);
    assert(ConfigStore_Save(&base, 0) == CONFIG_STORE_IO_ERROR);
    assert(erases == oldErases && programs == oldPrograms); ++cases;

    printf("PASS: %u storage cases (interrupted writes, CRC, fallback, sequence wrap)\n", cases);
    return 0;
}
