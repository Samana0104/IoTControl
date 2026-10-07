#pragma once

#include "RSession.h"

#include <stddef.h>
#include <stdint.h>

// RNetFrameHandler: RNetwork 워커가 CRC 검사를 마친 프레임 하나를 넘김.
// 길이/권한을 확인하고 cmd별 RPacket* 함수로 분배. -1이면 남은 송신 후 연결을 닫음.
int RPacketProcess(RSession *session, uint16_t cmd, const uint8_t *payload, size_t length);

// reqCmd(REQ_*)에 대한 ACK를 이 세션으로 보냄. 0: 보냄(또는 큐에 넣음), -1: 실패
int RPacketSendAck(RSession *session, uint16_t reqCmd, int succeeded);
