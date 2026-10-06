#include "SCLI.h"
#include "SCommand.h"
#include "SCmdGpio.h"
#include "SCmdSys.h"
#include "usart.h"

#define SCLI_RX_BUFFER_SIZE 64
#define SCLI_LINE_SIZE 64
#define SCLI_PRINT_SIZE 128
#define SCLI_TX_TIMEOUT_MS 100

static const SCommand commands[] = {
    {"gpio", SCmdGpio},
    {"sys", SCmdSys},
};

static UART_HandleTypeDef *const cliUart = &huart2;

// DMA가 직접 채우는 원형 버퍼 (인터럽트에서 복사하지 않음)
static uint8_t rxBuffer[SCLI_RX_BUFFER_SIZE];

// 인터럽트 -> 메인 전달용: 인터럽트에서는 이 값들만 기록
static volatile uint16_t rxWritePos = 0;
static volatile bool rxFlag = false;
static volatile bool rxRestartFlag = false;

// 메인 루프에서만 사용
static uint16_t rxReadPos = 0;
static uint8_t lineLength = 0;
static char line[SCLI_LINE_SIZE];
static bool lineOverflow = false;
static bool lastWasCR = false;

static char printBuffer[SCLI_PRINT_SIZE];

static void StartReceive(void)
{
    rxReadPos = 0;
    rxWritePos = 0;
    HAL_UARTEx_ReceiveToIdle_DMA(cliUart, rxBuffer, SCLI_RX_BUFFER_SIZE);
}

void SCLIInit(void)
{
    StartReceive();
}

// ---------------------------------------------------------------------------
// 인터럽트 콜백: 위치와 플래그만 기록
// ---------------------------------------------------------------------------

// IDLE(입력 멈춤), DMA 절반, DMA 끝에서 호출, size = 버퍼 시작부터 받은 위치
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    if (huart != cliUart)
    {
        return;
    }

    rxWritePos = size;
    rxFlag = true;
}

// 오버런 등으로 HAL이 수신을 멈추면 메인에서 다시 시작
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart != cliUart)
    {
        return;
    }

    rxRestartFlag = true;
    rxFlag = true;
}

// ---------------------------------------------------------------------------
// 메인 루프 처리
// ---------------------------------------------------------------------------

static void Echo(const char *text, uint16_t length)
{
    HAL_UART_Transmit(cliUart, (const uint8_t *)text, length, SCLI_TX_TIMEOUT_MS);
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
    if (!rxFlag)
    {
        return;
    }

    // 처리 중에 들어온 수신은 플래그를 다시 세우도록 먼저 내림
    rxFlag = false;

    if (rxRestartFlag)
    {
        rxRestartFlag = false;
        StartReceive();
        return;
    }

    // DMA 끝(size == 버퍼 크기)은 처음 위치와 같음
    uint16_t writePos = rxWritePos;
    if (writePos >= SCLI_RX_BUFFER_SIZE)
    {
        writePos = 0;
    }

    while (rxReadPos != writePos)
    {
        ProcessChar((char)rxBuffer[rxReadPos]);
        rxReadPos = (rxReadPos + 1) % SCLI_RX_BUFFER_SIZE;
    }
}

void SCLIPrintf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int length = vsnprintf(printBuffer, sizeof(printBuffer), format, args);
    va_end(args);

    if (length <= 0)
    {
        return;
    }

    if (length > (int)sizeof(printBuffer) - 1)
    {
        length = sizeof(printBuffer) - 1;
    }

    HAL_UART_Transmit(cliUart, (uint8_t *)printBuffer, (uint16_t)length, SCLI_TX_TIMEOUT_MS);
}
