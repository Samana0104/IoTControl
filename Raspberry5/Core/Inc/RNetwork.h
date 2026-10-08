#pragma once

#include "RSession.h"

#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>

#define NET_MAX_WORKERS 16

// CRC 검사를 통과한 프레임 하나. 그 연결을 맡은 워커에서 호출되며, 같은 연결의 프레임은 순서대로 하나씩 옴.
// payload는 호출 동안만 유효. 0: 계속, -1: 이미 보낸(큐에 넣은) 데이터를 내보낸 뒤 연결 닫기
typedef int (*RNetFrameHandler)(RSession *session, uint16_t cmd, const uint8_t *payload, size_t length);

// 서버 내부 연결 참조. fd 재사용 후 다른 연결에 송신/종료하지 않도록 generation을 함께 확인.
typedef struct _RNetReference
{
    int fd;
    uint64_t token;
} RNetReference;

typedef void (*RNetMaintenanceHandler)(uint64_t nowMs);
// RNetStart 전에만 설정. 네트워크 잠금을 풀고 약 500ms마다 워커 하나에서 호출.
int RNetSetMaintenanceHandler(RNetMaintenanceHandler handler);
int RNetGetSessionReference(const RSessionSnapshot *snapshot, RNetReference *reference);
int RNetIsReferenceOpen(const RNetReference *reference);
int RNetSendReferenced(const RNetReference *reference, const void *frame, size_t frameLength);
int RNetCloseReferenced(const RNetReference *reference);

// epoll 워커 workerCount개 시작. 워커가 송수신과 handler 호출을 모두 담당.
int RNetStart(RNetFrameHandler handler, int workerCount);
// 모든 연결을 닫고 워커 종료까지 대기 (처리 중인 handler는 끝날 때까지 기다림)
void RNetStop(void);

// accept한 소켓을 TCP 세션으로 등록. 성공/실패 모두 fd의 소유권을 가져감. 0: 등록됨, -1: 실패
int RNetOpenTcp(int fd, const struct sockaddr_in *address);
// 이미 연결된 HC-05 RFCOMM fd를 회원 세션으로 등록. 실패해도 fd는 닫힘.
// 0: 등록됨, -1: 실패 (errno: ENOSPC 가득 참, ECANCELED 종료 중)
int RNetOpenBt(int fd, const char *memberId, const char *mac, RMemberType memberType);

// 완성된 프레임(Make*Packet 결과)을 fd 연결로 보냄 (바로 못 보낸 나머지는 워커가 이어서 보냄).
// 0: 보냄 또는 큐에 넣음, 1: 그 fd의 열린 연결 없음, -1: 실패 (errno 설정)
int RNetSend(int fd, const void *frame, size_t frameLength);
// fd 연결을 닫도록 요청 (처리 중인 프레임이 있으면 끝난 뒤 닫힘). 0: 요청함, 1: 연결 없음
int RNetClose(int fd);
