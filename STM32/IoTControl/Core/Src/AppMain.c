#include "AppMain.h"
#include "SIntervalMS.h"

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
    SIntervalMSInit(&interval500MS, INTERVAL_MS_500MS);
    SIntervalMSInit(&interval2Sec, INTERVAL_MS_2SEC);
    SIntervalMSInit(&interval5Sec, INTERVAL_MS_5SEC);
}

void AppUpdate(void)
{
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
