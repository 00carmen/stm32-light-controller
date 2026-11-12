#include "CommandProtocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned cases;

static CommandResult FeedText(CommandParser *parser, const char *text, uint32_t now)
{
    CommandResult result = {COMMAND_NONE, 0, 0};
    CommandResult next;
    while (*text)
    {
        next = CommandProtocol_Feed(parser, (uint8_t)*text++, now++);
        if (next.type != COMMAND_NONE)
        {
            assert(result.type == COMMAND_NONE);
            result = next;
        }
    }
    return result;
}

static void Check(const char *line, CommandType type, unsigned value)
{
    CommandParser parser;
    CommandResult result;
    CommandProtocol_Init(&parser);
    result = FeedText(&parser, line, 0);
    assert(result.type == type);
    if (type == COMMAND_PWM || type == COMMAND_MODE || type == COMMAND_STREAM || type == COMMAND_RAMP)
        assert(result.value == value);
    ++cases;
}

int main(void)
{
    CommandParser parser;
    CommandResult result;
    char line[COMMAND_MAX_LENGTH + 3U];
    const char *batch = "MODE REMOTE\r\nPWM 75\r\nGET\r\n";
    const CommandType expected[] = {COMMAND_MODE, COMMAND_PWM, COMMAND_GET};
    unsigned i = 0;

    Check("GET\n", COMMAND_GET, 0);
    Check("help\r\n", COMMAND_HELP, 0);
    Check("  mOdE\t local  \n", COMMAND_MODE, COMMAND_MODE_LOCAL);
    Check("MODE AUTO\r", COMMAND_MODE, COMMAND_MODE_AUTO);
    Check("MODE REMOTE\n", COMMAND_MODE, COMMAND_MODE_REMOTE);
    Check("PWM 0\n", COMMAND_PWM, 0);
    Check("PWM 100\n", COMMAND_PWM, 100);
    Check("PWM 050\n", COMMAND_PWM, 50);
    Check("PWM 101\n", COMMAND_ERR_RANGE, 0);
    Check("PWM 99999999999999999999999999999999999\n", COMMAND_ERR_RANGE, 0);
    Check("PWM -1\n", COMMAND_ERR_ARGUMENTS, 0);
    Check("PWM 50%\n", COMMAND_ERR_ARGUMENTS, 0);
    Check("PWM 0x10\n", COMMAND_ERR_ARGUMENTS, 0);
    Check("PWM 4.5\n", COMMAND_ERR_ARGUMENTS, 0);
    Check("PWM\n", COMMAND_ERR_ARGUMENTS, 0);
    Check("PWM 10 20\n", COMMAND_ERR_ARGUMENTS, 0);
    Check("GET EXTRA\n", COMMAND_ERR_ARGUMENTS, 0);
    Check("MODE OTHER\n", COMMAND_ERR_ARGUMENTS, 0);
    Check("GET A B C\n", COMMAND_ERR_ARGUMENTS, 0);
    Check("UNKNOWN\n", COMMAND_ERR_UNKNOWN, 0);
    Check("STREAM ON\n", COMMAND_STREAM, 1);
    Check("stream off\n", COMMAND_STREAM, 0);
    Check("STREAM MAYBE\n", COMMAND_ERR_ARGUMENTS, 0);
    Check(" \r\n\n", COMMAND_NONE, 0);
    Check("CONFIG\r\n", COMMAND_CONFIG, 0);
    Check("CONFIG EXTRA\n", COMMAND_ERR_ARGUMENTS, 0);
    Check("defaults\n", COMMAND_DEFAULTS, 0);
    Check("save\r\n", COMMAND_SAVE, 0);
    Check("LOAD\n", COMMAND_LOAD, 0);
    Check("SAVE EXTRA\n", COMMAND_ERR_ARGUMENTS, 0);
    Check("LOAD NOW\n", COMMAND_ERR_ARGUMENTS, 0);
    Check("DEFAULTS 1\n", COMMAND_ERR_ARGUMENTS, 0);
    Check("RAMP 10\n", COMMAND_RAMP, 10);
    Check("RAMP 200\n", COMMAND_RAMP, 200);
    Check("RAMP 0\n", COMMAND_RAMP, 0); /* 应用配置层拒绝小于 10 的值。 */
    Check("RAMP 201\n", COMMAND_ERR_RAMP, 0);
    Check("RAMP 99999999999999999999\n", COMMAND_ERR_RAMP, 0);
    Check("RAMP -1\n", COMMAND_ERR_ARGUMENTS, 0);
    Check("RAMP 20ms\n", COMMAND_ERR_ARGUMENTS, 0);
    Check("RAMP 20 30\n", COMMAND_ERR_ARGUMENTS, 0);
    Check("CAL 5000 100\n", COMMAND_ERR_CALIBRATION, 0);
    Check("CAL 100 5000\n", COMMAND_ERR_CALIBRATION, 0);
    Check("CAL -1 100\n", COMMAND_ERR_ARGUMENTS, 0);
    Check("CAL 4000 ABC\n", COMMAND_ERR_ARGUMENTS, 0);
    Check("CAL 4000\n", COMMAND_ERR_ARGUMENTS, 0);
    Check("CAL 4000 1000 50\n", COMMAND_ERR_ARGUMENTS, 0);
    CommandProtocol_Init(&parser);
    result = FeedText(&parser, "CAL 3764 481\n", 0);
    assert(result.type == COMMAND_CAL && result.value == 3764 && result.value2 == 481);
    ++cases;
    CommandProtocol_Init(&parser);
    result = FeedText(&parser, "CAL 4095 0\n", 0);
    assert(result.type == COMMAND_CAL && result.value == 4095 && result.value2 == 0);
    ++cases;

    /* CRLF does not duplicate a command; multiple lines retain their order. */
    CommandProtocol_Init(&parser);
    while (*batch)
    {
        result = CommandProtocol_Feed(&parser, (uint8_t)*batch++, 50);
        if (result.type != COMMAND_NONE)
        {
            assert(i < 3 && result.type == expected[i++]);
        }
    }
    assert(i == 3); ++cases;

    /* Exact length boundary, overlong-line rejection and recovery. */
    memset(line, ' ', COMMAND_MAX_LENGTH);
    memcpy(line, "GET", 3);
    line[COMMAND_MAX_LENGTH] = '\n'; line[COMMAND_MAX_LENGTH + 1U] = '\0';
    Check(line, COMMAND_GET, 0);
    line[COMMAND_MAX_LENGTH] = ' ';
    line[COMMAND_MAX_LENGTH + 1U] = '\n'; line[COMMAND_MAX_LENGTH + 2U] = '\0';
    CommandProtocol_Init(&parser);
    assert(FeedText(&parser, line, 100).type == COMMAND_ERR_TOO_LONG);
    assert(FeedText(&parser, "GET\n", 200).type == COMMAND_GET); ++cases;

    /* A partial command is never executed merely because a timeout expires. */
    CommandProtocol_Init(&parser);
    assert(FeedText(&parser, "PWM 10", 100).type == COMMAND_NONE);
    assert(CommandProtocol_PollTimeout(&parser, 1104).type == COMMAND_NONE);
    assert(CommandProtocol_PollTimeout(&parser, 1105).type == COMMAND_ERR_INCOMPLETE);
    assert(FeedText(&parser, "GET\n", 1200).type == COMMAND_GET); ++cases;

    CommandProtocol_Init(&parser);
    CommandProtocol_Feed(&parser, 'G', 0xfffffff0U);
    assert(CommandProtocol_PollTimeout(&parser, (uint32_t)(0xfffffff0U + 1000U)).type == COMMAND_ERR_INCOMPLETE);
    ++cases;

    /* Embedded NUL is rejected, rather than truncating to a valid prefix. */
    CommandProtocol_Init(&parser);
    FeedText(&parser, "PWM 1", 0);
    assert(CommandProtocol_Feed(&parser, 0, 10).type == COMMAND_ERR_CHARACTER);
    assert(FeedText(&parser, "00\n", 20).type == COMMAND_NONE);
    assert(FeedText(&parser, "PWM 100\n", 30).value == 100); ++cases;

    CommandProtocol_Init(&parser);
    FeedText(&parser, "PWM ", 0);
    CommandProtocol_Discard(&parser, 10);
    assert(FeedText(&parser, "100\n", 20).type == COMMAND_NONE);
    assert(FeedText(&parser, "GET\n", 40).type == COMMAND_GET); ++cases;
    CommandProtocol_Discard(&parser, 100);
    assert(CommandProtocol_PollTimeout(&parser, 1100).type == COMMAND_NONE);
    assert(FeedText(&parser, "GET\n", 1200).type == COMMAND_GET); ++cases;

    printf("PASS: %u protocol cases\n", cases);
    return 0;
}
