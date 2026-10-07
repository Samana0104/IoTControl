#include "RCtrlCon.h"
#include "RDatabase.h"

#include <inttypes.h>
#include <stdio.h>

void RCtrlConReceive(const char *label, const ConData *data)
{
    uint64_t affectedRows;

    printf("[%s] CON: tempData=%u\n", label, (unsigned int)data->tempData);
    if(UpdateConData(data, &affectedRows) != 0)
    {
        fprintf(stderr, "[%s] CON DB UPDATE failed: singleton_id=1\n", label);
    }
    else
    {
        printf("[%s] CON DB UPDATE: singleton_id=1, affected=%" PRIu64 "\n", label, affectedRows);
    }
}
