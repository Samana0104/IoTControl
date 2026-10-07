#include "IotServerCli.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define SERVER_CLI_PROMPT "iot-server> "
#define SERVER_CLI_WHITESPACE " \t\r\n\v\f"

/* Command flags. */
#define SERVER_CLI_TAKES_ARGUMENTS 0x01
/* The command waits on workers that log; do not hold stdout's lock while it runs. */
#define SERVER_CLI_UNLOCKED_OUTPUT 0x02

/* arguments: text right after the command name, including its leading whitespace. */
typedef int (*ServerCliCommandHandler)(ServerState *server, const char *arguments);

typedef struct _ServerCliCommand
{
    const char *name;
    const char *alias;
    const char *usage;
    const char *description;
    int flags;
    ServerCliCommandHandler execute;
} ServerCliCommand;

static int ExecuteStartCommand(ServerState *server, const char *arguments);
static int ExecuteHelpCommand(ServerState *server, const char *arguments);
static int ExecuteStatusCommand(ServerState *server, const char *arguments);
static int ExecuteClientsCommand(ServerState *server, const char *arguments);
static int ExecuteBluetoothCommand(ServerState *server, const char *arguments);
static int ExecuteBluetoothConnectCommand(ServerState *server, const char *arguments);
static int ExecuteDatabaseCommand(ServerState *server, const char *arguments);
static int ExecuteClearCommand(ServerState *server, const char *arguments);
static int ExecuteQuitCommand(ServerState *server, const char *arguments);

static const ServerCliCommand SERVER_CLI_COMMANDS[] =
{
    {"start", NULL, "start <port>", "Start listening on the specified port (1..65535)", SERVER_CLI_TAKES_ARGUMENTS, ExecuteStartCommand},
    {"help", NULL, "help", "Show this command list", 0, ExecuteHelpCommand},
    {"status", NULL, "status", "Show server port and runtime session counts", 0, ExecuteStatusCommand},
    {"clients", NULL, "clients", "List TCP clients, IDs and authentication state", 0, ExecuteClientsCommand},
    {"bluetooth", NULL, "bluetooth", "List current BT sockets, MACs and receiver state", 0, ExecuteBluetoothCommand},
    {"bt-connect", NULL, "bt-connect <id>", "Connect the member's DB-registered Bluetooth device", SERVER_CLI_TAKES_ARGUMENTS | SERVER_CLI_UNLOCKED_OUTPUT, ExecuteBluetoothConnectCommand},
    {"db", NULL, "db <SQL>", "Run one INSERT/UPDATE/SELECT statement (UPDATE needs WHERE)", SERVER_CLI_TAKES_ARGUMENTS, ExecuteDatabaseCommand},
    {"clear", NULL, "clear", "Clear the terminal screen", 0, ExecuteClearCommand},
    {"quit", "exit", "quit / exit", "Stop accepting, disconnect clients and stop the server", 0, ExecuteQuitCommand}
};

static void PrintServerCliHelp(void);
static void PrintServerClients(int bluetoothOnly);
static const char *SkipServerCliWhitespace(const char *text);
static int MatchServerCliName(const char *name, const char *input, size_t inputLength);
static const ServerCliCommand *FindServerCliCommand(const char *input, const char **arguments);

void ShowServerCli(const ServerCli *cli)
{
    if(!cli->enabled)
    {
        return;
    }
    PrintServerCliHelp();
    if(cli->interactive)
    {
        fputs(SERVER_CLI_PROMPT, stdout);
    }
    fflush(stdout);
}

int GetServerCliFd(const ServerCli *cli)
{
    return cli->enabled ? STDIN_FILENO : -1;
}

int ReadServerCli(ServerCli *cli, ServerState *server)
{
    char input[SERVER_CLI_INPUT_SIZE];
    ssize_t length = read(STDIN_FILENO, input, sizeof(input));

    if(length > 0)
    {
        return ProcessServerCliInput(cli, input, (size_t)length, server);
    }
    if(length < 0 && (errno == EINTR || errno == EAGAIN))
    {
        return SERVER_CLI_CONTINUE;
    }
    if(length == 0 && (cli->inputLength > 0 || cli->discardingInput))
    {
        if(ProcessServerCliInput(cli, "\n", 1, server) != SERVER_CLI_CONTINUE)
        {
            return SERVER_CLI_STOP;
        }
    }
    cli->enabled = 0;
    puts(IsServerRunning(server) ? "Server CLI input closed; server continues running." : "Server CLI input closed before server start.");
    fflush(stdout);
    return SERVER_CLI_CONTINUE;
}

int ProcessServerCliInput(ServerCli *cli, const char *input, size_t length, ServerState *server)
{
    for(size_t index = 0; index < length; ++index)
    {
        if(input[index] == '\n')
        {
            int result = SERVER_CLI_CONTINUE;

            if(cli->discardingInput)
            {
                puts("CLI input is too long; command discarded.");
            }
            else
            {
                cli->input[cli->inputLength] = '\0';
                result = ExecuteServerCliCommand(cli->input, server);
            }
            cli->inputLength = 0;
            cli->discardingInput = 0;
            if(result == SERVER_CLI_STOP)
            {
                return SERVER_CLI_STOP;
            }
            if(cli->interactive)
            {
                fputs(SERVER_CLI_PROMPT, stdout);
            }
            fflush(stdout);
        }
        else if(!cli->discardingInput)
        {
            if(input[index] == '\0' || cli->inputLength == sizeof(cli->input) - 1)
            {
                /* Discard the whole line; never execute a truncated command. */
                cli->discardingInput = 1;
            }
            else
            {
                cli->input[cli->inputLength++] = input[index];
            }
        }
    }
    return SERVER_CLI_CONTINUE;
}

int ExecuteServerCliCommand(char *input, ServerState *server)
{
    char *line = (char *)SkipServerCliWhitespace(input);
    size_t length = strlen(line);
    const ServerCliCommand *command;
    const char *arguments;
    int result = SERVER_CLI_CONTINUE;

    while(length > 0 && isspace((unsigned char)line[length - 1]))
    {
        line[--length] = '\0';
    }
    if(length == 0)
    {
        return SERVER_CLI_CONTINUE;
    }

    command = FindServerCliCommand(line, &arguments);
    if(command != NULL && (command->flags & SERVER_CLI_UNLOCKED_OUTPUT))
    {
        result = command->execute(server, arguments);
        fflush(stdout);
        return result;
    }

    /* Keep each CLI response together while packet workers also print logs. */
    flockfile(stdout);
    if(command == NULL)
    {
        printf("Unknown command: %s. Type 'help'.\n", line);
    }
    else
    {
        result = command->execute(server, arguments);
    }
    fflush(stdout);
    funlockfile(stdout);
    return result;
}

void PrintServerStatus(const ServerState *server)
{
    ServerClientSnapshot snapshots[MAX_CLNT * 2];
    size_t snapshotCount = GetServerClientSnapshots(snapshots);
    size_t bluetoothCount = GetServerBluetoothSnapshots(snapshots);
    size_t receiverCount = 0;

    for(size_t index = 0; index < bluetoothCount; ++index)
    {
        if(snapshots[index].bluetoothFd >= 0)
        {
            receiverCount += snapshots[index].bluetoothReceiving != 0;
        }
    }
    if(IsServerRunning(server))
    {
        printf("Server: running\nListen: 0.0.0.0:%d (TCP/TLS)\n", GetServerPort(server));
    }
    else
    {
        puts("Server: not started\nListen: none (use 'start <port>')");
    }
    printf("TCP sessions: %zu/%d\nBT sockets: %zu (active receivers: %zu)\n", snapshotCount, MAX_CLNT, bluetoothCount, receiverCount);
}

static int ExecuteStartCommand(ServerState *server, const char *arguments)
{
    const char *port = SkipServerCliWhitespace(arguments);

    if(!isdigit((unsigned char)*port) || ParseServerPort(port) < 0)
    {
        puts("Usage: start <port> (1..65535)");
    }
    else if(IsServerRunning(server))
    {
        printf("Server already running on port %d. Restart the process to change ports.\n", GetServerPort(server));
    }
    else if(StartServerListener(server, port) != 0)
    {
        puts("Server start failed. Fix the configuration or retry 'start <port>'.");
    }
    return SERVER_CLI_CONTINUE;
}

static int ExecuteHelpCommand(ServerState *server, const char *arguments)
{
    (void)server;
    (void)arguments;
    PrintServerCliHelp();
    return SERVER_CLI_CONTINUE;
}

static int ExecuteStatusCommand(ServerState *server, const char *arguments)
{
    (void)arguments;
    PrintServerStatus(server);
    return SERVER_CLI_CONTINUE;
}

static int ExecuteClientsCommand(ServerState *server, const char *arguments)
{
    (void)server;
    (void)arguments;
    PrintServerClients(0);
    return SERVER_CLI_CONTINUE;
}

static int ExecuteBluetoothCommand(ServerState *server, const char *arguments)
{
    (void)server;
    (void)arguments;
    PrintServerClients(1);
    return SERVER_CLI_CONTINUE;
}

static int ExecuteBluetoothConnectCommand(ServerState *server, const char *arguments)
{
    const char *memberId = SkipServerCliWhitespace(arguments);

    if(*memberId == '\0' || strlen(memberId) > MEM_ID_SIZE || strpbrk(memberId, SERVER_CLI_WHITESPACE) != NULL)
    {
        puts("Usage: bt-connect <member ID> (1..8 bytes; registered in DB)");
    }
    else if(!IsServerRunning(server))
    {
        puts("Start the server first: start <port>");
    }
    else
    {
        int connectResult = RequestMemberBluetoothConnection(memberId);

        printf("Bluetooth request: id=%s, result=%s\n", memberId, connectResult == 0 ? "connected" : connectResult == 1 ? "not registered" : "failed");
    }
    return SERVER_CLI_CONTINUE;
}

static int ExecuteDatabaseCommand(ServerState *server, const char *arguments)
{
    if(!IsServerDatabaseInitialized(server))
    {
        puts("Initialize the database first: start <port>");
    }
    else
    {
        ExecuteDatabaseCliCommand(arguments, stdout);
    }
    return SERVER_CLI_CONTINUE;
}

static int ExecuteClearCommand(ServerState *server, const char *arguments)
{
    (void)server;
    (void)arguments;
    if(isatty(STDOUT_FILENO))
    {
        fputs("\033[2J\033[H", stdout);
    }
    return SERVER_CLI_CONTINUE;
}

static int ExecuteQuitCommand(ServerState *server, const char *arguments)
{
    (void)server;
    (void)arguments;
    return SERVER_CLI_STOP;
}

static void PrintServerCliHelp(void)
{
    puts("\n=== IoT server CLI ===");
    for(size_t index = 0; index < sizeof(SERVER_CLI_COMMANDS) / sizeof(SERVER_CLI_COMMANDS[0]); ++index)
    {
        printf("  %-16s %s\n", SERVER_CLI_COMMANDS[index].usage, SERVER_CLI_COMMANDS[index].description);
    }
    puts("CLI commands are local only. 'db insert/update' changes DB records.\n");
}

static void PrintServerClients(int bluetoothOnly)
{
    ServerClientSnapshot snapshots[MAX_CLNT * 2];
    size_t snapshotCount = bluetoothOnly ? GetServerBluetoothSnapshots(snapshots) : GetServerClientSnapshots(snapshots);
    size_t rowCount = 0;

    if(bluetoothOnly)
    {
        puts("Runtime BT sockets (not a list of DB registrations):");
        puts("SLOT ID       MAC               FD  RX");
    }
    else
    {
        puts("SLOT FD  IP              ID       AUTH LINK");
    }
    for(size_t index = 0; index < snapshotCount; ++index)
    {
        const ServerClientSnapshot *snapshot = &snapshots[index];
        const char *memberId = snapshot->memberId[0] != '\0' ? snapshot->memberId : "-";

        if(bluetoothOnly)
        {
            if(snapshot->bluetoothFd < 0)
            {
                continue;
            }
            printf("%-4d %-8s %-17s %-3d %s\n", snapshot->index, memberId, snapshot->bluetoothMac, snapshot->bluetoothFd, snapshot->bluetoothReceiving ? "running" : "stopped");
        }
        else
        {
            printf("%-4d %-3d %-15s %-8s %-4s %s\n", snapshot->index, snapshot->fd, snapshot->ip, memberId, snapshot->authenticated ? "yes" : "no", snapshot->connected ? "connected" : "closing");
        }
        ++rowCount;
    }
    if(rowCount == 0)
    {
        puts(bluetoothOnly ? "No runtime Bluetooth sockets." : "No TCP clients.");
    }
}

static const char *SkipServerCliWhitespace(const char *text)
{
    while(isspace((unsigned char)*text))
    {
        ++text;
    }
    return text;
}

static int MatchServerCliName(const char *name, const char *input, size_t inputLength)
{
    return name != NULL && strlen(name) == inputLength && strncmp(name, input, inputLength) == 0;
}

static const ServerCliCommand *FindServerCliCommand(const char *input, const char **arguments)
{
    size_t nameLength = strcspn(input, SERVER_CLI_WHITESPACE);

    for(size_t index = 0; index < sizeof(SERVER_CLI_COMMANDS) / sizeof(SERVER_CLI_COMMANDS[0]); ++index)
    {
        const ServerCliCommand *command = &SERVER_CLI_COMMANDS[index];

        if(!MatchServerCliName(command->name, input, nameLength) && !MatchServerCliName(command->alias, input, nameLength))
        {
            continue;
        }
        /* 'quit extra' is not 'quit': reject arguments for commands without any. */
        if(input[nameLength] != '\0' && !(command->flags & SERVER_CLI_TAKES_ARGUMENTS))
        {
            return NULL;
        }
        *arguments = input + nameLength;
        return command;
    }
    return NULL;
}
