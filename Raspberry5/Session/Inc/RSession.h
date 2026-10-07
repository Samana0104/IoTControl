#pragma once

#include "IoTPacket.h"
#include "RBluetooth.h"

#include <stddef.h>

// 동시에 연결된 TCP + BT 세션 최대 개수
#define MAX_SESSION 64
// IP(INET_ADDRSTRLEN)와 BT MAC 문자열을 모두 담는 크기
#define SESSION_ADDRESS_SIZE BLUETOOTH_MAC_TEXT_SIZE

typedef enum
{
    SESSION_TCP,
    SESSION_BLUETOOTH
} RSessionType;

#define SESSION_LABEL_SIZE 64

// 연결된 fd 하나의 정보. 송수신은 RNetwork가 담당.
// 연결을 맡은 워커(패킷 처리 함수)는 필드를 직접 읽음. memberId/authenticated 변경은 RSessionLogin/Logout으로만.
typedef struct _RSession
{
    int inUse;
    RSessionType type;
    int fd;
    char address[SESSION_ADDRESS_SIZE]; // TCP: IP, BT: MAC
    char label[SESSION_LABEL_SIZE];     // 로그용 (TCP: IP, BT: "BT id=... mac=...")
    char memberId[MEM_ID_SIZE + 1];
    int authenticated;                  // TCP는 로그인 후, BT는 등록 시점부터 1
} RSession;

typedef struct _RSessionSnapshot
{
    RSessionType type;
    int fd;
    int authenticated;
    char address[SESSION_ADDRESS_SIZE]; // TCP: IP, BT: MAC
    char memberId[MEM_ID_SIZE + 1];
} RSessionSnapshot;

// 연결이 성립된 fd를 등록. memberId != NULL이면 그 회원으로 인증된 상태 (BT). 가득 차면 NULL
RSession *RSessionAdd(RSessionType type, int fd, const char *address, const char *memberId);
// 연결을 닫은 쪽이 호출. 이후 session 포인터는 사용 금지
void RSessionRemove(RSession *session);

// 회원의 BT 세션 fd, 없으면 -1
int RSessionFindBtFd(const char *memberId);
// snapshots는 MAX_SESSION개를 담을 수 있어야 함. 채운 개수를 반환.
size_t RSessionGetSnapshots(RSessionSnapshot *snapshots);

// TCP 로그인 결과 기록 (memberIdLength <= MEM_ID_SIZE)
void RSessionLogin(RSession *session, const char *memberId, size_t memberIdLength);
void RSessionLogout(RSession *session);
