#include "RCtrlFan.h"
#include "RDatabase.h"

#include <inttypes.h>
#include <stdio.h>

void RCtrlFanReceive(const char *label, const FanData *data)
{
    uint64_t affectedRows;

    printf("[%s] FAN: fanSpeed=%u\n", label, (unsigned int)data->fanSpeed);
    if(UpdateFanData(data, &affectedRows) != 0)
    {
        fprintf(stderr, "[%s] FAN DB UPDATE failed: singleton_id=1\n", label);
    }
    else
    {
        printf("[%s] FAN DB UPDATE: singleton_id=1, affected=%" PRIu64 "\n", label, affectedRows);
    }
}
