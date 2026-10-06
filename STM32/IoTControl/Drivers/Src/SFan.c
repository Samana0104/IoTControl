#include "SFan.h"
#include "SLog.h"

typedef struct _SFan
{
    // Init 전에는 NULL
    TIM_HandleTypeDef *htim;
    uint32_t channel;

    uint8_t speedPercent;

    // 킥 스타트 중이면 true, 끝나면 speedPercent로 내림
    bool kicking;
    uint32_t kickStartMs;
} SFan;

static SFan fan;

// 퍼센트 -> CCR 값 (ARR은 CubeMX 설정에서 읽어서 주기를 바꿔도 그대로 동작)
static void ApplyDuty(uint8_t percent)
{
    uint32_t period = __HAL_TIM_GET_AUTORELOAD(fan.htim) + 1U;
    __HAL_TIM_SET_COMPARE(fan.htim, fan.channel, period * percent / 100U);
}

bool SFanInit(TIM_HandleTypeDef *htim, uint32_t channel)
{
    if (htim == NULL)
    {
        SLOG_ERROR("htim is NULL");
        return false;
    }

    fan.htim = htim;
    fan.channel = channel;
    fan.speedPercent = 0;
    fan.kicking = false;

    ApplyDuty(0);
    return HAL_TIM_PWM_Start(htim, channel) == HAL_OK;
}

void SFanSetSpeed(uint8_t percent)
{
    if (fan.htim == NULL)
    {
        SLOG_ERROR("fan not initialized");
        return;
    }

    if (percent < SFAN_MIN_PERCENT)
    {
        percent = 0;
    }
    else if (percent > SFAN_MAX_PERCENT)
    {
        percent = SFAN_MAX_PERCENT;
    }

    bool wasStopped = (fan.speedPercent == 0);
    fan.speedPercent = percent;

    if (percent == 0)
    {
        fan.kicking = false;
        ApplyDuty(0);
        return;
    }

    // 멈춰 있다 출발하면 잠깐 최대로 돌려서 확실히 출발시킴
    if (wasStopped)
    {
        fan.kicking = true;
        fan.kickStartMs = HAL_GetTick();
        ApplyDuty(SFAN_MAX_PERCENT);
        return;
    }

    // 킥 중에 속도가 바뀌면 킥이 끝날 때 새 값으로 내려감
    if (!fan.kicking)
    {
        ApplyDuty(percent);
    }
}

uint8_t SFanGetSpeed(void)
{
    return fan.speedPercent;
}

void SFanStop(void)
{
    SFanSetSpeed(0);
}

void SFanUpdate(void)
{
    if (!fan.kicking || HAL_GetTick() - fan.kickStartMs < SFAN_KICK_MS)
    {
        return;
    }

    fan.kicking = false;
    ApplyDuty(fan.speedPercent);
}
