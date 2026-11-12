#include "Serial.h"
#include "stm32f10x.h"
#include <stdarg.h>

/* 中断写入，主循环取出；容纳同步上报期间到达的短命令。 */
#define SERIAL_RX_BUFFER_SIZE 256U

static volatile uint8_t Serial_RxBuffer[SERIAL_RX_BUFFER_SIZE];
static volatile uint16_t Serial_RxWriteIndex;
static volatile uint16_t Serial_RxReadIndex;
static uint8_t Serial_RxData;
static volatile uint32_t Serial_RxErrorCount;

void Serial_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    /* 1. USART3 使用 APB1；GPIOB 和 AFIO 使用 APB2。 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB |
                          RCC_APB2Periph_AFIO, ENABLE);
    USART_DeInit(USART3);
    GPIO_PinRemapConfig(GPIO_PartialRemap_USART3, DISABLE);
    GPIO_PinRemapConfig(GPIO_FullRemap_USART3, DISABLE);

    /* 2. PB10 发送，PB11 接收；不要同时初始化占用 PB11 的按键。 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /* 3. 串口参数：9600 波特率、8 位数据、无校验、1 位停止位。 */
    USART_StructInit(&USART_InitStructure);
    USART_InitStructure.USART_BaudRate = 9600;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART3, &USART_InitStructure);

    /* 4. 清空接收状态，配置中断优先级。 */
    Serial_RxWriteIndex = 0;
    Serial_RxReadIndex = 0;
    Serial_RxData = 0;
    Serial_RxErrorCount = 0;

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 5. 开启接收中断和串口。 */
    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART3, ENABLE);
}

void Serial_SendByte(uint8_t Byte)
{
    /* 先等发送寄存器空，再写入下一个字节。 */
    while (USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET)
    {
    }
    USART_SendData(USART3, Byte);
}

void Serial_SendArray(const uint8_t *Array, uint16_t Length)
{
    uint16_t i;

    for (i = 0; i < Length; i++)
    {
        Serial_SendByte(Array[i]);
    }
}

void Serial_SendString(const char *String)
{
    while (*String != '\0')
    {
        Serial_SendByte((uint8_t)*String);
        String++;
    }
}

void Serial_SendNumber(uint32_t Number, uint8_t Length)
{
    uint32_t Divisor = 1;
    uint8_t i;

    /* uint32_t 最多 10 位十进制数字，Length 的有效范围为 1~10。 */
    if (Length == 0 || Length > 10)
    {
        return;
    }

    for (i = 1; i < Length; i++)
    {
        Divisor *= 10;
    }
    for (i = 0; i < Length; i++)
    {
        Serial_SendByte((uint8_t)(Number / Divisor % 10 + '0'));
        Divisor /= 10;
    }
}

/* Keil 工程启用 MicroLIB，printf 的字符通过此函数发送。 */
int fputc(int ch, FILE *Stream)
{
    (void)Stream;
    Serial_SendByte((uint8_t)ch);
    return ch;
}

void Serial_printf(const char *Format, ...)
{
    char String[100];
    va_list Args;
    int Length;

    va_start(Args, Format);
    Length = vsnprintf(String, sizeof(String), Format, Args);
    va_end(Args);

    if (Length > 0)
    {
        Serial_SendString(String);
    }
}

/* 有数据时取出一个字节，并返回 1；没有数据时返回 0。 */
uint8_t Serial_GetRxFlag(void)
{
    uint16_t ReadIndex = Serial_RxReadIndex;

    if (ReadIndex == Serial_RxWriteIndex)
    {
        return 0;
    }

    Serial_RxData = Serial_RxBuffer[ReadIndex];
    ReadIndex++;
    if (ReadIndex == SERIAL_RX_BUFFER_SIZE)
    {
        ReadIndex = 0;
    }
    Serial_RxReadIndex = ReadIndex;
    return 1;
}

uint8_t Serial_GetRxData(void)
{
    return Serial_RxData;
}

uint32_t Serial_GetRxErrorCount(void)
{
    return Serial_RxErrorCount;
}

void Serial_DiscardRxBuffer(void)
{
    Serial_RxReadIndex = Serial_RxWriteIndex;
}

void USART3_IRQHandler(void)
{
    uint32_t Status = USART3->SR;
    uint8_t Data;
    uint16_t NextIndex;

    if ((Status & (USART_FLAG_RXNE | USART_FLAG_ORE |
                   USART_FLAG_FE | USART_FLAG_NE | USART_FLAG_PE)) != 0)
    {
        /* 依次读 SR、DR 会清除接收和错误标志，不再手动清 RXNE。 */
        Data = (uint8_t)USART_ReceiveData(USART3);

        if ((Status & (USART_FLAG_ORE | USART_FLAG_FE | USART_FLAG_NE | USART_FLAG_PE)) != 0)
        {
            ++Serial_RxErrorCount;
            return;
        }
        if ((Status & USART_FLAG_RXNE) != 0)
        {
            NextIndex = Serial_RxWriteIndex + 1;
            if (NextIndex == SERIAL_RX_BUFFER_SIZE)
            {
                NextIndex = 0;
            }

            /* 缓存满时舍弃新字节，保留已经收到的数据。 */
            if (NextIndex != Serial_RxReadIndex)
            {
                Serial_RxBuffer[Serial_RxWriteIndex] = Data;
                Serial_RxWriteIndex = NextIndex;
            }
            else
            {
                ++Serial_RxErrorCount;
            }
        }
    }
}
