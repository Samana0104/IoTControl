#pragma once

#include "RSession.h"

#include <stddef.h>
#include <stdint.h>

// 인증된 TCP 클라이언트의 전체 조회 요청: SELECT 결과 행 + 완료 ACK.
int RPacketDhtAllReceive(RSession *session, const uint8_t *payload, size_t length);
