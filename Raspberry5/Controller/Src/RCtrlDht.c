#include "RCtrlDht.h"
#include "RDatabase.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

void RCtrlDhtReceive(const char *label, const char *memberId, const DhtData *data)
{
    uint64_t affectedRows;

    printf("[%s] DHT: temp=%u, humi=%u\n", label, (unsigned int)data->temp, (unsigned int)data->humi);
    if(UpdateDhtData(memberId, strlen(memberId), data, &affectedRows) != 0)
    {
        fprintf(stderr, "[%s] DHT DB UPDATE failed: id=%s\n", label, memberId);
    }
    else
    {
        printf("[%s] DHT DB UPDATE: id=%s, affected=%" PRIu64 "\n", label, memberId, affectedRows);
    }
}
