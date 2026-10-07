#pragma once

#include "RCommand.h"

// server <start|status|clients> ...
void RCmdServer(ServerState *server, const char *args);
