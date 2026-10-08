#include "RPacketMember.h"
#include "IoTPacketCodec.h"
#include "RDatabase.h"
#include "RLog.h"
#include "RPacket.h"

#include <sodium.h>
#include <string.h>

int RPacketLoginReceive(RSession *session, const uint8_t *payload, size_t length)
{
    MemData memData;
    char memberId[MEM_ID_SIZE + 1];
    size_t memberIdLength;
    size_t passwordLength;
    int verifyResult;
    RMemberType memberType;

    if(session == NULL || payload == NULL)
    {
        RLOG_ERROR("RPacketLoginReceive: NULL argument");
        return -1;
    }
    if(ReadMemData(payload, length, &memData) != 0)
    {
        RLOG_WARN("[%s] Malformed login payload: length=%zu", session->label, length);
        return -1;
    }
    memberIdLength = strnlen(memData.id, MEM_ID_SIZE);
    passwordLength = strnlen(memData.pw, MEM_PW_SIZE);
    verifyResult = VerifyMember(memData.id, memberIdLength, memData.pw, passwordLength);
    memcpy(memberId, memData.id, memberIdLength);
    memberId[memberIdLength] = '\0';
    sodium_memzero(&memData, sizeof(memData));

    if(verifyResult != 1)
    {
        RSessionLogout(session);
        RLOG_WARN("[%s] Member authentication failed", session->label);
        RPacketSendAck(session, REQ_LOGIN, 0);
        return -1;
    }

    memberType = RPacketReadMemberType(memberId);
    RSessionLogin(session, memberId, memberIdLength, memberType);
    RLOG_INFO("[%s] Member authenticated: id=%s, type=%s", session->label, memberId, RSessionMemberTypeName(memberType));
    return RPacketSendAck(session, REQ_LOGIN, 1);
}
