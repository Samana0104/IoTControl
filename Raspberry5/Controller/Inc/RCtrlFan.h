#pragma once

#include "IoTPacket.h"

// 장치 → 서버: 팬 속도를 fan 행(singleton_id=1)에 기록, label은 로그용 접속 표시(IP 등)
void RCtrlFanReceive(const char *label, const FanData *data);
