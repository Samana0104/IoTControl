#pragma once

#include "IoTPacket.h"

// 장치 → 서버: 설정 온도를 con 행(singleton_id=1)에 기록, label은 로그용 접속 표시(IP 등)
void RCtrlConReceive(const char *label, const ConData *data);
