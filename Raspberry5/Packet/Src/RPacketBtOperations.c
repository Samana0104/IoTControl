#include "RPacketBtOperations.h"
#include "IoTPacketCodec.h"
#include "RBluetooth.h"
#include "RDatabaseQuery.h"
#include "RLog.h"
#include "RNetwork.h"
#include "RPacketBt.h"
#include <errno.h>
#include <pthread.h>
#include <string.h>

#define BT_SCAN_SECONDS 10
_Static_assert(BT_CONNECT_ALL_MAX_ROWS == MAX_SESSION, "BT connect limit must match server session capacity");

typedef struct _BtOperationJob
{
    RNetReference requester;
    uint16_t command;
} BtOperationJob;
typedef struct _BtRegisteredIds
{
    char ids[BT_CONNECT_ALL_MAX_ROWS][MEM_ID_SIZE + 1];
    size_t count;
    int truncated;
} BtRegisteredIds;

static pthread_mutex_t operationMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t operationThread;
static int running;
static int joinable;
static int stopping;
static BtOperationJob job;
static int StartOperation(RSession *session, uint16_t command);
static int SendCompletion(const BtOperationJob *operation, uint8_t reason);
static int CollectMember(const DatabaseRow *row, void *context);
static void *RunOperation(void *argument);
static int CanContinue(const BtOperationJob *operation);
static void CopyScanName(char destination[BT_SCAN_NAME_SIZE], const char *source);
static uint8_t ConnectMemberStatus(const char *id);

void RPacketBtOperationsStart(void)
{
    pthread_mutex_lock(&operationMutex);
    stopping = 0;
    pthread_mutex_unlock(&operationMutex);
}

void RPacketBtOperationsStop(void)
{
    pthread_mutex_lock(&operationMutex);
    stopping = 1;
    const int SHOULD_JOIN = joinable;
    pthread_mutex_unlock(&operationMutex);
    if (SHOULD_JOIN)
        pthread_join(operationThread, NULL);
    pthread_mutex_lock(&operationMutex);
    joinable = 0;
    running = 0;
    pthread_mutex_unlock(&operationMutex);
}

int RPacketBtConnectAllReceive(RSession *session, const uint8_t *payload, size_t length)
{
    (void)payload;
    if (length != 0)
        return -1;
    return StartOperation(session, REQ_BT_CONNECT_ALL);
}

int RPacketBtScanReceive(RSession *session, const uint8_t *payload, size_t length)
{
    (void)payload;
    if (length != 0)
        return -1;
    return StartOperation(session, REQ_BT_SCAN);
}

static int SendCompletion(const BtOperationJob *operation, uint8_t reason)
{
    BtOperationAckData ack = {.result = reason == BT_OPERATION_OK ? RESULT_SUCCESS : RESULT_FAIL, .reason = reason};
    uint8_t frame[HEADER_SIZE + BT_OPERATION_ACK_SIZE];
    size_t length = MakeBtOperationAckPacket(frame, sizeof(frame), operation->command, &ack);
    return length > 0 && RNetSendReferenced(&operation->requester, frame, length) == 0 ? 0 : -1;
}

static int StartOperation(RSession *session, uint16_t command)
{
    RSessionSnapshot snapshot;
    BtOperationJob operation = {.command = command};
    if (session == NULL || RSessionFindByFd(session->fd, &snapshot) != 0 || RNetGetSessionReference(&snapshot, &operation.requester) != 0)
        return -1;
    if (session->type != SESSION_TCP || !session->authenticated || session->memberType != MEMBER_TYPE_PC)
        return SendCompletion(&operation, BT_OPERATION_NOT_ALLOWED);
    pthread_mutex_lock(&operationMutex);
    if (running || stopping)
    {
        pthread_mutex_unlock(&operationMutex);
        return SendCompletion(&operation, BT_OPERATION_BUSY);
    }
    // Completed threads are joined before replacing their job storage.
    if (joinable)
        pthread_join(operationThread, NULL);
    joinable = 0;
    job = operation;
    running = 1;
    if (pthread_create(&operationThread, NULL, RunOperation, NULL) != 0)
    {
        running = 0;
        pthread_mutex_unlock(&operationMutex);
        return SendCompletion(&operation, BT_OPERATION_FAILED);
    }
    joinable = 1;
    RLOG_INFO("BT operation started: requester=%s, cmd=0x%04X", session->memberId, (unsigned int)command);
    pthread_mutex_unlock(&operationMutex);
    return 0;
}

static int CanContinue(const BtOperationJob *operation)
{
    pthread_mutex_lock(&operationMutex);
    const int STOPPED = stopping;
    pthread_mutex_unlock(&operationMutex);
    return !STOPPED && RNetIsReferenceOpen(&operation->requester);
}

static int CollectMember(const DatabaseRow *row, void *context)
{
    BtRegisteredIds *registered = context;
    if (row->values == NULL)
        return 0;
    if (row->columnCount < 1 || row->values[0] == NULL)
        return -1;
    size_t length = strnlen(row->values[0], MEM_ID_SIZE + 1);
    if (length == 0 || length > MEM_ID_SIZE)
        return -1;
    if (registered->count >= BT_CONNECT_ALL_MAX_ROWS)
    {
        registered->truncated = 1;
        return 1;
    }
    memcpy(registered->ids[registered->count++], row->values[0], length + 1);
    return 0;
}

static void *RunOperation(void *argument)
{
    (void)argument;
    const BtOperationJob OPERATION = job;
    uint8_t reason = BT_OPERATION_OK;
    uint8_t frame[PACKET_FRAME_SIZE];
    if (OPERATION.command == REQ_BT_SCAN)
    {
        BluetoothScanDevice devices[BT_SCAN_MAX_ROWS];
        int count = ScanBluetoothDevices(NULL, BT_SCAN_SECONDS, devices, BT_SCAN_MAX_ROWS);
        if (count < 0)
        {
            reason = BT_OPERATION_FAILED;
            RLOG_WARN("BT scan failed: %s", strerror(errno));
        }
        else
        {
            if (count > BT_SCAN_MAX_ROWS)
            {
                count = BT_SCAN_MAX_ROWS;
                reason = BT_OPERATION_LIMIT;
            }
            // The driver caps discovery output; a full page may contain additional unseen devices.
            if (count == BT_SCAN_MAX_ROWS)
                reason = BT_OPERATION_LIMIT;
            for (int index = 0; index < count && CanContinue(&OPERATION); ++index)
            {
                BtScanRowData row = {.rssi = devices[index].rssi, .paired = devices[index].paired != 0};
                memcpy(row.mac, devices[index].mac, sizeof(row.mac));
                CopyScanName(row.name, devices[index].name);
                size_t length = MakeBtScanRowPacket(frame, sizeof(frame), &row);
                if (RNetSendReferenced(&OPERATION.requester, frame, length) != 0)
                    break;
            }
        }
    }
    else
    {
        BtRegisteredIds registered = {0};
        if (ExecuteDatabaseQuery(QUERY_SELECT_BLUETOOTH_ALL, NULL, 0, CollectMember, &registered, NULL) != 0)
            reason = BT_OPERATION_FAILED;
        else
        {
            if (registered.truncated)
                reason = BT_OPERATION_LIMIT;
            for (size_t index = 0; index < registered.count && CanContinue(&OPERATION); ++index)
            {
                const char *id = registered.ids[index];
                BtConnectRowData row = {0};
                memcpy(row.id, id, strnlen(id, MEM_ID_SIZE));
                row.status = ConnectMemberStatus(id);
                size_t length = MakeBtConnectRowPacket(frame, sizeof(frame), &row);
                if (RNetSendReferenced(&OPERATION.requester, frame, length) != 0)
                    break;
            }
        }
    }
    if (CanContinue(&OPERATION))
        SendCompletion(&OPERATION, reason);
    RLOG_INFO("BT operation finished: requester_fd=%d, cmd=0x%04X, reason=%u", OPERATION.requester.fd, (unsigned int)OPERATION.command, (unsigned int)reason);
    pthread_mutex_lock(&operationMutex);
    running = 0;
    pthread_mutex_unlock(&operationMutex);
    return NULL;
}

static void CopyScanName(char destination[BT_SCAN_NAME_SIZE], const char *source)
{
    size_t length = strnlen(source, BT_SCAN_NAME_SIZE - 1);
    if (length > 0)
    {
        size_t lastStart = length - 1;
        while (lastStart > 0 && ((uint8_t)source[lastStart] & 0xC0) == 0x80)
            --lastStart;
        uint8_t lead = (uint8_t)source[lastStart];
        const size_t MULTIBYTE_WIDTH = lead < 0xE0 ? 2 : lead < 0xF0 ? 3
                                                                     : 4;
        const size_t WIDTH = lead < 0x80 ? 1 : MULTIBYTE_WIDTH;
        if (length - lastStart < WIDTH)
            length = lastStart;
    }
    memcpy(destination, source, length);
    destination[length] = '\0';
}

static uint8_t ConnectMemberStatus(const char *id)
{
    RSessionSnapshot previous;
    RSessionSnapshot current;
    RNetReference previousReference;
    RNetReference currentReference;
    const int PREVIOUS_FD = RSessionFindBtFd(id);
    const int HAD_SESSION = PREVIOUS_FD >= 0 && RSessionFindByFd(PREVIOUS_FD, &previous) == 0 && RNetGetSessionReference(&previous, &previousReference) == 0;
    const int RESULT = RPacketBtConnectMember(id);
    if (RESULT != 0)
        return RESULT == 1 ? BT_CONNECT_NOT_REGISTERED : BT_CONNECT_FAILED;
    const int CURRENT_FD = RSessionFindBtFd(id);
    if (HAD_SESSION && CURRENT_FD == PREVIOUS_FD && RSessionFindByFd(CURRENT_FD, &current) == 0 && RNetGetSessionReference(&current, &currentReference) == 0 && currentReference.token == previousReference.token)
        return BT_CONNECT_ALREADY_CONNECTED;
    return BT_CONNECT_CONNECTED;
}
