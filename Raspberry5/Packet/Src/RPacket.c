#include "RPacket.h"
#include "IoTPacketCodec.h"
#include "RLog.h"
#include "RNetwork.h"
#include "RPacketBt.h"
#include "RPacketCon.h"
#include "RPacketDht.h"
#include "RPacketDhtQuery.h"
#include "RPacketFan.h"
#include "RPacketMember.h"

// 패킷을 받을 수 있는 세션 (PACKET_FLAG_*를 OR로 조합)
#define PACKET_FLAG_TCP 0x01      // TCP 클라이언트에서 받음
#define PACKET_FLAG_BT 0x02       // BT 장치에서 받음
#define PACKET_FLAG_NO_LOGIN 0x04 // TCP 로그인 전에도 받음 (BT는 등록 시점부터 인증됨)

typedef int (*RPacketHandler)(RSession *session, const uint8_t *payload, size_t length);

typedef struct _RPacketEntry
{
    uint16_t cmd;
    uint8_t flags;
    RPacketHandler handler;
} RPacketEntry;

static int ReceiveChat(RSession *session, const uint8_t *payload, size_t length);
static const RPacketEntry *FindPacketEntry(uint16_t cmd);
static int ValidatePacketPermission(const RSession *session, const RPacketEntry *entry);

// 수신 cmd → 처리 함수. 길이 검사는 CheckPacketLength가 먼저 함.
// 서버가 REQ를 보내는 곳은 BT 장치(REQ_FAN, REQ_DHT)와 Wi-Fi 장치(REQ_DHT)뿐이므로 ACK도 그 경로만 허용
static const RPacketEntry PACKET_TABLE[] =
{
    {REQ_LOGIN, PACKET_FLAG_TCP | PACKET_FLAG_NO_LOGIN, RPacketLoginReceive},
    {REQ_BT_CONNECT, PACKET_FLAG_TCP | PACKET_FLAG_NO_LOGIN, RPacketBtConnectReceive},
    {REQ_BT_REGISTER, PACKET_FLAG_TCP, RPacketBtRegisterReceive},
    {REQ_DHT_ALL, PACKET_FLAG_TCP, RPacketDhtAllReceive},
    {REQ_DHT_COLLECT, PACKET_FLAG_TCP, RPacketDhtCollectReceive},
    {REQ_FAN_QUERY, PACKET_FLAG_TCP, RPacketFanQueryReceive},
    {NFY_CHAT, PACKET_FLAG_TCP | PACKET_FLAG_BT, ReceiveChat},
    {NFY_DHT, PACKET_FLAG_TCP | PACKET_FLAG_BT, RPacketDhtReceive},
    {NFY_FAN, PACKET_FLAG_TCP | PACKET_FLAG_BT, RPacketFanReceive},
    {NFY_CON, PACKET_FLAG_TCP | PACKET_FLAG_BT, RPacketConReceive},
    {ACK_FAN, PACKET_FLAG_BT, RPacketFanReceiveAck},
    {ACK_DHT, PACKET_FLAG_TCP | PACKET_FLAG_BT, RPacketDhtReceiveAck}
};

#define PACKET_TABLE_COUNT (sizeof(PACKET_TABLE) / sizeof(PACKET_TABLE[0]))

int RPacketProcess(RSession *session, uint16_t cmd, const uint8_t *payload, size_t length)
{
    const RPacketEntry *entry;

    if(session == NULL || (payload == NULL && length > 0))
    {
        RLOG_ERROR("RPacketProcess: NULL argument: cmd=0x%04X", (unsigned int)cmd);
        return -1;
    }
    if(CheckPacketLength(cmd, length) != 0)
    {
        RLOG_WARN("Unknown command or invalid length from %s: cmd=0x%04X, length=%zu", session->label, (unsigned int)cmd, length);
        return -1;
    }
    entry = FindPacketEntry(cmd);
    if(entry == NULL)
    {
        RLOG_WARN("Unsupported command from %s: cmd=0x%04X", session->label, (unsigned int)cmd);
        return -1;
    }
    if(ValidatePacketPermission(session, entry) != 0)
    {
        return -1;
    }
    return entry->handler(session, payload, length);
}

int RPacketSendAck(RSession *session, uint16_t reqCmd, int succeeded)
{
    uint8_t frame[HEADER_SIZE + RESULT_DATA_SIZE];
    size_t frameLength;

    if(session == NULL)
    {
        RLOG_ERROR("RPacketSendAck: NULL session");
        return -1;
    }
    frameLength = MakeAckPacket(frame, sizeof(frame), reqCmd, succeeded ? RESULT_SUCCESS : RESULT_FAIL);
    if(frameLength == 0)
    {
        RLOG_ERROR("[%s] ACK frame build failed: cmd=0x%04X", session->label, (unsigned int)reqCmd);
        return -1;
    }
    if(RNetSend(session->fd, frame, frameLength) != 0)
    {
        RLOG_WARN("[%s] ACK send failed: cmd=0x%04X", session->label, (unsigned int)reqCmd);
        return -1;
    }
    return 0;
}

static int ReceiveChat(RSession *session, const uint8_t *payload, size_t length)
{
    RLOG_INFO("[%s] %.*s", session->label, (int)length, (const char *)payload);
    return 0;
}

static const RPacketEntry *FindPacketEntry(uint16_t cmd)
{
    for(size_t index = 0; index < PACKET_TABLE_COUNT; ++index)
    {
        if(PACKET_TABLE[index].cmd == cmd)
        {
            return &PACKET_TABLE[index];
        }
    }
    return NULL;
}

static int ValidatePacketPermission(const RSession *session, const RPacketEntry *entry)
{
    uint8_t sessionFlag = session->type == SESSION_BLUETOOTH ? PACKET_FLAG_BT : PACKET_FLAG_TCP;

    if((entry->flags & sessionFlag) == 0)
    {
        RLOG_WARN("Command not allowed from %s: cmd=0x%04X", session->label, (unsigned int)entry->cmd);
        return -1;
    }
    if(session->type == SESSION_TCP && !session->authenticated && (entry->flags & PACKET_FLAG_NO_LOGIN) == 0)
    {
        RLOG_WARN("Unauthenticated command from %s: cmd=0x%04X", session->label, (unsigned int)entry->cmd);
        return -1;
    }
    return 0;
}
