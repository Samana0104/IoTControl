#pragma once

#include "RConfig.h"
#include "RSession.h"

#include <openssl/ssl.h>
#include <stddef.h>

// 논블로킹 fd 하나의 전송 방식. TCP는 TLS, BT는 RFCOMM 평문.
typedef struct _RNetLink
{
    RSessionType type;
    int fd;
    SSL *tls; // SESSION_TCP 전용
} RNetLink;

// 인증서/개인 키 경로(Config/TLSConfig.json)로 서버용 TLS 컨텍스트 생성 (TLS 1.2 이상). 실패하면 NULL (로그 남김)
SSL_CTX *RNetLinkCreateTlsContext(const RTlsConfig *tls);

// TLS 핸드셰이크 진행. 1: 완료, 0: 더 기다림, -1: 실패
// *wantWrite: 1이면 쓰기 가능을 기다려야 함 (아니면 읽기 가능)
int RNetLinkHandshake(RNetLink *link, int *wantWrite);
// >0: 읽은 바이트 수, 0: 지금 읽을 것 없음, -1: 끊김/오류. *wantWrite는 RNetLinkHandshake와 같음
int RNetLinkRead(RNetLink *link, void *buffer, size_t size, int *wantWrite);
// >0: 보낸 바이트 수, 0: 지금 보낼 수 없음, -1: 오류
// TLS는 0을 받은 뒤 같은 길이로 다시 호출해야 함 (버퍼 이동은 허용)
int RNetLinkWrite(RNetLink *link, const void *buffer, size_t length);
// 연결 즉시 중단 요청. 그 fd의 epoll에 끊김 이벤트가 생김
void RNetLinkShutdown(RNetLink *link);
// fd와 TLS 해제
void RNetLinkClose(RNetLink *link);
