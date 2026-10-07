#include "SDHT.h"
#include "SLog.h"

// 응답 시작 1개 + 비트 시작 40개 + 마지막 비트 끝 1개
#define SDHT_EDGE_COUNT 42
#define SDHT_DATA_BITS 40

// 하강 엣지 간격: 0 = 50us LOW + 26~28us HIGH (약 78us), 1 = 50us LOW + 70us HIGH (약 120us)
#define SDHT_BIT_THRESHOLD_US 100

// 타이머 1MHz 기준 (1틱 = 1us), 카운터 오버플로(업데이트 인터럽트)로 시간을 잼
#define SDHT_COUNTER_PERIOD 65536U
#define SDHT_START_LOW_US 20000       // 시작 신호 LOW 유지 (최소 18ms)
#define SDHT_RECEIVE_TIMEOUT_US 10000 // 응답 전체가 약 5ms라 넉넉하게

// 연속 읽기 최소 간격
#define SDHT_MIN_INTERVAL_MS 1000

typedef enum _SDhtState
{
    SDHT_STATE_IDLE,
    SDHT_STATE_START_LOW,
    SDHT_STATE_RECEIVING,
} SDhtState;

typedef struct _SDht
{
    // Init 전에는 NULL
    TIM_HandleTypeDef *htim;
    uint32_t channel;

    // 타이머 채널 핀 (시작 신호 때만 잠깐 GPIO 출력으로 바뀜)
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t alternate;

    // 인터럽트에서만 씀
    uint16_t captures[SDHT_EDGE_COUNT];
    uint8_t captureCount;

    // 인터럽트에서 쓰고 메인에서 읽음
    volatile SDhtState state;
    volatile uint32_t lastReadMs;
    volatile bool everRead;
    volatile SDhtStatus status;
    volatile bool hasData;
    volatile int16_t temperatureX10;
    volatile uint16_t humidityX10;
} SDht;

static SDht dht;

// ---------------------------------------------------------------------------
// 핀 전환 (신호선이 하나라 보낼 때는 GPIO 출력, 받을 때는 타이머 입력)
// ---------------------------------------------------------------------------

// F411 타이머별 AF 번호
static uint8_t TimerAlternate(const TIM_TypeDef *instance)
{
    if (instance == TIM1 || instance == TIM2)
    {
        return GPIO_AF1_TIM1;
    }
    if (instance == TIM3 || instance == TIM4 || instance == TIM5)
    {
        return GPIO_AF2_TIM3;
    }
    return GPIO_AF3_TIM9;
}

static void PinDriveLow(void)
{
    // 출력으로 바뀌는 순간 HIGH가 튀지 않게 출력값을 먼저 LOW로
    HAL_GPIO_WritePin(dht.port, dht.pin, GPIO_PIN_RESET);

    GPIO_InitTypeDef init = {0};
    init.Pin = dht.pin;
    init.Mode = GPIO_MODE_OUTPUT_PP;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(dht.port, &init);
}

// 타이머 입력으로 되돌림 = 선을 놓음 (풀업으로 HIGH)
static void PinReleaseToTimer(void)
{
    GPIO_InitTypeDef init = {0};
    init.Pin = dht.pin;
    init.Mode = GPIO_MODE_AF_PP;
    init.Pull = GPIO_PULLUP;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    init.Alternate = dht.alternate;
    HAL_GPIO_Init(dht.port, &init);
}

bool SDhtInit(TIM_HandleTypeDef *htim, uint32_t channel, GPIO_TypeDef *port, uint16_t pin)
{
    if (htim == NULL || port == NULL)
    {
        SLOG_ERROR("dht invalid args");
        return false;
    }

    dht.htim = htim;
    dht.channel = channel;
    dht.port = port;
    dht.pin = pin;
    dht.alternate = TimerAlternate(htim->Instance);
    dht.state = SDHT_STATE_IDLE;
    dht.everRead = false;
    dht.status = SDHT_STATUS_NONE;
    dht.hasData = false;

    PinReleaseToTimer();
    return true;
}

// us 뒤에 카운터가 넘쳐서 업데이트 인터럽트가 오도록 맞춤
// (캡처 값은 두 값의 차이로만 쓰므로 카운터를 옮겨도 영향 없음)
static void ScheduleOverflow(uint32_t us)
{
    __HAL_TIM_SET_COUNTER(dht.htim, SDHT_COUNTER_PERIOD - us);
    __HAL_TIM_CLEAR_FLAG(dht.htim, TIM_FLAG_UPDATE);
}

bool SDhtStartRead(void)
{
    if (dht.htim == NULL)
    {
        SLOG_ERROR("dht not initialized");
        return false;
    }

    if (dht.state != SDHT_STATE_IDLE)
    {
        return false;
    }

    if (dht.everRead && HAL_GetTick() - dht.lastReadMs < SDHT_MIN_INTERVAL_MS)
    {
        return false;
    }

    // 시작 신호: 선을 LOW로 당기고, 20ms 뒤 업데이트 인터럽트에서 놓음
    dht.state = SDHT_STATE_START_LOW;
    PinDriveLow();
    ScheduleOverflow(SDHT_START_LOW_US);
    HAL_TIM_Base_Start_IT(dht.htim);
    return true;
}

// ---------------------------------------------------------------------------
// 인터럽트 처리 (프로젝트에서 이 두 콜백은 여기만 정의)
// ---------------------------------------------------------------------------

static SDhtStatus Decode(void)
{
    if (dht.captureCount < SDHT_EDGE_COUNT)
    {
        return SDHT_STATUS_TIMEOUT;
    }

    // captures[0] = 응답 시작, captures[1..40] = 각 비트 시작, captures[41] = 마지막 비트 끝
    uint8_t bytes[5] = {0};
    for (uint8_t i = 0; i < SDHT_DATA_BITS; ++i)
    {
        uint16_t interval = (uint16_t)(dht.captures[i + 2] - dht.captures[i + 1]);
        bytes[i / 8] <<= 1;
        if (interval > SDHT_BIT_THRESHOLD_US)
        {
            bytes[i / 8] |= 1U;
        }
    }

    if ((uint8_t)(bytes[0] + bytes[1] + bytes[2] + bytes[3]) != bytes[4])
    {
        return SDHT_STATUS_CHECKSUM;
    }

    // [습도 정수][습도 소수][온도 정수][온도 소수][체크섬], 온도 소수 최상위 비트 = 영하
    dht.humidityX10 = (uint16_t)(bytes[0] * 10U + bytes[1]);
    int16_t temperature = (int16_t)(bytes[2] * 10 + (bytes[3] & 0x7F));
    dht.temperatureX10 = (bytes[3] & 0x80) ? -temperature : temperature;

    dht.hasData = true;
    return SDHT_STATUS_OK;
}

static void FinishRead(void)
{
    // 채널을 먼저 꺼야 Base_Stop에서 카운터까지 멈춤
    HAL_TIM_IC_Stop_IT(dht.htim, dht.channel);
    HAL_TIM_Base_Stop_IT(dht.htim);

    dht.status = Decode();
    dht.lastReadMs = HAL_GetTick();
    dht.everRead = true;
    dht.state = SDHT_STATE_IDLE;
}

// 업데이트 인터럽트: 시작 신호 끝(20ms) 또는 수신 타임아웃(10ms)
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim != dht.htim)
    {
        return;
    }

    if (dht.state == SDHT_STATE_START_LOW)
    {
        // 선을 놓자마자 캡처 시작 (센서는 놓은 뒤 20~40us 후에 응답)
        dht.captureCount = 0;
        dht.state = SDHT_STATE_RECEIVING;
        ScheduleOverflow(SDHT_RECEIVE_TIMEOUT_US);
        PinReleaseToTimer();
        HAL_TIM_IC_Start_IT(dht.htim, dht.channel);
    }
    else if (dht.state == SDHT_STATE_RECEIVING)
    {
        FinishRead();
    }
}

// 캡처 인터럽트: 하강 엣지마다 값 저장, 다 모이면 해석
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (htim != dht.htim || dht.state != SDHT_STATE_RECEIVING)
    {
        return;
    }

    dht.captures[dht.captureCount++] = (uint16_t)HAL_TIM_ReadCapturedValue(htim, dht.channel);
    if (dht.captureCount >= SDHT_EDGE_COUNT)
    {
        FinishRead();
    }
}

// ---------------------------------------------------------------------------
// getter
// ---------------------------------------------------------------------------

bool SDhtIsBusy(void)
{
    return dht.state != SDHT_STATE_IDLE;
}

SDhtStatus SDhtGetStatus(void)
{
    return dht.status;
}

bool SDhtHasData(void)
{
    return dht.hasData;
}

int16_t SDhtGetTemperature(void)
{
    return dht.temperatureX10;
}

uint16_t SDhtGetHumidity(void)
{
    return dht.humidityX10;
}
