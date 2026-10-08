#pragma once

#include <main.h>

// 서버 → 장치 REQ_FAN: 팬 속도(0..100%) 적용 후 ACK_FAN으로 결과 응답
void SPacketFanRequestReceive(const uint8_t *payload, uint16_t length);
