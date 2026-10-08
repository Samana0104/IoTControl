#pragma once

#include "RSession.h"

// 인증된 PC 요청: 현재 인증된 STM32/Arduino 세션을 ID로 묶어 전송합니다.
int RPacketSessionAllReceive(RSession *session, const uint8_t *payload, size_t length);
