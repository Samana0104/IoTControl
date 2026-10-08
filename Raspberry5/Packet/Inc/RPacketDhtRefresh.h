#pragma once
#include "RSession.h"
#include <stdint.h>

int RPacketDhtRefreshReceive(RSession *session, const uint8_t *payload, size_t length);
int RPacketDhtRequestDevice(const RSessionSnapshot *device);
void RPacketDhtRefreshHandleAck(const RSession *session, uint8_t result, int saved);
void RPacketDhtRefreshTick(uint64_t nowMs);
void RPacketDhtRefreshReset(void);
