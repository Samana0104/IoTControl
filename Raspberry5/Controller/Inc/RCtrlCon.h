#pragma once

#include "RCtrl.h"

// 장치 → 서버 NFY_CON: 설정 온도를 con 행(singleton_id=1)에 기록
int RCtrlConReceive(const RCtrlContext *context, const uint8_t *payload, size_t length);
