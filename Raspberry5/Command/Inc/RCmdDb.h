#pragma once

#include "RCommand.h"

// db <select|insert|update> ...
void RCmdDb(TCPServer *server, const char *args);
