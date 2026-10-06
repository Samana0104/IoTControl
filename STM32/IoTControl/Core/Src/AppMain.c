#include "AppMain.h"
#include "SIntervalMS.h"
#include "SLog.h"
#include "SZS040.h"
#include "usart.h"

#ifdef DEBUG_BUILD
#include "SCLI.h"
#endif

// 내부에서만 사용
#define INTERVAL_MS_500MS 500
#define INTERVAL_MS_2SEC 2000
#define INTERVAL_MS_5SEC 5000

static SIntervalMS interval500MS;
static SIntervalMS interval2Sec;
static SIntervalMS interval5Sec;

void AppMain(void)
{
    AppInit();

    while (1)
    {
        AppUpdate();
    }
}

void AppInit(void)
{
    SLOG_INFO("Boot STM32");

    // UART 연결: USART2 = 시리얼(CLI), USART1 = 블루투스(ZS-040)
#ifdef DEBUG_BUILD
    SCLIInit(&huart2);
#endif

    if (!SZS040Init(&huart1, NULL, 0))
    {
        SLOG_ERROR("bluetooth uart init failed");
    }

    SIntervalMSInit(&interval500MS, INTERVAL_MS_500MS);
    SIntervalMSInit(&interval2Sec, INTERVAL_MS_2SEC);
    SIntervalMSInit(&interval5Sec, INTERVAL_MS_5SEC);
}

void AppUpdate(void)
{
#ifdef DEBUG_BUILD
    SCLIUpdate();
#endif

    const char *btLine = SZS040ReadLine();
    if (btLine != NULL)
    {
        SLOG_INFO("bt rx: %s", btLine);
    }

    uint32_t currentTime = HAL_GetTick();

    if (SIntervalMSElapsed(&interval500MS, currentTime))
    {
        // 동작 확인용 보드 LED 깜빡임
        // HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
    }

    if (SIntervalMSElapsed(&interval2Sec, currentTime))
    {
        // readDht();
    }

    if (SIntervalMSElapsed(&interval5Sec, currentTime))
    {
        // sendStatus();
    }
}
