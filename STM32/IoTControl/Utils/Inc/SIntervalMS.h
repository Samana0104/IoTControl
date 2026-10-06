#pragma once

#include <main.h>
#include <stdbool.h>

// 주기 실행 타이머 (아두이노 AIntervalMS의 C 버전)
typedef struct
{
    uint32_t periodMs;
    uint32_t lastMs;
} SIntervalMS;

void SIntervalMSInit(SIntervalMS *interval, uint32_t periodMs);

// 주기가 지났으면 true 반환 후 기준 시각 갱신 (uint32 오버플로에도 안전)
bool SIntervalMSElapsed(SIntervalMS *interval, uint32_t nowMs);
