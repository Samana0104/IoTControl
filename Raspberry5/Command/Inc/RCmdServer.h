#pragma once

#include "RCommand.h"

// server <start|status|clients> ...
void RCmdServer(TCPServer *server, const char *args);
