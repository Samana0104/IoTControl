#pragma once

#include "RCommand.h"

// server <start|status|sessions> ...
void RCmdServer(TCPServer *server, const char *args);
