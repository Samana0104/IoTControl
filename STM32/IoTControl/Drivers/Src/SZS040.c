#include "SZS040.h"
#include "SUsart.h"

typedef struct _SZS040
{
    // Init 전에는 NULL
    UART_HandleTypeDef *huart;

    // STATE 핀 (연결 시 HIGH), statePort가 NULL이면 미사용
    GPIO_TypeDef *statePort;
    uint16_t statePin;

    // ReadLine 줄 조립용
    char line[SZS040_LINE_SIZE];
    uint8_t lineLength;
    bool lineOverflow;
} SZS040;

static SZS040 bt;

bool SZS040Init(UART_HandleTypeDef *huart, GPIO_TypeDef *statePort, uint16_t statePin)
{
    bt.huart = huart;
    bt.statePort = statePort;
    bt.statePin = statePin;
    bt.lineLength = 0;
    bt.lineOverflow = false;

    return SUsartBegin(huart);
}

bool SZS040IsConnected(void)
{
    if (bt.statePort == NULL)
    {
        return false;
    }

    return HAL_GPIO_ReadPin(bt.statePort, bt.statePin) == GPIO_PIN_SET;
}

bool SZS040ReadByte(uint8_t *byte)
{
    if (bt.huart == NULL)
    {
        return false;
    }

    return SUsartReadByte(bt.huart, byte);
}

const char *SZS040ReadLine(void)
{
    uint8_t c;
    while (SZS040ReadByte(&c))
    {
        if (c == '\r' || c == '\n')
        {
            // "\r\n"의 두 번째 문자나 빈 줄은 무시
            if (bt.lineLength == 0 && !bt.lineOverflow)
            {
                continue;
            }

            bool overflow = bt.lineOverflow;
            bt.line[bt.lineLength] = '\0';
            bt.lineLength = 0;
            bt.lineOverflow = false;

            // 너무 긴 줄은 통째로 버림
            if (overflow)
            {
                continue;
            }
            return bt.line;
        }

        // '\0' 자리 1바이트 남겨둠
        if (bt.lineLength < SZS040_LINE_SIZE - 1)
        {
            bt.line[bt.lineLength++] = (char)c;
        }
        else
        {
            bt.lineOverflow = true;
        }
    }

    return NULL;
}

void SZS040Write(const uint8_t *data, uint16_t length)
{
    if (bt.huart == NULL)
    {
        return;
    }

    SUsartWrite(bt.huart, data, length);
}

void SZS040Printf(const char *format, ...)
{
    if (bt.huart == NULL)
    {
        return;
    }

    va_list args;
    va_start(args, format);
    SUsartVPrintf(bt.huart, format, args);
    va_end(args);
}

bool SZS040SendAT(const char *command, char *response, uint16_t responseSize)
{
    if (bt.huart == NULL)
    {
        return false;
    }

    // 이전에 받다 만 데이터가 응답으로 섞이지 않게 비움
    SUsartFlush(bt.huart);
    bt.lineLength = 0;
    bt.lineOverflow = false;

    if (response != NULL && responseSize > 0)
    {
        response[0] = '\0';
    }

    SUsartPrintf(bt.huart, "%s\r\n", command);

    uint32_t startMs = HAL_GetTick();
    while (HAL_GetTick() - startMs < SZS040_AT_TIMEOUT_MS)
    {
        const char *line = SZS040ReadLine();
        if (line == NULL)
        {
            continue;
        }

        if (strcmp(line, "OK") == 0)
        {
            return true;
        }

        if (strncmp(line, "ERROR", 5) == 0)
        {
            return false;
        }

        // OK 전에 온 응답 줄 (여러 줄이면 첫 줄만)
        if (response != NULL && responseSize > 0 && response[0] == '\0')
        {
            strncpy(response, line, responseSize - 1);
            response[responseSize - 1] = '\0';
        }
    }

    return false;
}
