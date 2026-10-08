#pragma once

#include "RSession.h"
#include <stddef.h>
#include <stdint.h>

int RPacketFanApplyReceive(RSession *session, const uint8_t *payload, size_t length);
int RPacketFanSendSpeed(int fd, uint8_t percent);
void RPacketFanHandleControlAck(const RSession *session, uint8_t result);
void RPacketFanControlTick(uint64_t nowMs);
void RPacketFanResetControl(void);
