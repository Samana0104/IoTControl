#include "RPacketSession.h"
#include "IoTPacketCodec.h"
#include "RLog.h"
#include "RNetwork.h"
#include "RPacket.h"

#include <string.h>

_Static_assert(MAX_SESSION <= SESSION_MAX_ROWS, "Session packet limit must fit the server session table");

int RPacketSessionAllReceive(RSession *session, const uint8_t *payload, size_t length)
{
    RSessionSnapshot snapshots[MAX_SESSION];
    SessionRowData rows[MAX_SESSION] = {0};
    size_t rowCount = 0;
    size_t snapshotCount;

    (void)payload;
    if(session == NULL || session->type != SESSION_TCP || !session->authenticated || length != 0)
        return -1;
    if(session->memberType != MEMBER_TYPE_PC)
    {
        RLOG_WARN("[%s] Session query rejected: PC member required", session->label);
        return RPacketSendAck(session, REQ_SESSION_ALL, 0);
    }
    snapshotCount = RSessionGetSnapshots(snapshots);
    if(snapshotCount > MAX_SESSION)
        return RPacketSendAck(session, REQ_SESSION_ALL, 0);
    for(size_t index = 0; index < snapshotCount; ++index)
    {
        const RSessionSnapshot *snapshot = &snapshots[index];
        size_t rowIndex;
        size_t idLength;
        uint8_t link;

        if(!snapshot->authenticated || snapshot->memberId[0] == '\0' || (snapshot->memberType != MEMBER_TYPE_STM32 && snapshot->memberType != MEMBER_TYPE_ARDUINO))
            continue;
        if(snapshot->type != SESSION_TCP && snapshot->type != SESSION_BLUETOOTH)
            continue;
        idLength = strnlen(snapshot->memberId, MEM_ID_SIZE + 1);
        if(idLength == 0 || idLength > MEM_ID_SIZE)
            continue;
        link = snapshot->type == SESSION_TCP ? SESSION_LINK_TCP : SESSION_LINK_BT;
        for(rowIndex = 0; rowIndex < rowCount; ++rowIndex)
        {
            if(strncmp(rows[rowIndex].id, snapshot->memberId, MEM_ID_SIZE) == 0)
                break;
        }
        if(rowIndex == rowCount)
        {
            memcpy(rows[rowIndex].id, snapshot->memberId, idLength);
            rows[rowIndex].memberType = snapshot->memberType == MEMBER_TYPE_STM32 ? SESSION_MEMBER_STM32 : SESSION_MEMBER_ARDUINO;
            ++rowCount;
        }
        rows[rowIndex].links |= link;
    }
    for(size_t index = 0; index < rowCount; ++index)
    {
        uint8_t frame[HEADER_SIZE + SESSION_ROW_DATA_SIZE];
        size_t frameLength = MakeSessionRowPacket(frame, sizeof(frame), &rows[index]);

        if(frameLength == 0 || RNetSend(session->fd, frame, frameLength) != 0)
        {
            RLOG_WARN("[%s] Session query row send failed", session->label);
            return -1;
        }
    }
    RLOG_INFO("[%s] Session query: %zu unique connected field device(s)", session->label, rowCount);
    return RPacketSendAck(session, REQ_SESSION_ALL, 1);
}
