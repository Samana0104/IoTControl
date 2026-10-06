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
bool SIntervalMSElapsed(SIntervalMS *interval, uint32_t nowMs);
