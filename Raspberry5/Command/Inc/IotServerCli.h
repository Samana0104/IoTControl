#pragma once

#include "IotDatabaseCommand.h"
#include "IotServerControl.h"

#include <stddef.h>

#define SERVER_CLI_INPUT_SIZE (DATABASE_COMMAND_MAX_SQL_SIZE + 8)

#define SERVER_CLI_CONTINUE 0
#define SERVER_CLI_STOP 1

typedef struct _ServerCli
{
    int enabled;
    int interactive;
    int discardingInput;
    size_t inputLength;
    char input[SERVER_CLI_INPUT_SIZE];
} ServerCli;

/* Prints the command list and the first prompt when the CLI is enabled. */
void ShowServerCli(const ServerCli *cli);
/* Descriptor for the server's poll() loop, or -1 while the CLI is disabled. */
int GetServerCliFd(const ServerCli *cli);

/* Each returns SERVER_CLI_STOP when a command asked the server to stop. */
int ReadServerCli(ServerCli *cli, ServerState *server);
int ProcessServerCliInput(ServerCli *cli, const char *input, size_t length, ServerState *server);
int ExecuteServerCliCommand(char *input, ServerState *server);

void PrintServerStatus(const ServerState *server);
