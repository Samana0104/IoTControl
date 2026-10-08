#pragma once
#include "RSession.h"
int RPacketBtConnectAllReceive(RSession *session, const uint8_t *payload, size_t length);
int RPacketBtScanReceive(RSession *session, const uint8_t *payload, size_t length);
void RPacketBtOperationsStart(void);
void RPacketBtOperationsStop(void);
