#pragma once

#include "RTCPServer.h"

#include <stddef.h>

/* args: text after the command name with leading whitespace removed. */
typedef void (*RCommandHandler)(TCPServer *server, const char *args);

typedef struct _RCommand
{
    const char *name;
    RCommandHandler handler;
} RCommand;

/* Runs one console line (trimmed in place). */
void RCommandExecute(TCPServer *server, char *line);
void RCommandPrintHelp(void);

const char *RCommandSkipSpace(const char *text);

/* Runs the command named by the first token of args (case-insensitive);
   prints the command list when none matches. group == NULL: top level. */
void RCommandDispatch(TCPServer *server, const char *args, const char *group, const RCommand *commands, size_t count);

#define RCOMMAND_COUNT(commands) (sizeof(commands) / sizeof((commands)[0]))
