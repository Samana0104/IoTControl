#include "RCtrlCon.h"
#include "RDatabase.h"
#include "RLog.h"

#include <inttypes.h>
#include <stdio.h>

void RCtrlConReceive(const char *label, const ConData *data)
{
    uint64_t affectedRows;

    RLOG_INFO("[%s] CON: tempData=%u", label, (unsigned int)data->tempData);
    if(UpdateConData(data, &affectedRows) != 0)
    {
        RLOG_WARN("[%s] CON DB UPDATE failed: singleton_id=1", label);
    }
    else
    {
        RLOG_INFO("[%s] CON DB UPDATE: singleton_id=1, affected=%" PRIu64, label, affectedRows);
    }
}
