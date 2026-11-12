#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>
#include <stdio.h>

/* USART3：PB10 = TX，PB11 = RX，9600 / 8N1。 */
void Serial_Init(void);

void Serial_SendByte(uint8_t Byte);
void Serial_SendArray(const uint8_t *Array, uint16_t Length);
void Serial_SendString(const char *String);
/* 固定输出 1~10 位十进制数字，前面补 0，超出宽度时保留低位。 */
void Serial_SendNumber(uint32_t Number, uint8_t Length);
/* 一次最多输出 99 个字符，更长的格式化结果会截断。 */
void Serial_printf(const char *Format, ...);

/* 在主循环中先检查 Flag；返回 1 后，调用 GetRxData 取得该字节。 */
uint8_t Serial_GetRxFlag(void);
uint8_t Serial_GetRxData(void);
/* 帧错误、硬件溢出或软件缓存溢出时增加，用于拒绝残缺命令。 */
uint32_t Serial_GetRxErrorCount(void);
void Serial_DiscardRxBuffer(void);

#endif
