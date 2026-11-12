#ifndef MODE_KEY_H
#define MODE_KEY_H

#include <stdint.h>

/* PB1 上拉输入，按键另一端接 GND；先于 AppTick_Init 初始化。 */
void ModeKey_Init(void);
void ModeKey_Scan1ms(void);
uint8_t ModeKey_TakePress(void);

#endif
