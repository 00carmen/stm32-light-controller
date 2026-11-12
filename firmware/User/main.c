#include "stm32f10x.h"
#include "OLED.h"
#include "Serial.h"
#include "LightSensor.h"
#include "LedPwm.h"
#include "ModeKey.h"
#include "AppTick.h"
#include "CommandProtocol.h"
#include "AppConfig.h"
#include "ConfigStore.h"
#include "ConfigFlash.h"

#define SAMPLE_PERIOD_MS 50U
#define DISPLAY_PERIOD_MS 200U
#define REPORT_PERIOD_MS 500U

typedef enum { MODE_AUTO = 0, MODE_LOCAL, MODE_REMOTE } ControlMode;

typedef struct
{
    ControlMode mode;
    uint16_t rawLight;
    uint16_t filteredLight;
    uint16_t potRaw;
    uint16_t millivolts;
    uint8_t lightPercent;
    uint8_t potPercent;
    uint8_t lightValid;
    uint8_t potValid;
    uint8_t target;
    uint8_t output;
    uint8_t remoteTarget;
} AppState;

static AppState app;
static uint8_t adcReady;
static uint8_t streamEnabled = 1;
static uint32_t rxErrorSeen;
static CommandParser commandParser;
static AppConfig config;
static uint8_t rampTimingReset;
static AppConfig savedConfig;
static uint8_t hasSavedConfig;
static uint32_t savedSequence;

static char *ModeName(void)
{
    if (app.mode == MODE_AUTO) return "AUTO";
    if (app.mode == MODE_LOCAL) return "LOCAL";
    return "REMOTE";
}

static void SampleInputs(void)
{
    app.potValid = adcReady && LightSensor_ReadPotentiometer(&app.potRaw);
    if (app.potValid)
        app.potPercent = (uint8_t)(((uint32_t)app.potRaw * 100U + 2047U) / 4095U);

    app.lightValid = adcReady && LightSensor_Read(&app.rawLight);
    if (app.lightValid)
    {
        app.filteredLight = LightSensor_Filter(app.rawLight);
        app.lightPercent = AppConfig_LightPercent(&config, app.filteredLight);
        app.millivolts = (uint16_t)(((uint32_t)app.filteredLight * 3300U + 2047U) / 4095U);
    }
    else
    {
        LightSensor_ResetFilter();
    }
}

static uint8_t SelectedInputValid(void)
{
    if (app.mode == MODE_REMOTE) return 1;
    return (app.mode == MODE_AUTO) ? app.lightValid : app.potValid;
}

static void UpdateTarget(void)
{
    if (!SelectedInputValid())
    {
        app.target = 0;
        app.output = 0;
        LedPwm_SetDuty(0);
    }
    else
    {
        if (app.mode == MODE_REMOTE) app.target = app.remoteTarget;
        else app.target = (app.mode == MODE_AUTO) ? (uint8_t)(100U - app.lightPercent) : app.potPercent;
    }
}

static void MovePwmTowards(uint32_t steps)
{
    uint8_t difference;
    if (steps > 100U) steps = 100U;
    if (app.output < app.target)
    {
        difference = app.target - app.output;
        app.output += (uint8_t)((steps > difference) ? difference : steps);
    }
    else
    {
        difference = app.output - app.target;
        app.output -= (uint8_t)((steps > difference) ? difference : steps);
    }
    LedPwm_SetDuty(app.output);
}

static void RenderStatus(void)
{
    OLED_ShowString(1, 6, "      ");
    OLED_ShowString(1, 6, ModeName());
    if (app.lightValid)
    {
        OLED_ShowNum(2, 3, app.rawLight, 4);
        OLED_ShowNum(2, 10, app.filteredLight, 4);
        OLED_ShowNum(3, 3, app.lightPercent, 3);
    }
    else
    {
        OLED_ShowString(2, 3, "ERR ");
        OLED_ShowString(2, 10, "----");
        OLED_ShowString(3, 3, "---");
    }
    OLED_ShowNum(3, 10, app.target, 3);
    if (app.potValid) OLED_ShowNum(4, 3, app.potPercent, 3);
    else OLED_ShowString(4, 3, "---");
    OLED_ShowNum(4, 12, app.output, 3);
}

static void ReportStatus(void)
{
    /* 分段格式化，避免超过 Serial_printf 的 100 字节缓存。 */
    Serial_printf("STATUS,Mode=%s,", ModeName());
    if (app.lightValid)
        Serial_printf("ADC=%u,AVG=%u,mV=%u,Light=%u%%,",
                      (unsigned)app.rawLight, (unsigned)app.filteredLight,
                      (unsigned)app.millivolts, (unsigned)app.lightPercent);
    else Serial_SendString("ADC=ERR,AVG=ERR,mV=ERR,Light=ERR,");
    Serial_printf("Target=%u%%,PWM=%u%%,", (unsigned)app.target, (unsigned)app.output);
    if (app.potValid)
        Serial_printf("Pot=%u,Knob=%u%%,", (unsigned)app.potRaw, (unsigned)app.potPercent);
    else Serial_SendString("Pot=ERR,Knob=ERR,");
    Serial_printf("Remote=%u%%,RxErr=%lu\r\n", (unsigned)app.remoteTarget,
                  (unsigned long)Serial_GetRxErrorCount());
}

static void ReportConfig(void)
{
    uint8_t dirty = !hasSavedConfig || !AppConfig_Equals(&config, &savedConfig);
    Serial_printf("CONFIG,Dark=%u,Bright=%u,RampMs=%u,Storage=%s,",
                  (unsigned)config.dark, (unsigned)config.bright, (unsigned)config.rampMs,
                  ConfigFlash_IsSupported() ? "FLASH" : "UNAVAILABLE");
    Serial_printf("Saved=%u,Dirty=%u,Seq=%lu\r\n", (unsigned)hasSavedConfig,
                  (unsigned)dirty, (unsigned long)savedSequence);
}

static void RefreshCalibration(void)
{
    LightSensor_ResetFilter();
    SampleInputs();
    UpdateTarget();
}

static void SetMode(ControlMode mode)
{
    /* 首次进入 REMOTE 时接住当前输出，避免未发亮度命令就突然变化。 */
    if (mode == MODE_REMOTE && app.mode != MODE_REMOTE) app.remoteTarget = app.output;
    app.mode = mode;
    UpdateTarget();
}

static uint8_t ExecuteCommand(CommandResult command)
{
    ConfigStoreResult storeResult;
    AppConfig loadedConfig;
    uint32_t loadedSequence;
    switch (command.type)
    {
    case COMMAND_NONE:
        break;
    case COMMAND_GET:
        ReportStatus();
        break;
    case COMMAND_HELP:
        Serial_SendString("OK HELP: GET; CONFIG; MODE AUTO|LOCAL|REMOTE; PWM 0..100; STREAM ON|OFF; CAL dark bright; RAMP ms; DEFAULTS; SAVE; LOAD\r\n");
        break;
    case COMMAND_CONFIG:
        ReportConfig();
        break;
    case COMMAND_SAVE:
        storeResult = ConfigStore_Save(&config, &loadedSequence);
        if (storeResult == CONFIG_STORE_OK || storeResult == CONFIG_STORE_UNCHANGED)
        {
            savedConfig = config;
            savedSequence = loadedSequence;
            hasSavedConfig = 1;
            Serial_SendString(storeResult == CONFIG_STORE_UNCHANGED ? "OK SAVE UNCHANGED\r\n" : "OK SAVE\r\n");
            ReportConfig();
        }
        else if (storeResult == CONFIG_STORE_UNSUPPORTED)
            Serial_SendString("ERR FLASH_UNSUPPORTED\r\n");
        else Serial_SendString("ERR FLASH_SAVE\r\n");
        break;
    case COMMAND_LOAD:
        storeResult = ConfigStore_Load(&loadedConfig, &loadedSequence);
        if (storeResult == CONFIG_STORE_OK)
        {
            config = loadedConfig;
            savedConfig = loadedConfig;
            savedSequence = loadedSequence;
            hasSavedConfig = 1;
            RefreshCalibration();
            rampTimingReset = 1;
            Serial_SendString("OK LOAD\r\n");
            ReportConfig();
            return 1;
        }
        if (storeResult == CONFIG_STORE_NO_DATA) Serial_SendString("ERR NO_SAVED_CONFIG\r\n");
        else if (storeResult == CONFIG_STORE_UNSUPPORTED) Serial_SendString("ERR FLASH_UNSUPPORTED\r\n");
        else Serial_SendString("ERR FLASH_LOAD\r\n");
        break;
    case COMMAND_CAL:
        if (!AppConfig_SetCalibration(&config, command.value, command.value2))
        {
            Serial_SendString("ERR CALIBRATION\r\n");
            break;
        }
        RefreshCalibration();
        Serial_printf("OK CAL %u %u\r\n", (unsigned)config.dark, (unsigned)config.bright);
        return 1;
    case COMMAND_RAMP:
        if (!AppConfig_SetRamp(&config, command.value))
        {
            Serial_SendString("ERR RAMP_10_200\r\n");
            break;
        }
        /* 换速时重置时间基准，不拿旧速度下积累的时间补跳。 */
        rampTimingReset = 1;
        Serial_printf("OK RAMP %u\r\n", (unsigned)config.rampMs);
        return 1;
    case COMMAND_DEFAULTS:
        AppConfig_Defaults(&config);
        RefreshCalibration();
        rampTimingReset = 1;
        Serial_SendString("OK DEFAULTS\r\n");
        ReportConfig();
        return 1;
    case COMMAND_MODE:
        if (command.value == COMMAND_MODE_AUTO) SetMode(MODE_AUTO);
        else if (command.value == COMMAND_MODE_LOCAL) SetMode(MODE_LOCAL);
        else SetMode(MODE_REMOTE);
        Serial_printf("OK MODE %s\r\n", ModeName());
        return 1;
    case COMMAND_PWM:
        if (app.mode != MODE_REMOTE)
        {
            Serial_SendString("ERR MODE_USE_REMOTE\r\n");
            break;
        }
        app.remoteTarget = (uint8_t)command.value;
        UpdateTarget();
        Serial_printf("OK PWM %u\r\n", (unsigned)command.value);
        return 1;
    case COMMAND_STREAM:
        streamEnabled = (uint8_t)command.value;
        Serial_printf("OK STREAM %s\r\n", streamEnabled ? "ON" : "OFF");
        break;
    case COMMAND_ERR_UNKNOWN:
        Serial_SendString("ERR COMMAND\r\n");
        break;
    case COMMAND_ERR_ARGUMENTS:
        Serial_SendString("ERR ARGUMENTS\r\n");
        break;
    case COMMAND_ERR_RANGE:
        Serial_SendString("ERR RANGE_0_100\r\n");
        break;
    case COMMAND_ERR_CALIBRATION:
        Serial_SendString("ERR CALIBRATION\r\n");
        break;
    case COMMAND_ERR_RAMP:
        Serial_SendString("ERR RAMP_10_200\r\n");
        break;
    case COMMAND_ERR_TOO_LONG:
        Serial_SendString("ERR LINE_TOO_LONG\r\n");
        break;
    case COMMAND_ERR_CHARACTER:
        Serial_SendString("ERR CHARACTER\r\n");
        break;
    case COMMAND_ERR_INCOMPLETE:
        Serial_SendString("ERR INCOMPLETE_NEED_NEWLINE\r\n");
        break;
    default:
        break;
    }
    return 0;
}

static uint8_t CheckRxLoss(uint32_t now)
{
    uint32_t errors = Serial_GetRxErrorCount();
    if (errors == rxErrorSeen) return 0;
    rxErrorSeen = errors;
    Serial_DiscardRxBuffer();
    CommandProtocol_Discard(&commandParser, now);
    Serial_SendString("ERR RX_LOSS_RESEND_LINE\r\n");
    return 1;
}

static uint8_t ProcessSerialCommands(uint32_t now)
{
    uint8_t count;
    uint8_t changed = 0;
    uint8_t byte;
    CommandResult result;
    if (CheckRxLoss(now)) return 0;
    ExecuteCommand(CommandProtocol_PollTimeout(&commandParser, now));
    for (count = 0; count < 32U; ++count)
    {
        if (CheckRxLoss(now)) break;
        if (!Serial_GetRxFlag()) break;
        byte = Serial_GetRxData();
        result = CommandProtocol_Feed(&commandParser, byte, now);
        /* 若取字节过程中发现丢失，则不执行刚拼出的命令。 */
        if (CheckRxLoss(now)) break;
        changed |= ExecuteCommand(result);
    }
    return changed;
}

int main(void)
{
    uint32_t now;
    uint32_t lastSample;
    uint32_t lastDisplay;
    uint32_t lastReport;
    uint32_t lastRamp;
    uint32_t rampSteps;
    uint8_t keyEvents;
    uint8_t count;

    OLED_Init();
    OLED_ShowString(1, 1, "MODE:AUTO   V1.8");
    OLED_ShowString(2, 1, "R:0000 A:0000");
    OLED_ShowString(3, 1, "L:000% T:000%");
    OLED_ShowString(4, 1, "P:000% PWM:000%");
    Serial_Init();
    LedPwm_Init();
    AppConfig_Defaults(&config);
    if (ConfigStore_Load(&savedConfig, &savedSequence) == CONFIG_STORE_OK)
    {
        config = savedConfig;
        hasSavedConfig = 1;
    }
    adcReady = LightSensor_Init();
    ModeKey_Init();
    CommandProtocol_Init(&commandParser);
    rxErrorSeen = Serial_GetRxErrorCount();
    Serial_SendString("LIGHT CONTROL V1.8; boot=AUTO; text commands require newline\r\n");
    Serial_SendString("HELP lists commands; PB1 exits REMOTE to AUTO\r\n");
    Serial_printf("BOOT CONFIG=%s; SAVE persists calibration/ramp only\r\n",
                  hasSavedConfig ? "FLASH" : "DEFAULTS");
    ReportConfig();
    SampleInputs();
    UpdateTarget();
    RenderStatus();
    /* 开启扫描前完成初始化；启动时按住按键不产生切换事件。 */
    AppTick_Init();
    lastSample = lastDisplay = lastReport = lastRamp = AppTick_Millis();

    while (1)
    {
        now = AppTick_Millis();
        keyEvents = ProcessSerialCommands(now);
        now = AppTick_Millis();
        if (rampTimingReset)
        {
            lastRamp = now;
            rampTimingReset = 0;
        }
        for (count = 0; count < 8U && ModeKey_TakePress(); ++count)
        {
            SetMode((app.mode == MODE_AUTO) ? MODE_LOCAL : MODE_AUTO);
            Serial_printf("EVENT MODE=%s\r\n", ModeName());
            keyEvents = 1;
        }
        if ((uint32_t)(now - lastSample) >= SAMPLE_PERIOD_MS)
        {
            lastSample = now;
            SampleInputs();
        }
        UpdateTarget();

        rampSteps = (uint32_t)(now - lastRamp) / config.rampMs;
        if (rampSteps != 0)
        {
            lastRamp += rampSteps * config.rampMs;
            MovePwmTowards(rampSteps);
        }
        if (keyEvents || (uint32_t)(now - lastDisplay) >= DISPLAY_PERIOD_MS)
        {
            lastDisplay = now;
            RenderStatus();
        }
        if (streamEnabled && (keyEvents || (uint32_t)(now - lastReport) >= REPORT_PERIOD_MS))
        {
            lastReport = now;
            ReportStatus();
        }
    }
}
