#include "AppMain.h"
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
#define INTERVAL_MS_5SEC 5000

static SIntervalMS interval500MS;
static SIntervalMS interval5Sec;

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
#ifdef DEBUG_BUILD
    SCLIInit(&huart2);
#endif

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

    SIntervalMSInit(&interval500MS, INTERVAL_MS_500MS);
    SIntervalMSInit(&interval5Sec, INTERVAL_MS_5SEC);
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
        SFanUpdate();
        // readDht();
    }

    if (SIntervalMSElapsed(&interval5Sec, currentTime))
    {
        // sendStatus();
    }
}
