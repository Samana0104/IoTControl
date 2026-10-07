#include "RCmdDb.h"
#include "RDatabaseCommand.h"

#include <stdio.h>

#define DB_STATEMENT_SIZE (DATABASE_COMMAND_MAX_SQL_SIZE + 1)

// 하위 명령 이름이 SQL 키워드이므로 키워드를 다시 붙여 한 문장으로 실행
static void ExecuteDbStatement(TCPServer *server, const char *keyword, const char *args)
{
    char sql[DB_STATEMENT_SIZE];
    int length;

    if(!server->databaseInitialized)
    {
        puts("Initialize the database first: server start [port]");
        return;
    }
    length = snprintf(sql, sizeof(sql), "%s %s", keyword, args);
    if(length < 0 || (size_t)length >= sizeof(sql))
    {
        puts("SQL is too long.");
        return;
    }
    flockfile(stdout);
    ExecuteDatabaseCliCommand(sql, stdout);
    funlockfile(stdout);
}

static void DbSelect(TCPServer *server, const char *args)
{
    ExecuteDbStatement(server, "SELECT", args);
}

static void DbInsert(TCPServer *server, const char *args)
{
    ExecuteDbStatement(server, "INSERT", args);
}

static void DbUpdate(TCPServer *server, const char *args)
{
    ExecuteDbStatement(server, "UPDATE", args);
}

static const RCommand DB_COMMANDS[] =
{
    {"select", DbSelect},
    {"insert", DbInsert},
    {"update", DbUpdate}
};

void RCmdDb(TCPServer *server, const char *args)
{
    RCommandDispatch(server, args, "db", DB_COMMANDS, RCOMMAND_COUNT(DB_COMMANDS));
}
