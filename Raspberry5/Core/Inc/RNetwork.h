#pragma once

#include "RSession.h"

#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>

#define NET_MAX_WORKERS 16

// CRC 검사를 통과한 프레임 하나. 그 연결을 맡은 워커에서 호출되며, 같은 연결의 프레임은 순서대로 하나씩 옴.
// payload는 호출 동안만 유효. 0: 계속, -1: 이미 보낸(큐에 넣은) 데이터를 내보낸 뒤 연결 닫기
typedef int (*RNetFrameHandler)(RSession *session, uint16_t cmd, const uint8_t *payload, size_t length);

// epoll 워커 workerCount개 시작. 워커가 송수신과 handler 호출을 모두 담당.
int RNetStart(RNetFrameHandler handler, int workerCount);
// 모든 연결을 닫고 워커 종료까지 대기 (처리 중인 handler는 끝날 때까지 기다림)
void RNetStop(void);

// accept한 소켓을 TCP 세션으로 등록. 성공/실패 모두 fd의 소유권을 가져감. 0: 등록됨, -1: 실패
int RNetOpenTcp(int fd, const struct sockaddr_in *address);
// 이미 연결된 HC-05 RFCOMM fd를 회원 세션으로 등록. 실패해도 fd는 닫힘.
// 0: 등록됨, -1: 실패 (errno: ENOSPC 가득 참, ECANCELED 종료 중)
int RNetOpenBt(int fd, const char *memberId, const char *mac);

// 완성된 프레임(Make*Packet 결과)을 fd 연결로 보냄 (바로 못 보낸 나머지는 워커가 이어서 보냄).
// 0: 보냄 또는 큐에 넣음, 1: 그 fd의 열린 연결 없음, -1: 실패 (errno 설정)
int RNetSend(int fd, const void *frame, size_t frameLength);
// fd 연결을 닫도록 요청 (처리 중인 프레임이 있으면 끝난 뒤 닫힘). 0: 요청함, 1: 연결 없음
int RNetClose(int fd);
