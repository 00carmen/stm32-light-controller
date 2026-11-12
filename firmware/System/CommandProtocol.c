#include "CommandProtocol.h"
#include <string.h>

static CommandResult Result(CommandType type, uint16_t value)
{
    CommandResult result;
    result.type = type;
    result.value = value;
    result.value2 = 0;
    return result;
}

/* 0=成功，1=格式错误，2=超范围；每位都检查，避免长数字溢出。 */
static uint8_t ParseUnsigned(char *text, uint16_t maximum, uint16_t *value)
{
    uint32_t number = 0;
    if (*text == '\0') return 1;
    while (*text != '\0')
    {
        if (*text < '0' || *text > '9') return 1;
        number = number * 10U + (uint32_t)(*text++ - '0');
        if (number > maximum) return 2;
    }
    *value = (uint16_t)number;
    return 0;
}

static CommandResult ParseLine(char *line)
{
    char *tokens[3];
    char *cursor = line;
    uint8_t count = 0;
    uint16_t number = 0;
    uint16_t second = 0;
    uint8_t numericStatus;
    CommandResult result;

    while (*cursor != '\0')
    {
        while (*cursor == ' ') ++cursor;
        if (*cursor == '\0') break;
        if (count >= 3U) return Result(COMMAND_ERR_ARGUMENTS, 0);
        tokens[count++] = cursor;
        while (*cursor != '\0' && *cursor != ' ') ++cursor;
        if (*cursor != '\0') *cursor++ = '\0';
    }
    if (count == 0) return Result(COMMAND_NONE, 0);
    if (strcmp(tokens[0], "SAVE") == 0 || strcmp(tokens[0], "LOAD") == 0)
    {
        if (count != 1U) return Result(COMMAND_ERR_ARGUMENTS, 0);
        return Result(strcmp(tokens[0], "SAVE") == 0 ? COMMAND_SAVE : COMMAND_LOAD, 0);
    }
    if (strcmp(tokens[0], "CONFIG") == 0 || strcmp(tokens[0], "DEFAULTS") == 0)
    {
        if (count != 1U) return Result(COMMAND_ERR_ARGUMENTS, 0);
        return Result(strcmp(tokens[0], "CONFIG") == 0 ? COMMAND_CONFIG : COMMAND_DEFAULTS, 0);
    }
    if (strcmp(tokens[0], "CAL") == 0)
    {
        if (count != 3U) return Result(COMMAND_ERR_ARGUMENTS, 0);
        numericStatus = ParseUnsigned(tokens[1], 4095U, &number);
        if (numericStatus == 1U) return Result(COMMAND_ERR_ARGUMENTS, 0);
        if (numericStatus == 2U) return Result(COMMAND_ERR_CALIBRATION, 0);
        numericStatus = ParseUnsigned(tokens[2], 4095U, &second);
        if (numericStatus == 1U) return Result(COMMAND_ERR_ARGUMENTS, 0);
        if (numericStatus == 2U) return Result(COMMAND_ERR_CALIBRATION, 0);
        result = Result(COMMAND_CAL, number);
        result.value2 = second;
        return result;
    }
    if (strcmp(tokens[0], "RAMP") == 0)
    {
        if (count != 2U) return Result(COMMAND_ERR_ARGUMENTS, 0);
        numericStatus = ParseUnsigned(tokens[1], 200U, &number);
        if (numericStatus == 1U) return Result(COMMAND_ERR_ARGUMENTS, 0);
        if (numericStatus == 2U) return Result(COMMAND_ERR_RAMP, 0);
        return Result(COMMAND_RAMP, number);
    }
    if (strcmp(tokens[0], "GET") == 0 || strcmp(tokens[0], "HELP") == 0)
    {
        if (count != 1U) return Result(COMMAND_ERR_ARGUMENTS, 0);
        return Result(strcmp(tokens[0], "GET") == 0 ? COMMAND_GET : COMMAND_HELP, 0);
    }
    if (strcmp(tokens[0], "MODE") == 0)
    {
        if (count != 2U) return Result(COMMAND_ERR_ARGUMENTS, 0);
        if (strcmp(tokens[1], "AUTO") == 0) return Result(COMMAND_MODE, COMMAND_MODE_AUTO);
        if (strcmp(tokens[1], "LOCAL") == 0) return Result(COMMAND_MODE, COMMAND_MODE_LOCAL);
        if (strcmp(tokens[1], "REMOTE") == 0) return Result(COMMAND_MODE, COMMAND_MODE_REMOTE);
        return Result(COMMAND_ERR_ARGUMENTS, 0);
    }
    if (strcmp(tokens[0], "STREAM") == 0)
    {
        if (count != 2U) return Result(COMMAND_ERR_ARGUMENTS, 0);
        if (strcmp(tokens[1], "ON") == 0) return Result(COMMAND_STREAM, 1);
        if (strcmp(tokens[1], "OFF") == 0) return Result(COMMAND_STREAM, 0);
        return Result(COMMAND_ERR_ARGUMENTS, 0);
    }
    if (strcmp(tokens[0], "PWM") == 0)
    {
        if (count != 2U) return Result(COMMAND_ERR_ARGUMENTS, 0);
        numericStatus = ParseUnsigned(tokens[1], 100U, &number);
        if (numericStatus == 1U) return Result(COMMAND_ERR_ARGUMENTS, 0);
        if (numericStatus == 2U) return Result(COMMAND_ERR_RANGE, 0);
        return Result(COMMAND_PWM, number);
    }
    return Result(COMMAND_ERR_UNKNOWN, 0);
}

void CommandProtocol_Init(CommandParser *parser)
{
    parser->length = 0;
    parser->discarding = 0;
    parser->lastByteMs = 0;
}

void CommandProtocol_Discard(CommandParser *parser, uint32_t now)
{
    parser->length = 0;
    parser->discarding = 1;
    parser->lastByteMs = now;
}

CommandResult CommandProtocol_Feed(CommandParser *parser, uint8_t byte, uint32_t now)
{
    CommandResult result;
    parser->lastByteMs = now;
    if (byte == '\r' || byte == '\n')
    {
        if (parser->discarding || parser->length == 0)
        {
            parser->length = 0;
            parser->discarding = 0;
            return Result(COMMAND_NONE, 0);
        }
        parser->line[parser->length] = '\0';
        parser->length = 0;
        result = ParseLine(parser->line);
        return result;
    }
    if (parser->discarding) return Result(COMMAND_NONE, 0);
    if (byte == '\t') byte = ' ';
    if (byte < 0x20U || byte > 0x7eU)
    {
        CommandProtocol_Discard(parser, now);
        return Result(COMMAND_ERR_CHARACTER, 0);
    }
    if (parser->length >= COMMAND_MAX_LENGTH)
    {
        CommandProtocol_Discard(parser, now);
        return Result(COMMAND_ERR_TOO_LONG, 0);
    }
    if (byte >= 'a' && byte <= 'z') byte = (uint8_t)(byte - 'a' + 'A');
    parser->line[parser->length++] = (char)byte;
    return Result(COMMAND_NONE, 0);
}

CommandResult CommandProtocol_PollTimeout(CommandParser *parser, uint32_t now)
{
    uint8_t wasDiscarding;
    if ((parser->length != 0 || parser->discarding) &&
        (uint32_t)(now - parser->lastByteMs) >= COMMAND_TIMEOUT_MS)
    {
        wasDiscarding = parser->discarding;
        CommandProtocol_Init(parser);
        if (!wasDiscarding) return Result(COMMAND_ERR_INCOMPLETE, 0);
    }
    return Result(COMMAND_NONE, 0);
}
