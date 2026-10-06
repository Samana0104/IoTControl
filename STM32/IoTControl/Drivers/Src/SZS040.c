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

bool SZS040SetUartBaud(uint32_t baud)
{
    if (bt.huart == NULL)
    {
        return false;
    }

    bt.lineLength = 0;
    bt.lineOverflow = false;
    return SUsartSetBaud(bt.huart, baud);
}

uint32_t SZS040GetUartBaud(void)
{
    return (bt.huart != NULL) ? bt.huart->Init.BaudRate : 0;
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
    if (response != NULL && responseSize > 0)
    {
        response[0] = '\0';
    }

    if (bt.huart == NULL)
    {
        return false;
    }

    // 이전에 받다 만 데이터가 응답으로 섞이지 않게 비움
    SUsartFlush(bt.huart);
    bt.lineLength = 0;
    bt.lineOverflow = false;

    SZS040Write((const uint8_t *)command, (uint16_t)strlen(command));

    // 응답에 줄바꿈이 없어서, 바이트가 끊긴 뒤 SZS040_AT_IDLE_MS 동안 조용하면 끝으로 봄
    char received[SZS040_LINE_SIZE];
    uint16_t length = 0;
    uint32_t startMs = HAL_GetTick();
    uint32_t lastByteMs = 0;

    while (HAL_GetTick() - startMs < SZS040_AT_TIMEOUT_MS)
    {
        uint8_t c;
        if (SZS040ReadByte(&c))
        {
            if (length < sizeof(received) - 1)
            {
                received[length++] = (char)c;
            }
            lastByteMs = HAL_GetTick();
            continue;
        }

        if (length > 0 && HAL_GetTick() - lastByteMs >= SZS040_AT_IDLE_MS)
        {
            break;
        }
    }
    received[length] = '\0';

    if (response != NULL && responseSize > 0)
    {
        strncpy(response, received, responseSize - 1);
        response[responseSize - 1] = '\0';
    }

    return strncmp(received, "OK", 2) == 0;
}

// ---------------------------------------------------------------------------
// 자주 쓰는 AT 명령
// ---------------------------------------------------------------------------

#define SZS040_COMMAND_SIZE 32
#define SZS040_NAME_MAX 20
#define SZS040_PIN_LENGTH 4

bool SZS040Test(void)
{
    return SZS040SendAT("AT", NULL, 0);
}

bool SZS040GetVersion(char *version, uint16_t size)
{
    char response[SZS040_LINE_SIZE];
    if (!SZS040SendAT("AT+VERSION", response, sizeof(response)) || version == NULL || size == 0)
    {
        return false;
    }

    // "OKlinvorV1.8" -> "linvorV1.8"
    strncpy(version, response + 2, size - 1);
    version[size - 1] = '\0';
    return true;
}

bool SZS040SetName(const char *name)
{
    size_t length = strlen(name);
    if (length == 0 || length > SZS040_NAME_MAX)
    {
        return false;
    }

    char command[SZS040_COMMAND_SIZE];
    snprintf(command, sizeof(command), "AT+NAME%s", name);
    return SZS040SendAT(command, NULL, 0);
}

bool SZS040SetPin(const char *pin)
{
    if (strlen(pin) != SZS040_PIN_LENGTH)
    {
        return false;
    }

    for (uint8_t i = 0; i < SZS040_PIN_LENGTH; ++i)
    {
        if (pin[i] < '0' || pin[i] > '9')
        {
            return false;
        }
    }

    char command[SZS040_COMMAND_SIZE];
    snprintf(command, sizeof(command), "AT+PIN%s", pin);
    return SZS040SendAT(command, NULL, 0);
}

bool SZS040SetBaud(uint32_t baud)
{
    // AT+BAUD 뒤에 붙는 번호 = 배열 인덱스 + 1
    static const uint32_t baudTable[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};

    for (uint8_t i = 0; i < sizeof(baudTable) / sizeof(baudTable[0]); ++i)
    {
        if (baudTable[i] == baud)
        {
            char command[SZS040_COMMAND_SIZE];
            snprintf(command, sizeof(command), "AT+BAUD%u", i + 1);
            if (!SZS040SendAT(command, NULL, 0))
            {
                return false;
            }

            // 모듈은 응답 직후 새 보레이트로 바뀌므로 MCU 쪽도 바로 맞춤
            return SZS040SetUartBaud(baud);
        }
    }

    return false;
}
