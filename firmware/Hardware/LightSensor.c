#include "stm32f10x.h"
#include "LightSensor.h"
#include "Delay.h"

/* Bounded polling prevents an ADC fault from trapping the main loop. */
#define ADC_WAIT_LIMIT 100000UL

#define LIGHT_FILTER_SIZE 4U

static uint16_t lightSamples[LIGHT_FILTER_SIZE];
static uint32_t lightSum;
static uint8_t lightIndex;
static uint8_t lightCount;

uint8_t LightSensor_Init(void)
{
    GPIO_InitTypeDef gpio;
    ADC_InitTypeDef adc;
    uint32_t remaining;

    LightSensor_ResetFilter();
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1, ENABLE);
    /* Existing system clock: 72 MHz; ADC clock: 72 / 6 = 12 MHz. */
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);
    gpio.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
    gpio.GPIO_Mode = GPIO_Mode_AIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    ADC_DeInit(ADC1);
    ADC_StructInit(&adc);
    adc.ADC_Mode = ADC_Mode_Independent;
    adc.ADC_ScanConvMode = DISABLE;
    adc.ADC_ContinuousConvMode = DISABLE;
    adc.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &adc);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 1, ADC_SampleTime_239Cycles5);
    ADC_Cmd(ADC1, ENABLE);
    Delay_ms(1);

    ADC_ResetCalibration(ADC1);
    remaining = ADC_WAIT_LIMIT;
    while (ADC_GetResetCalibrationStatus(ADC1) != RESET)
    {
        if (--remaining == 0) return 0;
    }
    ADC_StartCalibration(ADC1);
    remaining = ADC_WAIT_LIMIT;
    while (ADC_GetCalibrationStatus(ADC1) != RESET)
    {
        if (--remaining == 0) return 0;
    }
    return 1;
}

/* 两路输入共用 ADC1，每次转换前明确选择通道，防止读到上一路。 */
static uint8_t ReadAdcChannel(uint8_t channel, uint16_t *value)
{
    uint32_t remaining = ADC_WAIT_LIMIT;
    if (value == 0) return 0;
    ADC_RegularChannelConfig(ADC1, channel, 1, ADC_SampleTime_239Cycles5);
    ADC_ClearFlag(ADC1, ADC_FLAG_EOC);
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET)
    {
        if (--remaining == 0) return 0;
    }
    *value = ADC_GetConversionValue(ADC1);
    return 1;
}

uint8_t LightSensor_Read(uint16_t *value)
{
    return ReadAdcChannel(ADC_Channel_0, value);
}

uint8_t LightSensor_ReadPotentiometer(uint16_t *value)
{
    return ReadAdcChannel(ADC_Channel_1, value);
}

void LightSensor_ResetFilter(void)
{
    uint8_t i;
    for (i = 0; i < LIGHT_FILTER_SIZE; ++i)
    {
        lightSamples[i] = 0;
    }
    lightSum = 0;
    lightIndex = 0;
    lightCount = 0;
}

uint16_t LightSensor_Filter(uint16_t value)
{
    lightSum -= lightSamples[lightIndex];
    lightSamples[lightIndex] = value;
    lightSum += value;
    lightIndex = (uint8_t)((lightIndex + 1U) % LIGHT_FILTER_SIZE);
    if (lightCount < LIGHT_FILTER_SIZE) ++lightCount;
    return (uint16_t)((lightSum + lightCount / 2U) / lightCount);
}

