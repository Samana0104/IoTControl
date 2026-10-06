#include "SCLI.h"
#include "SCommand.h"
#include "SCmdGpio.h"
#include "SCmdSys.h"
#include "SUsart.h"

#define SCLI_LINE_SIZE 64

static const SCommand commands[] = {
    {"gpio", SCmdGpio},
    {"sys", SCmdSys},
};

// SCLIInit에서 연결 (Init 전에는 NULL)
static UART_HandleTypeDef *cliUart = NULL;

static uint8_t lineLength = 0;
static char line[SCLI_LINE_SIZE];
static bool lineOverflow = false;
static bool lastWasCR = false;

void SCLIInit(UART_HandleTypeDef *huart)
{
    cliUart = huart;
    SUsartBegin(huart);
}

static void Echo(const char *text, uint16_t length)
{
    SUsartWrite(cliUart, (const uint8_t *)text, length);
}

static void ProcessChar(char c)
{
    // \r\n 으로 들어오면 \n 은 이미 처리한 줄바꿈이라 무시
    if (c == '\n' && lastWasCR)
    {
        lastWasCR = false;
        return;
    }
    lastWasCR = (c == '\r');

    // 백스페이스 (터미널에 따라 0x08 또는 0x7F)
    if (c == '\b' || c == 0x7F)
    {
        if (lineLength > 0 && !lineOverflow)
        {
            --lineLength;
            Echo("\b \b", 3);
        }
        return;
    }

    if (c == '\r' || c == '\n')
    {
        Echo("\r\n", 2);
        line[lineLength] = '\0';
        if (lineOverflow)
        {
            SCLIPrintf("command too long\r\n");
        }
        else if (lineLength > 0)
        {
            SCommandDispatch(line, NULL, commands, SCOMMAND_COUNT(commands));
        }
        lineLength = 0;
        lineOverflow = false;
        return;
    }

    Echo(&c, 1);

    // '\0' 자리 1바이트 남겨둠, 넘치면 줄바꿈까지 버림
    if (lineLength < SCLI_LINE_SIZE - 1)
    {
        line[lineLength++] = c;
    }
    else
    {
        lineOverflow = true;
    }
}

void SCLIUpdate(void)
{
    if (cliUart == NULL)
    {
        return;
    }

    uint8_t c;
    while (SUsartReadByte(cliUart, &c))
    {
        ProcessChar((char)c);
    }
}

void SCLIPrintf(const char *format, ...)
{
    if (cliUart == NULL)
    {
        return;
    }

    va_list args;
    va_start(args, format);
    SUsartVPrintf(cliUart, format, args);
    va_end(args);
}
