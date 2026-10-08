#pragma once

#include "RSession.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// 장치 → 서버 NFY_FAN: 팬 속도를 fan 행(singleton_id=1)에 기록
int RPacketFanReceive(RSession *session, const uint8_t *payload, size_t length);
// 장치 → 서버 ACK_FAN: RPacketFanSetSpeed 요청의 처리 결과
int RPacketFanReceiveAck(RSession *session, const uint8_t *payload, size_t length);

// PC → 서버 REQ_FAN_QUERY: fan 행(singleton_id=1)의 속도를 ACK_FAN_QUERY로 응답 (장치 제어 없음)
// 행이 없거나 DB 오류면 RESULT_FAIL. 전송 실패면 -1(연결 닫힘)
int RPacketFanQueryReceive(RSession *session, const uint8_t *payload, size_t length);

// 서버 → 장치 REQ_FAN: fd 세션의 장치 팬 속도를 0..100%로 설정
// 0: 보냄 (결과는 ACK_FAN으로 도착), 1: 그 fd의 연결된 세션 없음, -1: 잘못된 값 또는 전송 실패
int RPacketFanSetSpeed(int fd, uint8_t percent);

// PC -> 서버 REQ_FAN_UPDATE: 인증된 PC가 fan(singleton_id=1)의 speed를 0..100%로 저장
// DB 저장 확인 후 ACK_FAN_UPDATE 응답. 장치 제어는 보내지 않음
<<<<<<< Updated upstream
int RPacketFanUpdateReceive(RSession *session, const uint8_t *payload, size_t length);
=======
int RPacketFanUpdateReceive(RSession *session, const uint8_t *payload, size_t length);

#ifdef __cplusplus
}
#endif
>>>>>>> Stashed changes
