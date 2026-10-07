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

    if(ReadMemData(payload, length, &memData) != 0)
    {
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
        RLOG_WARN("[%s] Member authentication failed", RSessionGetLabel(session));
        RPacketSendAck(session, REQ_LOGIN, 0);
        return -1;
    }

    RSessionLogin(session, memberId, memberIdLength);
    RLOG_INFO("[%s] Member authenticated: id=%s", RSessionGetLabel(session), memberId);
    return RPacketSendAck(session, REQ_LOGIN, 1);
}
