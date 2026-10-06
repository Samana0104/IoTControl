#include "ALog.h"

// 디버그 시리얼 관리 주체: 시작은 BeginLog()에서만, 다른 곳은 GetLogSerial()로 받아 씀
static Stream &logSerial = Serial;
static Print *logOutput = nullptr;
static uint8_t logLevel = ALOG_LEVEL;

void BeginLog(long baudRate)
{
    Serial.begin(baudRate);
    logOutput = &logSerial;
}

Stream &GetLogSerial()
{
    return logSerial;
}

void SetLogLevel(uint8_t level)
{
    logLevel = level;
}

uint8_t GetLogLevel()
{
    return logLevel;
}

Print *BeginLogLine(uint8_t level)
{
    if (logOutput == nullptr || level == ALOG_LEVEL_NONE || level > logLevel)
    {
        return nullptr;
    }

    logOutput->print('[');
    logOutput->print(millis());
    logOutput->print(F("]["));

    switch (level)
    {
    case ALOG_LEVEL_ERROR:
        logOutput->print('E');
        break;
    case ALOG_LEVEL_WARN:
        logOutput->print('W');
        break;
    case ALOG_LEVEL_INFO:
        logOutput->print('I');
        break;
    default:
        logOutput->print('D');
        break;
    }

    logOutput->print(F("] "));
    return logOutput;
}
