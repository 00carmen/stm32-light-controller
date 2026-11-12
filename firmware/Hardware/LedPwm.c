#include "stm32f10x.h"
#include "LedPwm.h"

void LedPwm_Init(void)
{
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef timer;
    TIM_OCInitTypeDef output;
    RCC_ClocksTypeDef clocks;
    uint32_t timerClock;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);
    /* TIM3 default mapping puts channel 1 on PA6. */
    GPIO_PinRemapConfig(GPIO_PartialRemap_TIM3, DISABLE);
    GPIO_PinRemapConfig(GPIO_FullRemap_TIM3, DISABLE);

    /* Keep LED low while changing PA6 to alternate-function output. */
    GPIO_ResetBits(GPIOA, GPIO_Pin_6);
    gpio.GPIO_Pin = GPIO_Pin_6;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOA, &gpio);

    RCC_GetClocksFreq(&clocks);
    timerClock = clocks.PCLK1_Frequency;
    if (clocks.PCLK1_Frequency != clocks.HCLK_Frequency)
    {
        timerClock *= 2U;
    }
    TIM_DeInit(TIM3);
    TIM_TimeBaseStructInit(&timer);
    timer.TIM_Prescaler = (uint16_t)(timerClock / 1000000U - 1U);
    timer.TIM_Period = 999;
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    timer.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM3, &timer);

    TIM_OCStructInit(&output);
    output.TIM_OCMode = TIM_OCMode_PWM1;
    output.TIM_OutputState = TIM_OutputState_Enable;
    output.TIM_Pulse = 0;
    output.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC1Init(TIM3, &output);
    TIM_OC1PreloadConfig(TIM3, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM3, ENABLE);
    TIM_GenerateEvent(TIM3, TIM_EventSource_Update);
    TIM_Cmd(TIM3, ENABLE);
}

void LedPwm_SetDuty(uint8_t percent)
{
    if (percent > 100U) percent = 100U;
    /* ARR+1=1000. CCR=1000 yields a true 100% high output. */
    TIM_SetCompare1(TIM3, (uint16_t)percent * 10U);
}
