#include "stm32f10x.h"
#include "ModeKey.h"

#define KEY_DEBOUNCE_MS 20U
static uint8_t candidatePressed;
static uint8_t stablePressed;
static uint8_t stableSamples;
/* 中断只写 produced，主循环只写 consumed，取事件时无需关闭中断。 */
static volatile uint32_t produced;
static uint32_t consumed;

void ModeKey_Init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    gpio.GPIO_Pin = GPIO_Pin_1;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOB, &gpio);
    stablePressed = (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_1) == Bit_RESET);
    candidatePressed = stablePressed;
    stableSamples = 0;
    produced = 0;
    consumed = 0;
}

void ModeKey_Scan1ms(void)
{
    uint8_t pressed = (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_1) == Bit_RESET);
    if (pressed != candidatePressed)
    {
        candidatePressed = pressed;
        stableSamples = 1;
    }
    else if (stableSamples < KEY_DEBOUNCE_MS) ++stableSamples;
    if (stableSamples >= KEY_DEBOUNCE_MS && stablePressed != candidatePressed)
    {
        stablePressed = candidatePressed;
        if (stablePressed != 0) ++produced;
    }
}

uint8_t ModeKey_TakePress(void)
{
    if (consumed == produced) return 0;
    ++consumed;
    return 1;
}
