#include "SUsart.h"
#include "SLog.h"

#define SUSART_SLOT_COUNT 3
#define SUSART_PRINT_SIZE 128

// UART 하나의 수신 상태
typedef struct _SUsart
{
    // Begin 전에는 NULL
    UART_HandleTypeDef *huart;
    uint8_t rxBuffer[SUSART_RX_BUFFER_SIZE];

    // 메인 루프에서만 사용
    uint16_t rxReadPos;
    uint16_t rxReadEnd;

    // 인터럽트 -> 메인 전달용
    volatile uint16_t rxWritePos;
    volatile bool rxFlag;
    volatile bool rxRestartFlag;
} SUsart;

// F411 USART1 / USART2 / USART6 칸
static SUsart usarts[SUSART_SLOT_COUNT];

// Printf 공용 버퍼 (메인 루프에서만 사용)
static char printBuffer[SUSART_PRINT_SIZE];

// 하드웨어 인스턴스로 칸을 고름, F411에 없는 UART면 NULL
static SUsart *GetSlot(const UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        return &usarts[0];
    }
    if (huart->Instance == USART2)
    {
        return &usarts[1];
    }
    if (huart->Instance == USART6)
    {
        return &usarts[2];
    }
    return NULL;
}

// Begin 한 칸만 반환
static SUsart *FindUsart(const UART_HandleTypeDef *huart)
{
    SUsart *usart = GetSlot(huart);
    return (usart != NULL && usart->huart == huart) ? usart : NULL;
}

static void StartReceive(SUsart *usart)
{
    usart->rxReadPos = 0;
    usart->rxReadEnd = 0;
    usart->rxWritePos = 0;
    HAL_UARTEx_ReceiveToIdle_DMA(usart->huart, usart->rxBuffer, SUSART_RX_BUFFER_SIZE);
}

bool SUsartBegin(UART_HandleTypeDef *huart)
{
    SUsart *usart = GetSlot(huart);
    if (usart == NULL)
    {
        SLOG_ERROR("usart not supported (only USART1/2/6)");
        return false;
    }

    if (usart->huart != NULL)
    {
        SLOG_ERROR("usart already begun");
        return false;
    }

    if (huart->hdmarx == NULL)
    {
        SLOG_ERROR("usart rx dma not linked (CubeMX DMA RX Circular)");
        return false;
    }

    usart->huart = huart;
    usart->rxFlag = false;
    usart->rxRestartFlag = false;
    StartReceive(usart);
    return true;
}

bool SUsartSetBaud(UART_HandleTypeDef *huart, uint32_t baud)
{
    SUsart *usart = FindUsart(huart);

    if (usart != NULL)
    {
        HAL_UART_AbortReceive(huart);
    }

    // 이미 초기화된 핸들이라 MspInit(GPIO/DMA)은 다시 안 타고 BRR만 다시 계산됨
    huart->Init.BaudRate = baud;
    if (HAL_UART_Init(huart) != HAL_OK)
    {
        return false;
    }

    if (usart != NULL)
    {
        usart->rxFlag = false;
        usart->rxRestartFlag = false;
        StartReceive(usart);
    }
    return true;
}

// ---------------------------------------------------------------------------
// HAL 콜백 (프로젝트에서 여기만 정의): 위치와 플래그만 기록
// ---------------------------------------------------------------------------

// IDLE(입력 멈춤), DMA 절반, DMA 끝에서 호출, size = 버퍼 시작부터 받은 위치
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    SUsart *usart = FindUsart(huart);
    if (usart == NULL)
    {
        return;
    }

    usart->rxWritePos = size;
    usart->rxFlag = true;
}

// 오버런 등으로 HAL이 수신을 멈추면 메인에서 다시 시작
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    SUsart *usart = FindUsart(huart);
    if (usart == NULL)
    {
        return;
    }

    usart->rxRestartFlag = true;
    usart->rxFlag = true;
}

// ---------------------------------------------------------------------------
// 메인 루프 처리
// ---------------------------------------------------------------------------

bool SUsartReadByte(UART_HandleTypeDef *huart, uint8_t *byte)
{
    SUsart *usart = FindUsart(huart);
    if (usart == NULL)
    {
        return false;
    }

    if (usart->rxReadPos == usart->rxReadEnd)
    {
        if (!usart->rxFlag)
        {
            return false;
        }

        // 처리 중에 들어온 수신은 플래그를 다시 세우도록 먼저 내림
        usart->rxFlag = false;

        if (usart->rxRestartFlag)
        {
            usart->rxRestartFlag = false;
            StartReceive(usart);
            return false;
        }

        // DMA 끝(size == 버퍼 크기)은 처음 위치와 같음
        uint16_t writePos = usart->rxWritePos;
        usart->rxReadEnd = (writePos >= SUSART_RX_BUFFER_SIZE) ? 0 : writePos;

        if (usart->rxReadPos == usart->rxReadEnd)
        {
            return false;
        }
    }

    *byte = usart->rxBuffer[usart->rxReadPos];
    usart->rxReadPos = (usart->rxReadPos + 1) % SUSART_RX_BUFFER_SIZE;
    return true;
}

void SUsartFlush(UART_HandleTypeDef *huart)
{
    uint8_t byte;
    while (SUsartReadByte(huart, &byte))
    {
    }
}

void SUsartWrite(UART_HandleTypeDef *huart, const uint8_t *data, uint16_t length)
{
    // 1바이트 = 10비트(start + 8 + stop), 여유 10ms
    uint32_t timeoutMs = (uint32_t)length * 10U * 1000U / huart->Init.BaudRate + 10U;
    HAL_UART_Transmit(huart, data, length, timeoutMs);
}

void SUsartVPrintf(UART_HandleTypeDef *huart, const char *format, va_list args)
{
    int length = vsnprintf(printBuffer, sizeof(printBuffer), format, args);
    if (length <= 0)
    {
        return;
    }

    if (length > (int)sizeof(printBuffer) - 1)
    {
        length = sizeof(printBuffer) - 1;
    }

    SUsartWrite(huart, (const uint8_t *)printBuffer, (uint16_t)length);
}

void SUsartPrintf(UART_HandleTypeDef *huart, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    SUsartVPrintf(huart, format, args);
    va_end(args);
}
