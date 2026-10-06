#pragma once

#include <main.h>


// 이보다 낮은 속도는 모터가 못 돌아서 0%로 처리 (실측 후 조정)
#define SFAN_MIN_PERCENT 25

// 모터 정격보다 높은 전압으로 돌릴 때 듀티 상한 (3V 모터를 5V로 돌리면 60 정도)
#define SFAN_MAX_PERCENT 100

// 멈춘 상태에서 출발할 때 이 시간 동안 최대 듀티로 돌린 뒤 목표 속도로 내림
#define SFAN_KICK_MS 300

// PWM 타이머/채널 연결 + PWM 시작 (팬은 정지 상태로 시작)
bool SFanInit(TIM_HandleTypeDef *htim, uint32_t channel);

// 속도 0~100% (MIN 미만은 정지, MAX 초과는 MAX로 제한)
void SFanSetSpeed(uint8_t percent);
uint8_t SFanGetSpeed(void);

void SFanStop(void);

// 킥 스타트 시간 처리 (메인 루프에서 매번 호출)
void SFanUpdate(void);
