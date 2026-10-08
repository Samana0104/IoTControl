#include "AppMain.h"
#include "SDht.h"
#include "SIntervalMS.h"
#include "SLog.h"
#include "SFan.h"
#include "SZS040.h"
#include "tim.h"
#include "i2c.h"
#include "SIotProtocol.h"
#include "IoTPacketCodec.h"
#include "usart.h"
#include "SClcd.h"
#include "SData.h"

#ifdef DEBUG_BUILD
#include "SCLI.h"
#endif

#define INTERVAL_MS_500MS 500
#define INTERVAL_MS_2SEC 2000

static SIntervalMS interval500MS;
static SIntervalMS interval2Sec;

// 서버 → 장치 REQ_FAN: 팬 속도(0..100%) 적용 후 ACK_FAN으로 결과 응답
static void HandleFanControl(const uint8_t *data, uint16_t length)
{
    FanData fanData;
    uint8_t result = RESULT_FAIL;

    if (ReadFanData(data, length, &fanData) == 0 && fanData.fanSpeed <= SFAN_MAX_PERCENT)
    {
        SFanSetSpeed((uint8_t)fanData.fanSpeed);
        result = RESULT_SUCCESS;
        SLOG_INFO("bt fan control: speed=%u%%", (unsigned int)fanData.fanSpeed);
    }
    else
    {
        SLOG_WARN("bt fan control rejected: length=%u", (unsigned int)length);
    }
    SIotProtocolSendPacket(ACK_FAN, &result, RESULT_DATA_SIZE);
}

// 서버 → 장치 REQ_DHT: 마지막으로 성공한 측정값을 ACK_DHT로 응답 (0.1 단위 → 정수 단위)
// 마지막 읽기가 실패했거나 아직 값이 없으면 RESULT_FAIL
static void HandleDhtRequest(void)
{
    DhtAckData ack = {0};
    uint8_t payload[DHT_ACK_DATA_SIZE];
    int16_t temperature = SDhtGetTemperature();

    ack.result = RESULT_FAIL;
    if (SDhtHasData() && SDhtGetStatus() == SDHT_STATUS_OK && temperature >= 0)
    {
        ack.result = RESULT_SUCCESS;
        ack.dht.temp = (uint16_t)(temperature / 10);
        ack.dht.humi = (uint16_t)(SDhtGetHumidity() / 10);
        SLOG_INFO("bt dht request: temp=%u, humi=%u", (unsigned int)ack.dht.temp, (unsigned int)ack.dht.humi);
    }
    else
    {
        SLOG_WARN("bt dht request: no valid reading, status=%d", (int)SDhtGetStatus());
    }

    // SIotProtocolSendPacket은 payload 바이트를 받으므로 와이어 형식(little-endian)으로 직렬화
    payload[0] = ack.result;
    payload[1] = (uint8_t)(ack.dht.temp & 0xFF);
    payload[2] = (uint8_t)(ack.dht.temp >> 8);
    payload[3] = (uint8_t)(ack.dht.humi & 0xFF);
    payload[4] = (uint8_t)(ack.dht.humi >> 8);
    SIotProtocolSendPacket(ACK_DHT, payload, DHT_ACK_DATA_SIZE);
}

static void HandleBluetoothPacket(uint16_t cmd, const uint8_t *data, uint16_t length)
{
    SLOG_INFO("bt packet rx: cmd=0x%04X, length=%u", (unsigned int)cmd, (unsigned int)length);

    if (cmd == REQ_FAN)
    {
        HandleFanControl(data, length);
    }
    else if (cmd == REQ_DHT)
    {
        HandleDhtRequest();
    }
    else if (cmd == NFY_CHAT)
    {
        SLOG_INFO("bt chat rx (%u bytes): %.*s", (unsigned int)length, (int)length, (const char *)data);
    }
    else
    {
        // 센서/제어 명령의 응용 동작은 해당 cmd의 처리부에서 연결.
        SLOG_WARN("bt packet unhandled: cmd=0x%04X", (unsigned int)cmd);
    }
}

static void HandleBluetoothSend(uint16_t cmd, bool success)
{
    if (success)
    {
        SLOG_INFO("bt frame transmitted: cmd=0x%04X", (unsigned int)cmd);
    }
    else
    {
        SLOG_ERROR("bt send failed: cmd=0x%04X (invalid length or UART error)", (unsigned int)cmd);
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

    SDataInit();

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
        SIotProtocolInit(HandleBluetoothPacket, HandleBluetoothSend);
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
