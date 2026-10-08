#pragma once

#include "RSession.h"

// 장치 → 서버 NFY_DHT: 측정값을 dht 테이블에 한 행 추가
int RPacketDhtReceive(RSession *session, const uint8_t *payload, size_t length);
// 장치 → 서버 ACK_DHT: REQ_DHT 응답. 성공이면 NFY_DHT와 같이 dht 행 추가
int RPacketDhtReceiveAck(RSession *session, const uint8_t *payload, size_t length);
// PC → 서버 REQ_DHT_COLLECT: 모든 장치에 REQ_DHT를 보내고 ACK_DHT_COLLECT로 응답
int RPacketDhtCollectReceive(RSession *session, const uint8_t *payload, size_t length);

// 인증된 STM32/Arduino에 ID별로 REQ_DHT 전송. 이미 갱신 중인 ID와 excludeFd는 건너뜀 (-1이면 없음)
// 보낸 장치 수 반환. 측정값은 각 장치의 ACK_DHT로 도착
int RPacketDhtRequestAll(int excludeFd);
