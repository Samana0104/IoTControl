#include "RCtrlFan.h"
#include "RDatabase.h"
#include "RLog.h"

#include <inttypes.h>
#include <stdio.h>

void RCtrlFanReceive(const char *label, const FanData *data)
{
    uint64_t affectedRows;

    RLOG_INFO("[%s] FAN: fanSpeed=%u", label, (unsigned int)data->fanSpeed);
    if(UpdateFanData(data, &affectedRows) != 0)
    {
        RLOG_WARN("[%s] FAN DB UPDATE failed: singleton_id=1", label);
    }
    else
    {
        RLOG_INFO("[%s] FAN DB UPDATE: singleton_id=1, affected=%" PRIu64, label, affectedRows);
    }
}
