#ifndef LED_PWM_H
#define LED_PWM_H

#include <stdint.h>

/* PA6 / TIM3_CH1, 1 kHz, high level lights the external LED. */
void LedPwm_Init(void);
/* Duty is clamped to 0..100; 0 is fully off, 100 fully on. */
void LedPwm_SetDuty(uint8_t percent);

#endif
