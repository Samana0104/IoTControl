#pragma once

#include "IoTProtocol.h"

#define BUF_SIZE 100
#define ID_SIZE 10
#define MAX_CLNT 30

int StartServer(const char *port);
/* With the CLI enabled, a NULL port waits for 'start <port>'. */
int StartServerWithCli(const char *port, int enableCli);
