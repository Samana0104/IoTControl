#include "AppMain.h"
#include "SDht.h"
#include "SIntervalMS.h"
#include "SLog.h"
#include "SFan.h"
#include "SZS040.h"
#include "tim.h"
#include "SIotProtocol.h"
#include "usart.h"
#include "SData.h"

#ifdef DEBUG_BUILD
#include "SCLI.h"
#endif

#define INTERVAL_MS_500MS 500
#define INTERVAL_MS_2SEC 2000

static SIntervalMS interval500MS;
static SIntervalMS interval2Sec;

void AppMain(void)
{
    AppInit();

    SLOG_INFO("AppMain started");
    while (1)
    {
        AppUpdate();
    }
    
    SLOG_INFO("AppMain exited");
}

void AppInit(void)
{
#ifdef DEBUG_BUILD
    SCLIInit(&huart2);
#endif

    SLOG_INFO("Boot STM32");

    SDataInit();

    // UART 연결: USART2 = 시리얼(CLI), USART1 = 블루투스(ZS-040)

    if (!SZS040Init(&huart1, NULL, 0))
    {
        SLOG_ERROR("bluetooth uart init failed");
    }
    else
    {
        SIotProtocolInit();
    }

    // TIM3 CH1 PWM (25kHz) = 팬
    if (!SFanInit(&htim3, TIM_CHANNEL_1))
    {
        SLOG_ERROR("fan pwm init failed");
    }
    
    // DHT 신호선 1개 = TIM4 CH1 핀 (PB6), 시작 신호와 입력 캡처를 같이 함
    if (!SDhtInit(&htim4, TIM_CHANNEL_1, GPIOB, GPIO_PIN_6))
    {
        SLOG_ERROR("DHT sensor init failed");
    }

    SIntervalMSInit(&interval500MS, INTERVAL_MS_500MS);
    SIntervalMSInit(&interval2Sec, INTERVAL_MS_2SEC);
}

void AppUpdate(void)
{
    uint32_t currentTime = HAL_GetTick();

    SIotProtocolUpdate();

#ifdef DEBUG_BUILD
    SCLIUpdate();
#endif

    if (SIntervalMSElapsed(&interval500MS, currentTime))
    {
        SDhtStartRead();
        SFanUpdate();
    }

    if (SIntervalMSElapsed(&interval2Sec, currentTime))
    {
        // sendStatus();
        //
        // 지난 주기에 시작한 읽기 결과 (읽기는 약 25ms면 끝남)
        // 값은 0.1 단위 정수 (235 = 23.5), %f는 newlib-nano 기본 설정에서 출력 안 됨
    }
}
