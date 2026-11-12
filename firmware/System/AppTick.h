#ifndef APP_TICK_H
#define APP_TICK_H

#include <stdint.h>

/* TIM2 提供 1ms 节拍，不占用 PWM 的 TIM3 或 Delay 的 SysTick。 */
void AppTick_Init(void);
uint32_t AppTick_Millis(void);

#endif
