#pragma once

#include "IoTPacket.h"
#include "RBluetooth.h"

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

// TCP(TLS) 연결이든 서버가 연 HC-05 RFCOMM 연결이든 fd 하나 = 세션 하나
typedef struct _RSession RSession;

// 세션마다 수신 스레드에서 한 번 호출됨. 반환하면 그 세션을 닫음.
typedef void (*RSessionReceiver)(RSession *session);

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

// 세션 테이블 초기화. receiver는 모든 세션 공통 패킷 처리 루프.
int RSessionInit(RSessionReceiver receiver);
// 모든 세션을 끊고 워커 스레드가 끝날 때까지 대기. 이후 BT 연결은 거부.
void RSessionCloseAll(void);

// TLS 핸드셰이크가 끝난 소켓을 세션으로 등록하고 송신/수신 스레드 시작.
// 성공/실패 모두 fd와 tls의 소유권을 가져감. 0: 시작됨, -1: 세션 가득 참 또는 스레드 실패
int RSessionOpenTcp(int fd, SSL *tls, const struct sockaddr_in *address);
// 회원의 HC-05(mac은 DB에 등록된 값)에 RFCOMM 연결하고 수신 스레드 시작.
// 같은 회원이 같은 MAC으로 이미 연결돼 있으면 그대로 성공.
// 성공: 세션 fd, -1: 실패 (errno: ENOSPC 가득 참, EADDRINUSE 다른 회원이 사용 중, ECANCELED 종료 중 등)
int RSessionOpenBt(const char *memberId, const char *mac);

// 완성된 프레임(Make*Packet 결과)을 fd의 세션으로 전송.
// 0: 전송함, 1: 그 fd의 연결된 세션 없음, -1: 전송 실패 (errno 설정)
int RSessionSend(int fd, const uint8_t *frame, size_t frameLength);
// 회원의 연결된 BT 세션 fd, 없으면 -1
int RSessionFindBtFd(const char *memberId);

// snapshots는 MAX_SESSION개를 담을 수 있어야 함. 채운 개수를 반환.
size_t RSessionGetSnapshots(RSessionSnapshot *snapshots);

/* ---- 세션 하나 (수신 스레드의 RSessionReceiver 안에서 사용) ---- */

// 0: length만큼 수신, -1: 연결 끊김/중지
int RSessionReceive(RSession *session, void *buffer, size_t length);
// 1: 데이터 있음, 0: 시간 초과, -1: 연결 끊김/중지
int RSessionWaitForData(RSession *session, int timeoutMs);
// 0: 전송함, -1: 실패
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
