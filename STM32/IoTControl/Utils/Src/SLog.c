#include "SLog.h"
#include "usart.h"

#include <stdarg.h>
#include <stdio.h>

#define SLOG_TX_TIMEOUT_MS 100

// 로그 출력 UART (CubeMX에서 생성된 USART2, main()에서 MX_USART2_UART_Init 이후 사용)
static UART_HandleTypeDef *const logUart = &huart2;
static uint8_t logLevel = SLOG_LEVEL;
static char logLine[SLOG_LINE_SIZE];

void SLogSetLevel(uint8_t level)
{
    logLevel = level;
}

uint8_t SLogGetLevel(void)
{
    return logLevel;
}

static char LevelToChar(uint8_t level)
{
    switch (level)
    {
    case SLOG_LEVEL_ERROR:
        return 'E';
    case SLOG_LEVEL_WARN:
        return 'W';
    case SLOG_LEVEL_INFO:
        return 'I';
    default:
        return 'D';
    }
}

void SLogWrite(uint8_t level, const char *format, ...)
{
    if (level == SLOG_LEVEL_NONE || level > logLevel)
    {
        return;
    }

    // 줄바꿈 "\r\n" 2바이트는 항상 남겨둠
    const int bodyLimit = SLOG_LINE_SIZE - 2;

    int length = snprintf(logLine, bodyLimit, "[%lu][%c] ", (unsigned long)HAL_GetTick(), LevelToChar(level));
    if (length < 0)
    {
        return;
    }

    if (length < bodyLimit)
    {
        va_list args;
        va_start(args, format);
        int bodyLength = vsnprintf(logLine + length, bodyLimit - length, format, args);
        va_end(args);

        if (bodyLength > 0)
        {
            length += bodyLength;
        }
    }

    // 잘린 경우 버퍼 끝(널 문자 자리)까지만
    if (length > bodyLimit - 1)
    {
        length = bodyLimit - 1;
    }

    logLine[length++] = '\r';
    logLine[length++] = '\n';

    HAL_UART_Transmit(logUart, (uint8_t *)logLine, (uint16_t)length, SLOG_TX_TIMEOUT_MS);
}
