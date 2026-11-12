#ifndef COMMAND_PROTOCOL_H
#define COMMAND_PROTOCOL_H

#include <stdint.h>

#define COMMAND_MAX_LENGTH 63U
#define COMMAND_TIMEOUT_MS 1000U

typedef enum
{
    COMMAND_NONE = 0,
    COMMAND_GET,
    COMMAND_HELP,
    COMMAND_MODE,
    COMMAND_PWM,
    COMMAND_STREAM,
    COMMAND_CONFIG,
    COMMAND_CAL,
    COMMAND_RAMP,
    COMMAND_DEFAULTS,
    COMMAND_SAVE,
    COMMAND_LOAD,
    COMMAND_ERR_CALIBRATION,
    COMMAND_ERR_RAMP,
    COMMAND_ERR_UNKNOWN,
    COMMAND_ERR_ARGUMENTS,
    COMMAND_ERR_RANGE,
    COMMAND_ERR_TOO_LONG,
    COMMAND_ERR_CHARACTER,
    COMMAND_ERR_INCOMPLETE
} CommandType;

typedef enum
{
    COMMAND_MODE_AUTO = 0,
    COMMAND_MODE_LOCAL,
    COMMAND_MODE_REMOTE
} CommandMode;

typedef struct
{
    CommandType type;
    uint16_t value;
    uint16_t value2;
} CommandResult;

typedef struct
{
    char line[COMMAND_MAX_LENGTH + 1U];
    uint8_t length;
    uint8_t discarding;
    uint32_t lastByteMs;
} CommandParser;

void CommandProtocol_Init(CommandParser *parser);
CommandResult CommandProtocol_Feed(CommandParser *parser, uint8_t byte, uint32_t now);
CommandResult CommandProtocol_PollTimeout(CommandParser *parser, uint32_t now);
/* 串口接收丢字节时丢弃残行，直到新换行或足够长的空闲。 */
void CommandProtocol_Discard(CommandParser *parser, uint32_t now);

#endif
