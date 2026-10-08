#pragma once

#include <main.h>

// 서버 → 장치 REQ_DHT: 마지막으로 성공한 측정값을 ACK_DHT로 응답 (0.1 단위 → 정수 단위)
// 마지막 읽기가 실패했거나 아직 값이 없으면 RESULT_FAIL
void SPacketDhtRequestReceive(const uint8_t *payload, uint16_t length);
