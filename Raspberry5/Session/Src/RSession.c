#include "RSession.h"
#include "RLog.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

static RSession sessions[MAX_SESSION];
// 슬롯 추가/삭제와 로그인 상태 변경을 목록 조회와 직렬화
static pthread_mutex_t tableMutex = PTHREAD_MUTEX_INITIALIZER;

static void CopySnapshot(const RSession *session, RSessionSnapshot *snapshot);

RSession *RSessionAdd(RSessionType type, int fd, const char *address, const char *memberId, RMemberType memberType)
{
    RSession *session = NULL;

    if(address == NULL)
    {
        RLOG_ERROR("RSessionAdd: NULL address");
        return NULL;
    }
    pthread_mutex_lock(&tableMutex);
    for(int index = 0; index < MAX_SESSION; ++index)
    {
        if(sessions[index].inUse)
        {
            continue;
        }
        session = &sessions[index];
        memset(session, 0, sizeof(*session));
        session->inUse = 1;
        session->type = type;
        session->fd = fd;
        snprintf(session->address, sizeof(session->address), "%s", address);
        if(memberId != NULL)
        {
            snprintf(session->memberId, sizeof(session->memberId), "%s", memberId);
            session->memberType = memberType;
            session->authenticated = 1;
        }
        if(type == SESSION_BLUETOOTH)
        {
            snprintf(session->label, sizeof(session->label), "BT id=%s mac=%s", memberId != NULL ? memberId : "-", address);
        }
        else
        {
            snprintf(session->label, sizeof(session->label), "%s", address);
        }
        break;
    }
    pthread_mutex_unlock(&tableMutex);
    return session;
}

void RSessionRemove(RSession *session)
{
    if(session == NULL)
    {
        RLOG_ERROR("RSessionRemove: NULL session");
        return;
    }
    pthread_mutex_lock(&tableMutex);
    memset(session, 0, sizeof(*session));
    session->fd = -1;
    pthread_mutex_unlock(&tableMutex);
}

int RSessionFindBtFd(const char *memberId)
{
    int fd = -1;

    if(memberId == NULL)
    {
        return -1;
    }
    pthread_mutex_lock(&tableMutex);
    for(int index = 0; index < MAX_SESSION; ++index)
    {
        const RSession *session = &sessions[index];

        if(session->inUse && session->type == SESSION_BLUETOOTH && strcmp(session->memberId, memberId) == 0)
        {
            fd = session->fd;
            break;
        }
    }
    pthread_mutex_unlock(&tableMutex);
    return fd;
}

size_t RSessionGetSnapshots(RSessionSnapshot *snapshots)
{
    size_t snapshotCount = 0;

    if(snapshots == NULL)
    {
        RLOG_ERROR("RSessionGetSnapshots: NULL snapshots");
        return 0;
    }
    pthread_mutex_lock(&tableMutex);
    for(int index = 0; index < MAX_SESSION; ++index)
    {
        const RSession *session = &sessions[index];
        RSessionSnapshot *snapshot;

        if(!session->inUse)
        {
            continue;
        }
        snapshot = &snapshots[snapshotCount++];
        CopySnapshot(session, snapshot);
    }
    pthread_mutex_unlock(&tableMutex);
    return snapshotCount;
}

int RSessionFindByFd(int fd, RSessionSnapshot *snapshot)
{
    int result = -1;

    if(snapshot == NULL)
    {
        RLOG_ERROR("RSessionFindByFd: NULL snapshot");
        return -1;
    }
    pthread_mutex_lock(&tableMutex);
    for(int index = 0; index < MAX_SESSION; ++index)
    {
        const RSession *session = &sessions[index];

        if(session->inUse && session->fd == fd)
        {
            CopySnapshot(session, snapshot);
            result = 0;
            break;
        }
    }
    pthread_mutex_unlock(&tableMutex);
    return result;
}

const char *RSessionMemberTypeName(RMemberType memberType)
{
    switch(memberType)
    {
        case MEMBER_TYPE_STM32: return "STM32";
        case MEMBER_TYPE_ARDUINO: return "ARDUINO";
        case MEMBER_TYPE_PC: return "PC";
        default: return "-";
    }
}

void RSessionLogin(RSession *session, const char *memberId, size_t memberIdLength, RMemberType memberType)
{
    if(session == NULL || memberId == NULL)
    {
        RLOG_ERROR("RSessionLogin: NULL argument");
        return;
    }
    if(memberIdLength > MEM_ID_SIZE)
    {
        memberIdLength = MEM_ID_SIZE;
    }
    pthread_mutex_lock(&tableMutex);
    memcpy(session->memberId, memberId, memberIdLength);
    session->memberId[memberIdLength] = '\0';
    session->memberType = memberType;
    session->authenticated = 1;
    pthread_mutex_unlock(&tableMutex);
}

void RSessionLogout(RSession *session)
{
    if(session == NULL)
    {
        RLOG_ERROR("RSessionLogout: NULL session");
        return;
    }
    pthread_mutex_lock(&tableMutex);
    session->memberId[0] = '\0';
    session->memberType = MEMBER_TYPE_UNKNOWN;
    session->authenticated = 0;
    pthread_mutex_unlock(&tableMutex);
}

// tableMutex를 잡은 상태에서 호출
static void CopySnapshot(const RSession *session, RSessionSnapshot *snapshot)
{
    snapshot->type = session->type;
    snapshot->fd = session->fd;
    snapshot->authenticated = session->authenticated;
    memcpy(snapshot->address, session->address, sizeof(snapshot->address));
    memcpy(snapshot->memberId, session->memberId, sizeof(snapshot->memberId));
    snapshot->memberType = session->memberType;
}
