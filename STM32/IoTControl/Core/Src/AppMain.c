#include "AppMain.h"
#include "SDht.h"
#include "SIntervalMS.h"
#include "SLog.h"
#include "SFan.h"
#include "SZS040.h"
#include "tim.h"
#include "i2c.h"
#include "SIotProtocol.h"
#include "usart.h"
#include "SClcd.h"

#ifdef DEBUG_BUILD
#include "SCLI.h"
#endif

#define INTERVAL_MS_500MS 500
#define INTERVAL_MS_2SEC 2000

static SIntervalMS interval500MS;
static SIntervalMS interval2Sec;

static void HandleBluetoothPacket(uint8_t cmd, const uint8_t *data, uint8_t length)
{
    if (cmd == CMD_CHAT_DATA)
    {
        SLOG_INFO("bt chat rx (%u bytes): %.*s", (unsigned int)length, (int)length, (const char *)data);
    }
    else
    {
        // 센서/제어 명령의 응용 동작은 해당 cmd의 처리부에서 연결.
        SLOG_INFO("bt packet rx: cmd=%u, length=%u", (unsigned int)cmd, (unsigned int)length);
    }
}

static void HandleBluetoothSend(uint8_t cmd, bool success)
{
    if (success)
    {
        SLOG_INFO("bt DATA transmitted: cmd=%u", (unsigned int)cmd);
    }
    else
    {
        SLOG_ERROR("bt send failed: cmd=%u (RQ timeout/mismatch or UART error)", (unsigned int)cmd);
    }
}

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

    if(SClcdInit(&hi2c1,0x27))
    {
        SClcdSetCursor(0, 0);
        SClcdWriteString("IoTControl");
        SClcdSetCursor(0, 1);
        SClcdPrintf("Temp: %d C", 25);
    }
    else
    {
        SLOG_ERROR("CLcd init failed");
    }
    
    // UART 연결: USART2 = 시리얼(CLI), USART1 = 블루투스(ZS-040)

    if (!SZS040Init(&huart1, NULL, 0))
    {
        SLOG_ERROR("bluetooth uart init failed");
    }
    else
    {
        const SIotProtocolIo io = {SZS040ReadByte, SZS040Write, HAL_GetTick};
        SIotProtocolInit(&io, HandleBluetoothPacket, HandleBluetoothSend);
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

    const char *btLine = SZS040ReadLine();
    if (btLine != NULL)
    {
        SLOG_INFO("bt rx: %s", btLine);
    }

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
        int16_t temperature = SDhtGetTemperature();
        uint16_t humidity = SDhtGetHumidity();
        SLOG_INFO("DHT sensor: temperature=%d.%d C, humidity=%u.%u %%", temperature / 10, abs(temperature % 10),
                  humidity / 10U, humidity % 10U);
    }
}
