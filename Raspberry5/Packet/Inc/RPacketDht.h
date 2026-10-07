#pragma once

#include "RSession.h"

// 장치 → 서버 NFY_DHT: 측정값을 회원의 dht 행에 기록
int RPacketDhtReceive(RSession *session, const uint8_t *payload, size_t length);
