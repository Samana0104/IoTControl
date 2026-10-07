#include "RCommand.h"
#include "RCmdBt.h"
#include "RCmdDb.h"
#include "RCmdFan.h"
#include "RCmdServer.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static void CmdHelp(TCPServer *server, const char *args);
static void CmdClear(TCPServer *server, const char *args);
static void CmdQuit(TCPServer *server, const char *args);
static void PrintCommandList(const char *group, const RCommand *commands, size_t count);

static const RCommand COMMANDS[] =
{
    {"server", RCmdServer},
    {"bt", RCmdBt},
    {"db", RCmdDb},
    {"fan", RCmdFan},
    {"help", CmdHelp},
    {"clear", CmdClear},
    {"quit", CmdQuit},
    {"exit", CmdQuit}
};

void RCommandExecute(TCPServer *server, char *line)
{
    char *command = (char *)RCommandSkipSpace(line);
    size_t length = strlen(command);

    while(length > 0 && isspace((unsigned char)command[length - 1]))
    {
        command[--length] = '\0';
    }
    if(length == 0)
    {
        return;
    }
    RCommandDispatch(server, command, NULL, COMMANDS, RCOMMAND_COUNT(COMMANDS));
}

void RCommandPrintHelp(void)
{
    flockfile(stdout);
    PrintCommandList(NULL, COMMANDS, RCOMMAND_COUNT(COMMANDS));
    puts("Type a group name (server, bt, db, fan) to list its commands.");
    fflush(stdout);
    funlockfile(stdout);
}

const char *RCommandSkipSpace(const char *text)
{
    while(isspace((unsigned char)*text))
    {
        ++text;
    }
    return text;
}

void RCommandDispatch(TCPServer *server, const char *args, const char *group, const RCommand *commands, size_t count)
{
    const char *input = RCommandSkipSpace(args);
    size_t nameLength = 0;

    while(input[nameLength] != '\0' && !isspace((unsigned char)input[nameLength]))
    {
        ++nameLength;
    }

    for(size_t index = 0; nameLength > 0 && index < count; ++index)
    {
        if(strlen(commands[index].name) == nameLength && strncasecmp(commands[index].name, input, nameLength) == 0)
        {
            commands[index].handler(server, RCommandSkipSpace(input + nameLength));
            fflush(stdout);
            return;
        }
    }

    flockfile(stdout);
    if(nameLength > 0)
    {
        printf("Unknown command: %.*s\n", (int)nameLength, input);
    }
    PrintCommandList(group, commands, count);
    fflush(stdout);
    funlockfile(stdout);
}

static void CmdHelp(TCPServer *server, const char *args)
{
    (void)server;
    (void)args;
    RCommandPrintHelp();
}

static void CmdClear(TCPServer *server, const char *args)
{
    (void)server;
    (void)args;
    if(isatty(STDOUT_FILENO))
    {
        fputs("\033[2J\033[H", stdout);
    }
}

static void CmdQuit(TCPServer *server, const char *args)
{
    (void)server;
    (void)args;
    RequestServerStop();
}

static void PrintCommandList(const char *group, const RCommand *commands, size_t count)
{
    if(group == NULL)
    {
        puts("commands:");
    }
    else
    {
        printf("usage: %s <command>\n", group);
    }
    for(size_t index = 0; index < count; ++index)
    {
        printf("  %s\n", commands[index].name);
    }
}
