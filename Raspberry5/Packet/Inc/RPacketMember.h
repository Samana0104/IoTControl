#pragma once

#include "RSession.h"

// TCP REQ_LOGIN: 회원 인증 후 세션에 회원 ID 기록, ACK_LOGIN 응답. 실패하면 실패 ACK 후 -1(연결 닫힘)
int RPacketLoginReceive(RSession *session, const uint8_t *payload, size_t length);
