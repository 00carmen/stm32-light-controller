#include "AppConfig.h"

void AppConfig_Defaults(AppConfig *config)
{
    config->dark = CONFIG_DEFAULT_DARK;
    config->bright = CONFIG_DEFAULT_BRIGHT;
    config->rampMs = CONFIG_DEFAULT_RAMP_MS;
}

uint8_t AppConfig_ValidCalibration(uint16_t dark, uint16_t bright)
{
    return dark <= 4095U && bright < dark && (uint16_t)(dark - bright) >= CONFIG_MIN_CAL_SPAN;
}

uint8_t AppConfig_SetCalibration(AppConfig *config, uint16_t dark, uint16_t bright)
{
    if (!AppConfig_ValidCalibration(dark, bright)) return 0;
    config->dark = dark;
    config->bright = bright;
    return 1;
}

uint8_t AppConfig_IsValid(const AppConfig *config)
{
    return AppConfig_ValidCalibration(config->dark, config->bright) &&
           config->rampMs >= CONFIG_MIN_RAMP_MS && config->rampMs <= CONFIG_MAX_RAMP_MS;
}

uint8_t AppConfig_Equals(const AppConfig *a, const AppConfig *b)
{
    return a->dark == b->dark && a->bright == b->bright && a->rampMs == b->rampMs;
}

uint8_t AppConfig_SetRamp(AppConfig *config, uint16_t milliseconds)
{
    if (milliseconds < CONFIG_MIN_RAMP_MS || milliseconds > CONFIG_MAX_RAMP_MS) return 0;
    config->rampMs = milliseconds;
    return 1;
}

uint8_t AppConfig_LightPercent(const AppConfig *config, uint16_t adc)
{
    uint32_t span;
    /* 对非法参数防御，正常配置入口也会在提交前完成校验。 */
    if (!AppConfig_ValidCalibration(config->dark, config->bright)) return 0;
    if (adc >= config->dark) return 0;
    if (adc <= config->bright) return 100;
    span = (uint32_t)config->dark - config->bright;
    return (uint8_t)(((config->dark - (uint32_t)adc) * 100U + span / 2U) / span);
}
