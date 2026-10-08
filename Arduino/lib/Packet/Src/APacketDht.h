#pragma once

#include <ADht.h>

// REQ_DHT 처리에 쓸 센서 연결 (APacketBegin에서 호출)
void APacketDhtBegin(ADht &dht);

// 서버 → 장치 REQ_DHT: 요청 시점에 DHT11을 읽어 ACK_DHT로 응답, 읽기 실패(NaN)면 RESULT_FAIL
void APacketDhtRequestReceive(const uint8_t *payload, uint16_t length);
