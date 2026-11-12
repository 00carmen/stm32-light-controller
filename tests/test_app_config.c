#include "AppConfig.h"
#include <assert.h>
#include <stdio.h>

static void Same(const AppConfig *a, const AppConfig *b)
{
    assert(a->dark == b->dark && a->bright == b->bright && a->rampMs == b->rampMs);
}

int main(void)
{
    AppConfig config;
    AppConfig before;
    unsigned i;
    unsigned j;
    unsigned checks = 0;
    uint8_t previous;
    uint8_t current;
    const uint16_t calibration[][2] = {{3764, 481}, {4095, 0}, {100, 0}, {4000, 1000}};

    AppConfig_Defaults(&config);
    assert(config.dark == 3764 && config.bright == 481 && config.rampMs == 40);
    ++checks;
    before = config;
    assert(!AppConfig_SetCalibration(&config, 0, 0)); Same(&config, &before); ++checks;
    assert(!AppConfig_SetCalibration(&config, 1000, 2000)); Same(&config, &before); ++checks;
    assert(!AppConfig_SetCalibration(&config, 4096, 0)); Same(&config, &before); ++checks;
    assert(!AppConfig_SetCalibration(&config, 1000, 901)); Same(&config, &before); ++checks;
    assert(!AppConfig_SetCalibration(&config, 65535, 0)); Same(&config, &before); ++checks;
    assert(!AppConfig_SetRamp(&config, 0)); Same(&config, &before); ++checks;
    assert(!AppConfig_SetRamp(&config, 9)); Same(&config, &before); ++checks;
    assert(!AppConfig_SetRamp(&config, 201)); Same(&config, &before); ++checks;
    assert(AppConfig_SetRamp(&config, 10) && config.rampMs == 10); ++checks;
    assert(AppConfig_SetRamp(&config, 200) && config.rampMs == 200); ++checks;

    for (j = 0; j < sizeof(calibration) / sizeof(calibration[0]); ++j)
    {
        assert(AppConfig_SetCalibration(&config, calibration[j][0], calibration[j][1]));
        assert(AppConfig_LightPercent(&config, config.dark) == 0);
        assert(AppConfig_LightPercent(&config, config.bright) == 100);
        previous = AppConfig_LightPercent(&config, 0);
        for (i = 1; i <= 4095U; ++i)
        {
            current = AppConfig_LightPercent(&config, (uint16_t)i);
            assert(current <= 100 && current <= previous);
            previous = current;
        }
        ++checks;
    }
    assert(AppConfig_SetCalibration(&config, 3764, 481));
    assert(AppConfig_LightPercent(&config, 2123) == 50); ++checks;
    assert(AppConfig_LightPercent(&config, 4095) == 0); ++checks;
    assert(AppConfig_LightPercent(&config, 0) == 100); ++checks;
    config.dark = config.bright;
    assert(AppConfig_LightPercent(&config, 1000) == 0); ++checks;
    AppConfig_Defaults(&config);
    assert(config.dark == 3764 && config.bright == 481 && config.rampMs == 40); ++checks;

    printf("PASS: %u config checks, including four full ADC-range sweeps\n", checks);
    return 0;
}
