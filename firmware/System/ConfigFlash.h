#ifndef CONFIG_FLASH_H
#define CONFIG_FLASH_H

#include <stdint.h>

/* STM32F103C8 最后两页；链接区必须截止到 0x0800F800。 */
#define CONFIG_FLASH_PAGE_SIZE 1024UL
#define CONFIG_FLASH_PAGE_A 0x0800F800UL
#define CONFIG_FLASH_PAGE_B 0x0800FC00UL
#define CONFIG_FLASH_END 0x08010000UL

uint8_t ConfigFlash_IsSupported(void);
uint8_t ConfigFlash_Read(uint32_t address, uint8_t *data, uint16_t length);
uint8_t ConfigFlash_ErasePage(uint32_t address);
uint8_t ConfigFlash_ProgramHalfWord(uint32_t address, uint16_t value);

#endif
