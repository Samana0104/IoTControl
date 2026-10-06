#pragma once

#include <Arduino.h>

class IntervalMS
{
public:
    explicit IntervalMS(uint32_t _periodMs) : periodMs(_periodMs) {}
    IntervalMS() = delete;

    bool Elapsed(uint32_t nowMs) noexcept
    {
        if (nowMs - lastMs < periodMs) 
        {
            return false;
        }   
        
        lastMs = nowMs;
        return true;
    }

private:
    uint32_t periodMs;
    uint32_t lastMs = 0;
};

