#include "ALog.h"

static Print *logOutput = nullptr;
static uint8_t logLevel = ALOG_LEVEL;

void BeginLog(Print &output)
{
    logOutput = &output;
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
