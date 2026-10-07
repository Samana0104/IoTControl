#include "RPacket.h"
#include "IoTPacketCodec.h"
#include "RLog.h"
#include "RPacketBt.h"
#include "RPacketCon.h"
#include "RPacketDht.h"
#include "RPacketFan.h"
#include "RPacketMember.h"

static int ValidatePacketPermission(RSession *session, uint16_t cmd);

int RPacketProcess(RSession *session, uint16_t cmd, const uint8_t *payload, size_t length)
{
    if(CheckPacketLength(cmd, length) != 0)
    {
        RLOG_WARN("Unknown command or invalid length from %s: cmd=0x%04X, length=%zu", RSessionGetLabel(session), (unsigned int)cmd, length);
        return -1;
    }
    if(ValidatePacketPermission(session, cmd) != 0)
    {
        return -1;
    }

    switch(cmd)
    {
        case REQ_LOGIN: return RPacketLoginReceive(session, payload, length);
        case REQ_BT_REGISTER: return RPacketBtRegisterReceive(session, payload, length);
        case REQ_BT_CONNECT: return RPacketBtConnectReceive(session, payload, length);
        case NFY_CHAT:
            RLOG_INFO("[%s] %.*s", RSessionGetLabel(session), (int)length, (const char *)payload);
            return 0;
        case NFY_DHT: return RPacketDhtReceive(session, payload, length);
        case NFY_FAN: return RPacketFanReceive(session, payload, length);
        case NFY_CON: return RPacketConReceive(session, payload, length);
        case ACK_FAN: return RPacketFanReceiveAck(session, payload, length);
        default:
            RLOG_WARN("Unsupported command from %s: cmd=0x%04X", RSessionGetLabel(session), (unsigned int)cmd);
            return -1;
    }
}

int RPacketSendAck(RSession *session, uint16_t reqCmd, int succeeded)
{
    uint8_t frame[HEADER_SIZE + RESULT_DATA_SIZE];

    MakeAckPacket(frame, sizeof(frame), reqCmd, succeeded ? RESULT_SUCCESS : RESULT_FAIL);
    return RSessionSendFrame(session, frame, sizeof(frame));
}

static int ValidatePacketPermission(RSession *session, uint16_t cmd)
{
    if(RSessionGetType(session) == SESSION_BLUETOOTH)
    {
        // BT 링크는 이미 등록된 회원/MAC에 묶여 있으므로 관리 명령을 받지 않음
        if(cmd == REQ_LOGIN || cmd == REQ_BT_REGISTER || cmd == REQ_BT_CONNECT)
        {
            RLOG_WARN("Management command not allowed from %s: cmd=0x%04X", RSessionGetLabel(session), (unsigned int)cmd);
            return -1;
        }
        return 0;
    }
    if(cmd != REQ_LOGIN && cmd != REQ_BT_CONNECT && !RSessionIsAuthenticated(session))
    {
        RLOG_WARN("Unauthenticated command from %s: cmd=0x%04X", RSessionGetLabel(session), (unsigned int)cmd);
        return -1;
    }
    // 서버는 TCP 클라이언트에 REQ를 보내지 않으므로 ACK가 올 이유가 없음
    if(IS_ACK(cmd))
    {
        RLOG_WARN("Unexpected ACK from %s: cmd=0x%04X", RSessionGetLabel(session), (unsigned int)cmd);
        return -1;
    }
    return 0;
}
