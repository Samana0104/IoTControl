#pragma once

#include "IoTPacket.h"

// 장치 → 서버: DHT 측정값을 회원의 dht 행에 기록, label은 로그용 접속 표시(IP 등)
void RCtrlDhtReceive(const char *label, const char *memberId, const DhtData *data);
