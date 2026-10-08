#include "RPacketDhtQuery.h"

#include "IoTPacketCodec.h"
#include "RDatabaseQuery.h"
#include "RLog.h"
#include "RNetwork.h"
#include "RPacket.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int SendDhtRow(const DatabaseRow *row, void *context);
static int ReadDatabaseFloat(const char *text, float *value);

int RPacketDhtAllReceive(RSession *session, const uint8_t *payload, size_t length)
{
    uint64_t rowCount = 0;
    (void)payload;
    if(session == NULL || session->type != SESSION_TCP || !session->authenticated || length != 0)
        return -1;

    // 사용자 제공 스키마 dht(id, temp, humi, updated_at)의 모든 행을 조회합니다.
    // 회원 유형은 PC 테이블의 보드 종류 표시를 위해 함께 읽습니다.
    const int SUCCEEDED = ExecuteDatabaseQuery(QUERY_SELECT_DHT_ALL, NULL, 0, SendDhtRow, session, &rowCount) == 0;
    if(!SUCCEEDED)
        RLOG_WARN("[%s] DHT all SELECT failed", session->label);
    return RPacketSendAck(session, REQ_DHT_ALL, SUCCEEDED);
}

static int SendDhtRow(const DatabaseRow *row, void *context)
{
    RSession *session = (RSession *)context;
    DhtRowData data = {0};
    uint8_t frame[HEADER_SIZE + DHT_ROW_DATA_SIZE];
    size_t idLength;
    size_t frameLength;
    if(row == NULL || row->columnCount != 5)
        return -1;
    // ExecuteDatabaseQuery는 데이터보다 먼저 열 이름만 있는 행을 전달합니다.
    if(row->values == NULL)
        return 0;
    if(row->values[0] == NULL || row->values[1] == NULL || row->values[2] == NULL)
        return -1;
    idLength = strlen(row->values[0]);
    if(idLength == 0 || idLength > MEM_ID_SIZE || ReadDatabaseFloat(row->values[1], &data.temp) != 0 || ReadDatabaseFloat(row->values[2], &data.humi) != 0)
        return -1;
    memcpy(data.id, row->values[0], idLength);
    if(row->values[3] != NULL)
    {
        if(strlen(row->values[3]) != DHT_TIMESTAMP_SIZE)
            return -1;
        memcpy(data.updatedAt, row->values[3], DHT_TIMESTAMP_SIZE);
    }
    if(row->values[4] != NULL)
    {
        if(strcmp(row->values[4], "STM32") == 0)
            data.memberType = DHT_MEMBER_STM32;
        else if(strcmp(row->values[4], "ARDUINO") == 0)
            data.memberType = DHT_MEMBER_ARDUINO;
        else if(strcmp(row->values[4], "PC") == 0)
            data.memberType = DHT_MEMBER_PC;
    }
    frameLength = MakeDhtRowPacket(frame, sizeof(frame), &data);
    return frameLength > 0 && RNetSend(session->fd, frame, frameLength) == 0 ? 0 : -1;
}

static int ReadDatabaseFloat(const char *text, float *value)
{
    char *end = NULL;
    errno = 0;
    *value = strtof(text, &end);
    return errno == 0 && end != text && *end == '\0' && isfinite(*value) ? 0 : -1;
}
