#pragma once

#include "IoTPacket.h"
#include "RBluetooth.h"
#include "RThreadPool.h"

#include <netinet/in.h>
#include <openssl/ssl.h>
#include <stddef.h>
#include <stdint.h>

// TCP + BT 세션을 합친 최대 개수
#define MAX_SESSION 64
// IP(INET_ADDRSTRLEN)와 BT MAC 문자열을 모두 담는 크기
#define SESSION_ADDRESS_SIZE BLUETOOTH_MAC_TEXT_SIZE

typedef enum
{
    SESSION_TCP,
    SESSION_BLUETOOTH
} RSessionType;

// TCP(TLS) 연결이든 서버가 연 HC-05 RFCOMM 연결이든 fd 하나 = 세션 하나.
// 모든 세션의 송수신은 epoll I/O 스레드 하나가 처리함.
typedef struct _RSession RSession;

// CRC 검사를 통과한 프레임 하나를 스레드풀 워커에서 처리. 같은 세션의 프레임은 순서대로 하나씩 전달됨.
// payload는 호출 동안만 유효. 0: 계속, -1: 이미 넣은 송신을 보낸 뒤 세션 닫기
typedef int (*RSessionHandler)(RSession *session, uint16_t cmd, const uint8_t *payload, size_t length);

typedef struct _RSessionSnapshot
{
    int index;
    RSessionType type;
    int fd;
    int connected;
    int authenticated;
    char address[SESSION_ADDRESS_SIZE]; // TCP: IP, BT: MAC
    char memberId[MEM_ID_SIZE + 1];
} RSessionSnapshot;

/* ---- 세션 테이블 ---- */

// 세션 테이블과 epoll I/O 스레드 시작. handler는 pool의 워커에서 실행됨.
int RSessionInit(RSessionHandler handler, RThreadPool *pool);
// 모든 세션을 닫고 처리 중인 작업이 끝날 때까지 기다린 뒤 I/O 스레드 종료. pool은 그 다음에 멈춤.
void RSessionCloseAll(void);

// accept한 소켓과 SSL_set_fd까지 마친 tls를 세션으로 등록. TLS 핸드셰이크는 I/O 스레드가 진행.
// 성공/실패 모두 fd와 tls의 소유권을 가져감. 0: 등록됨, -1: 세션 가득 참 또는 실패
int RSessionOpenTcp(int fd, SSL *tls, const struct sockaddr_in *address);
// 회원의 HC-05(mac은 DB에 등록된 값)에 RFCOMM 연결하고 세션으로 등록 (연결하는 동안 호출 스레드가 대기).
// 같은 회원이 같은 MAC으로 이미 연결돼 있으면 그대로 성공.
// 성공: 세션 fd, -1: 실패 (errno: ENOSPC 가득 참, EADDRINUSE 다른 회원이 사용 중, ECANCELED 종료 중 등)
int RSessionOpenBt(const char *memberId, const char *mac);

// 완성된 프레임(Make*Packet 결과)을 fd 세션의 송신 큐에 넣음.
// 0: 넣음 (I/O 스레드가 전송), 1: 그 fd의 연결된 세션 없음, -1: 실패 (errno 설정)
int RSessionSend(int fd, const uint8_t *frame, size_t frameLength);
// 회원의 연결된 BT 세션 fd, 없으면 -1
int RSessionFindBtFd(const char *memberId);

// snapshots는 MAX_SESSION개를 담을 수 있어야 함. 채운 개수를 반환.
size_t RSessionGetSnapshots(RSessionSnapshot *snapshots);

/* ---- 세션 하나 (RSessionHandler 안에서 사용) ---- */

// 프레임을 이 세션의 송신 큐에 넣음. 0: 넣음, -1: 닫힘(ENOTCONN) 또는 큐 가득 참(ENOBUFS)
int RSessionSendFrame(RSession *session, const void *frame, size_t frameLength);

RSessionType RSessionGetType(const RSession *session);
int RSessionGetFd(const RSession *session);
// 로그용 접속 표시 (TCP: IP, BT: "BT id=... mac=...")
const char *RSessionGetLabel(const RSession *session);
// TCP는 로그인 후, BT는 연결 시점부터 인증된 상태
int RSessionIsAuthenticated(RSession *session);
// 0: 인증된 회원 ID 복사, -1: 로그인 전
int RSessionGetMemberId(RSession *session, char memberId[MEM_ID_SIZE + 1]);
// TCP 로그인 결과 기록 (memberIdLength <= MEM_ID_SIZE)
void RSessionLogin(RSession *session, const char *memberId, size_t memberIdLength);
void RSessionLogout(RSession *session);
