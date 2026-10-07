#include "RCmdMember.h"
#include "IoTPacket.h"
#include "RDatabase.h"
#include "RDatabaseQuery.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define MEMBER_TYPE_SIZE 8

// 회원가입. 비밀번호는 서버가 argon2로 해시해서 DB에 저장 (공백 없는 비밀번호만)
static void MemberAdd(TCPServer *server, const char *args)
{
    char memberId[MEM_ID_SIZE + 2];
    char password[MEM_PW_SIZE + 2];
    char type[MEMBER_TYPE_SIZE + 1];
    char extra[2];
    int result;

    if(sscanf(args, "%9s %65s %8s %1s", memberId, password, type, extra) != 3 || strlen(memberId) > MEM_ID_SIZE || strlen(password) > MEM_PW_SIZE)
    {
        puts("Usage: member add <ID 1..8> <password> <stm32|arduino|pc>");
        return;
    }
    if(!server->databaseInitialized)
    {
        puts("Initialize the database first: server start [port]");
        return;
    }
    for(size_t index = 0; type[index] != '\0'; ++index)
    {
        type[index] = (char)toupper((unsigned char)type[index]);
    }

    result = RegisterMember(memberId, password, type);
    memset(password, 0, sizeof(password));
    if(result > 0)
    {
        printf("Member added: id=%s, type=%s\n", memberId, type);
    }
    else if(result == 0)
    {
        printf("Member ID already exists: %s\n", memberId);
    }
    else
    {
        puts("Member sign-up failed (type must be stm32, arduino or pc; see log).");
    }
}

static int PrintMemberRow(const DatabaseRow *row, void *context)
{
    (void)context;
    if(row->values == NULL || row->columnCount < 2)
    {
        return 0;
    }
    printf("%-8s %s\n", row->values[0] != NULL ? row->values[0] : "-", row->values[1] != NULL ? row->values[1] : "-");
    return 0;
}

static void MemberList(TCPServer *server, const char *args)
{
    uint64_t rowCount = 0;

    (void)args;
    if(!server->databaseInitialized)
    {
        puts("Initialize the database first: server start [port]");
        return;
    }
    flockfile(stdout);
    puts("ID       TYPE");
    if(ExecuteDatabaseQuery(QUERY_SELECT_MEMBER_ALL, NULL, 0, PrintMemberRow, NULL, &rowCount) != 0)
    {
        puts("Member list failed (see log).");
    }
    else if(rowCount == 0)
    {
        puts("No members.");
    }
    funlockfile(stdout);
}

static const RCommand MEMBER_COMMANDS[] =
{
    {"add", MemberAdd},
    {"list", MemberList}
};

void RCmdMember(TCPServer *server, const char *args)
{
    RCommandDispatch(server, args, "member", MEMBER_COMMANDS, RCOMMAND_COUNT(MEMBER_COMMANDS));
}
