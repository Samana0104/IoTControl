#pragma once

#include "RSession.h"

// TCP REQ_BT_REGISTER (로그인 후): HC-05 PIN 페어링 → DB 등록 → 서버가 BT 연결. ACK_BT_REGISTER 응답, 실패하면 -1(연결 닫힘)
int RPacketBtRegisterReceive(RSession *session, const uint8_t *payload, size_t length);
// TCP REQ_BT_CONNECT (로그인 불필요): ID/비밀번호와 DB의 ID-MAC 바인딩을 확인한 뒤 서버가 BT 연결. ACK_BT_CONNECT 응답
int RPacketBtConnectReceive(RSession *session, const uint8_t *payload, size_t length);

// DB에 등록된 회원의 HC-05에 서버가 연결 (콘솔 bt connect, 등록 직후)
// 0: 연결됨, 1: DB에 등록 안 됨, -1: 실패 (errno 설정)
int RPacketBtConnectMember(const char *memberId);
// 콘솔 bt pair: 회원 확인 → HC-05 PIN 페어링 → DB 등록 → 서버가 BT 연결
// mac은 "AA:BB:CC:DD:EE:FF", pin은 숫자. 0: 등록·연결 완료, -1: 실패 (로그에 이유)
int RPacketBtRegisterMember(const char *memberId, const char *mac, const char *pin);
