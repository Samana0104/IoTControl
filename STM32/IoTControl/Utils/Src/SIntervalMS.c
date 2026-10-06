#include "SIntervalMS.h"

void SIntervalMSInit(SIntervalMS *interval, uint32_t periodMs)
{
    interval->periodMs = periodMs;
    interval->lastMs = 0;
}

bool SIntervalMSElapsed(SIntervalMS *interval, uint32_t nowMs)
{
    if (nowMs - interval->lastMs < interval->periodMs)
    {
        return false;
    }

    interval->lastMs = nowMs;
    return true;
}
