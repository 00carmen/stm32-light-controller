#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include "AppConfig.h"

typedef enum
{
    CONFIG_STORE_OK = 0,
    CONFIG_STORE_UNCHANGED,
    CONFIG_STORE_NO_DATA,
    CONFIG_STORE_UNSUPPORTED,
    CONFIG_STORE_INVALID,
    CONFIG_STORE_IO_ERROR
} ConfigStoreResult;

/* LOAD 失败不改动调用方的配置；sequence 可传空指针。 */
ConfigStoreResult ConfigStore_Load(AppConfig *config, uint32_t *sequence);
/* 同一有效配置重复 SAVE 不擦写 Flash。 */
ConfigStoreResult ConfigStore_Save(const AppConfig *config, uint32_t *sequence);

#endif
