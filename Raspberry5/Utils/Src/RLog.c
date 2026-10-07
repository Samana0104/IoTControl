#include "RLog.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

#define RLOG_TRUNCATED_MARK "..."

static _Atomic(FILE *) logOutput = NULL;
static _Atomic uint8_t logLevel = RLOG_LEVEL;

static char LevelToChar(uint8_t level);
static const char *GetFileName(const char *path);

void RLogBegin(FILE *output)
{
    atomic_store(&logOutput, output);
}

void RLogSetLevel(uint8_t level)
{
    atomic_store(&logLevel, level);
}

uint8_t RLogGetLevel(void)
{
    return atomic_load(&logLevel);
}

void RLogWrite(uint8_t level, const char *file, int line, const char *format, ...)
{
    char logLine[RLOG_LINE_SIZE + 1]; /* + newline */
    struct timespec now;
    struct tm localNow;
    FILE *output = atomic_load(&logOutput);
    va_list args;
    int length;
    int bodyLength = 0;

    if(level == RLOG_LEVEL_NONE || level > atomic_load(&logLevel))
    {
        return;
    }
    if(output == NULL)
    {
        output = stdout;
    }

    clock_gettime(CLOCK_REALTIME, &now);
    localtime_r(&now.tv_sec, &localNow);
    length = snprintf(logLine, RLOG_LINE_SIZE, "[%04d-%02d-%02d %02d:%02d:%02d.%03ld][%c]", localNow.tm_year + 1900, localNow.tm_mon + 1, localNow.tm_mday, localNow.tm_hour, localNow.tm_min, localNow.tm_sec, now.tv_nsec / 1000000L, LevelToChar(level));
    if(length >= 0 && (size_t)length < RLOG_LINE_SIZE)
    {
        int locationLength = file != NULL ? snprintf(logLine + length, RLOG_LINE_SIZE - (size_t)length, "[%s:%d] ", GetFileName(file), line) : snprintf(logLine + length, RLOG_LINE_SIZE - (size_t)length, " ");

        length = locationLength < 0 ? -1 : length + locationLength;
    }
    if(length < 0)
    {
        return;
    }
    if((size_t)length < RLOG_LINE_SIZE)
    {
        va_start(args, format);
        bodyLength = vsnprintf(logLine + length, RLOG_LINE_SIZE - (size_t)length, format, args);
        va_end(args);
    }
    if((size_t)length + (size_t)(bodyLength > 0 ? bodyLength : 0) >= RLOG_LINE_SIZE)
    {
        memcpy(logLine + RLOG_LINE_SIZE - sizeof(RLOG_TRUNCATED_MARK), RLOG_TRUNCATED_MARK, sizeof(RLOG_TRUNCATED_MARK));
        length = RLOG_LINE_SIZE - 1;
    }
    else
    {
        length += bodyLength > 0 ? bodyLength : 0;
    }
    logLine[length] = '\n';
    logLine[length + 1] = '\0';

    // stdio 함수 하나는 FILE 락 안에서 실행되므로, 줄바꿈까지 한 번에 쓰면 다른 스레드와 섞이지 않음
    fputs(logLine, output);
    fflush(output);
}

static const char *GetFileName(const char *path)
{
    const char *slash = strrchr(path, '/');

    return slash != NULL ? slash + 1 : path;
}

static char LevelToChar(uint8_t level)
{
    switch(level)
    {
    case RLOG_LEVEL_ERROR:
        return 'E';
    case RLOG_LEVEL_WARN:
        return 'W';
    case RLOG_LEVEL_INFO:
        return 'I';
    default:
        return 'D';
    }
}
