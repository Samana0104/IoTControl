#pragma once

#include "RCtrl.h"

// 장치 → 서버 NFY_FAN: 팬 속도를 fan 행(singleton_id=1)에 기록
int RCtrlFanReceive(const RCtrlContext *context, const uint8_t *payload, size_t length);
// 장치 → 서버 ACK_FAN: RCtrlFanSetSpeed 요청의 처리 결과
int RCtrlFanReceiveAck(const RCtrlContext *context, const uint8_t *payload, size_t length);

// 서버 → 장치 REQ_FAN: fd 세션의 장치 팬 속도를 0..100%로 설정
// 0: 전송함 (결과는 ACK_FAN으로 도착), 1: 그 fd의 연결된 세션 없음, -1: 잘못된 값 또는 전송 실패
int RCtrlFanSetSpeed(int fd, uint8_t percent);
