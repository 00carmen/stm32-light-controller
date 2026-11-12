#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>

#define CONFIG_DEFAULT_DARK 3764U
#define CONFIG_DEFAULT_BRIGHT 481U
#define CONFIG_DEFAULT_RAMP_MS 40U
#define CONFIG_MIN_CAL_SPAN 100U
#define CONFIG_MIN_RAMP_MS 10U
#define CONFIG_MAX_RAMP_MS 200U

typedef struct
{
    uint16_t dark;
    uint16_t bright;
    uint16_t rampMs;
} AppConfig;

void AppConfig_Defaults(AppConfig *config);
uint8_t AppConfig_ValidCalibration(uint16_t dark, uint16_t bright);
uint8_t AppConfig_IsValid(const AppConfig *config);
uint8_t AppConfig_Equals(const AppConfig *a, const AppConfig *b);
/* 校验成功才修改参数；失败时保留原值。 */
uint8_t AppConfig_SetCalibration(AppConfig *config, uint16_t dark, uint16_t bright);
uint8_t AppConfig_SetRamp(AppConfig *config, uint16_t milliseconds);
uint8_t AppConfig_LightPercent(const AppConfig *config, uint16_t adc);

#endif
