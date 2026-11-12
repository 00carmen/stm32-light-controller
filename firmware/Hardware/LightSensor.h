#ifndef LIGHT_SENSOR_H
#define LIGHT_SENSOR_H

#include <stdint.h>

/* ADC1 初始化 PA0 光敏输入和 PA1 电位器输入。成功返回 1，超时返回 0。 */
uint8_t LightSensor_Init(void);
uint8_t LightSensor_Read(uint16_t *value);
uint8_t LightSensor_ReadPotentiometer(uint16_t *value);

/* 最近 4 次有效采样的滑动平均；启动时仅平均已有数据。 */
void LightSensor_ResetFilter(void);
uint16_t LightSensor_Filter(uint16_t value);

#endif
