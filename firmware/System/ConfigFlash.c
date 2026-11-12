#include "stm32f10x.h"
#include "ConfigFlash.h"

static uint8_t ValidRange(uint32_t address, uint16_t length)
{
    return address >= CONFIG_FLASH_PAGE_A && address < CONFIG_FLASH_END &&
           length <= CONFIG_FLASH_END - address;
}

uint8_t ConfigFlash_IsSupported(void)
{
    uint16_t sizeKB = *(volatile const uint16_t *)0x1FFFF7E0UL;
    uint32_t device = *(volatile const uint32_t *)0xE0042000UL;
    /* 仅支持已核对的中容量 F103（1KB/页），不猜测其他器件布局。 */
    return (device & 0xFFFU) == 0x410U && (sizeKB == 64U || sizeKB == 128U);
}

uint8_t ConfigFlash_Read(uint32_t address, uint8_t *data, uint16_t length)
{
    uint16_t i;
    if (!ConfigFlash_IsSupported() || !ValidRange(address, length)) return 0;
    for (i = 0; i < length; ++i) data[i] = *(volatile const uint8_t *)(address + i);
    return 1;
}

uint8_t ConfigFlash_ErasePage(uint32_t address)
{
    FLASH_Status status;
    if (!ConfigFlash_IsSupported() ||
        (address != CONFIG_FLASH_PAGE_A && address != CONFIG_FLASH_PAGE_B)) return 0;
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
    status = FLASH_ErasePage(address);
    FLASH_Lock();
    return status == FLASH_COMPLETE;
}

uint8_t ConfigFlash_ProgramHalfWord(uint32_t address, uint16_t value)
{
    FLASH_Status status;
    if (!ConfigFlash_IsSupported() || !ValidRange(address, 2) || (address & 1U)) return 0;
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
    status = FLASH_ProgramHalfWord(address, value);
    FLASH_Lock();
    return status == FLASH_COMPLETE;
}
