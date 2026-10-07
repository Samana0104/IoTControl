#include "RDatabaseQuery.h"
#include "RDatabase.h"
#include "RLog.h"

#include <errmsg.h>
#include <errno.h>
#include <mysql.h>
#include <stdlib.h>
#include <string.h>

// 결과 버퍼를 열의 표시 폭으로 잡는 최대값 (넘으면 실제 최대 값 길이만큼)
#define DATABASE_QUERY_MAX_COLUMN_WIDTH 65535

// QueryDatabaseValue가 첫 행 첫 열을 받는 곳
typedef struct _DatabaseValueContext
{
    char *value;
    size_t valueSize;
    int found;
    int tooLong;
} DatabaseValueContext;

static int CopyFirstValue(const DatabaseRow *row, void *context);
static int RunQuery(MYSQL_STMT *statement, const char *query, const DatabaseValue *params, unsigned int paramCount, DatabaseRowCallback callback, void *context, uint64_t *rowCount, int *connectionLost);
static int IsConnectionLost(unsigned int errorCode);
static int BindQueryParams(MYSQL_STMT *statement, const DatabaseValue *params, unsigned int paramCount, unsigned long *lengths);
static int ReadQueryRows(MYSQL_STMT *statement, MYSQL_RES *metadata, DatabaseRowCallback callback, void *context, uint64_t *rowCount);
static int DeliverQueryRows(MYSQL_STMT *statement, MYSQL_RES *metadata, MYSQL_BIND *results, char **buffers, DatabaseRowCallback callback, void *context, uint64_t *rowCount);

int ExecuteDatabaseQuery(const char *query, const DatabaseValue *params, unsigned int paramCount, DatabaseRowCallback callback, void *context, uint64_t *rowCount)
{
    MYSQL *connection;
    MYSQL_STMT *statement;
    uint64_t count = 0;
    int result;

    if(rowCount != NULL)
    {
        *rowCount = 0;
    }
    if(query == NULL || (params == NULL && paramCount > 0) || paramCount > DATABASE_QUERY_MAX_PARAMS)
    {
        RLOG_ERROR("ExecuteDatabaseQuery: invalid argument: paramCount=%u", paramCount);
        errno = EINVAL;
        return -1;
    }
    // 서버가 오래 쉰 연결을 끊었으면(wait_timeout) 다시 접속해서 한 번만 재시도
    for(int attempt = 0; attempt < 2; ++attempt)
    {
        int connectionLost = 0;

        connection = GetDatabaseConnection();
        if(connection == NULL)
        {
            errno = EIO;
            return -1;
        }
        statement = mysql_stmt_init(connection);
        if(statement == NULL)
        {
            RLOG_ERROR("MySQL statement initialization failed");
            ResetDatabaseConnection();
            errno = EIO;
            return -1;
        }
        count = 0;
        result = RunQuery(statement, query, params, paramCount, callback, context, &count, &connectionLost);
        mysql_stmt_close(statement);
        if(!connectionLost)
        {
            break;
        }
        RLOG_WARN("MySQL connection lost; reconnecting [%s]", query);
        ResetDatabaseConnection();
    }
    if(rowCount != NULL)
    {
        *rowCount = count;
    }
    return result;
}

// 준비 → 바인딩 → 실행. 결과가 있으면 행을 읽고, 없으면 변경 행 수
// 준비·실행 단계에서 연결이 끊겼으면 *connectionLost = 1 (아직 결과를 넘기기 전이라 재시도해도 됨)
static int RunQuery(MYSQL_STMT *statement, const char *query, const DatabaseValue *params, unsigned int paramCount, DatabaseRowCallback callback, void *context, uint64_t *rowCount, int *connectionLost)
{
    unsigned long lengths[DATABASE_QUERY_MAX_PARAMS];
    MYSQL_RES *metadata;
    int result;

    if(mysql_stmt_prepare(statement, query, (unsigned long)strlen(query)) != 0)
    {
        *connectionLost = IsConnectionLost(mysql_stmt_errno(statement));
        RLOG_ERROR("MySQL query prepare failed: %s [%s]", mysql_stmt_error(statement), query);
        errno = EIO;
        return -1;
    }
    if(mysql_stmt_param_count(statement) != paramCount)
    {
        RLOG_ERROR("MySQL query expects %lu parameter(s), got %u [%s]", mysql_stmt_param_count(statement), paramCount, query);
        errno = EINVAL;
        return -1;
    }
    if(BindQueryParams(statement, params, paramCount, lengths) != 0)
    {
        RLOG_ERROR("MySQL parameter binding failed: %s [%s]", mysql_stmt_error(statement), query);
        errno = EIO;
        return -1;
    }
    // 쿼리는 ? 자리표시자만 있는 상수라 로그에 바인딩 값(비밀번호 등)이 남지 않음
    if(mysql_stmt_execute(statement) != 0)
    {
        *connectionLost = IsConnectionLost(mysql_stmt_errno(statement));
        RLOG_WARN("MySQL query failed: %s (error %u) [%s]", mysql_stmt_error(statement), mysql_stmt_errno(statement), query);
        errno = EIO;
        return -1;
    }

    metadata = mysql_stmt_result_metadata(statement);
    if(metadata == NULL)
    {
        *rowCount = (uint64_t)mysql_stmt_affected_rows(statement);
        return 0;
    }
    result = ReadQueryRows(statement, metadata, callback, context, rowCount);
    mysql_free_result(metadata);
    if(result != 0)
    {
        RLOG_WARN("MySQL query result read failed [%s]", query);
        errno = EIO;
    }
    return result;
}

// lengths는 mysql_stmt_execute가 끝날 때까지 살아 있어야 함 (실행 시점에 길이를 포인터로 읽음)
static int BindQueryParams(MYSQL_STMT *statement, const DatabaseValue *params, unsigned int paramCount, unsigned long *lengths)
{
    MYSQL_BIND binds[DATABASE_QUERY_MAX_PARAMS];

    if(paramCount == 0)
    {
        return 0;
    }
    memset(binds, 0, sizeof(binds));
    for(unsigned int index = 0; index < paramCount; ++index)
    {
        if(params[index].text != NULL)
        {
            lengths[index] = (unsigned long)strlen(params[index].text);
            binds[index].buffer_type = MYSQL_TYPE_STRING;
            binds[index].buffer = (void *)params[index].text;
            binds[index].buffer_length = lengths[index];
            binds[index].length = &lengths[index];
        }
        else
        {
            binds[index].buffer_type = MYSQL_TYPE_LONGLONG;
            binds[index].buffer = (void *)&params[index].number;
        }
    }
    // MYSQL_BIND 자체는 복사되지만 buffer/length가 가리키는 값은 실행 때 읽음
    return mysql_stmt_bind_param(statement, binds) == 0 ? 0 : -1;
}

// 결과를 모두 받아 열마다 가장 긴 값 크기의 문자열 버퍼를 잡은 뒤 행을 넘김
static int ReadQueryRows(MYSQL_STMT *statement, MYSQL_RES *metadata, DatabaseRowCallback callback, void *context, uint64_t *rowCount)
{
    unsigned int columnCount = mysql_num_fields(metadata);
    MYSQL_FIELD *fields;
    MYSQL_BIND *results;
    char **buffers;
    my_bool updateMaxLength = 1;
    int result = -1;

    mysql_stmt_attr_set(statement, STMT_ATTR_UPDATE_MAX_LENGTH, &updateMaxLength);
    if(mysql_stmt_store_result(statement) != 0)
    {
        RLOG_WARN("MySQL result store failed: %s", mysql_stmt_error(statement));
        return -1;
    }
    fields = mysql_fetch_fields(metadata);
    results = calloc(columnCount, sizeof(*results));
    buffers = calloc(columnCount, sizeof(*buffers));
    if(fields != NULL && results != NULL && buffers != NULL)
    {
        result = 0;
        for(unsigned int index = 0; index < columnCount && result == 0; ++index)
        {
            // 숫자/날짜 열은 max_length가 바이너리 크기라 문자열 표시 폭(length)도 고려 (TEXT류처럼 큰 폭은 제외)
            unsigned long size = fields[index].max_length;

            if(fields[index].length > size && fields[index].length <= DATABASE_QUERY_MAX_COLUMN_WIDTH)
            {
                size = fields[index].length;
            }
            buffers[index] = malloc(size + 1);
            if(buffers[index] == NULL)
            {
                result = -1;
                break;
            }
            results[index].buffer_type = MYSQL_TYPE_STRING;
            results[index].buffer = buffers[index];
            results[index].buffer_length = size + 1;
        }
        if(result == 0)
        {
            result = DeliverQueryRows(statement, metadata, results, buffers, callback, context, rowCount);
        }
    }
    for(unsigned int index = 0; buffers != NULL && index < columnCount; ++index)
    {
        // 비밀번호 해시 같은 값이 해제된 메모리에 남지 않게 지움
        if(buffers[index] != NULL)
        {
            explicit_bzero(buffers[index], results[index].buffer_length);
        }
        free(buffers[index]);
    }
    free(buffers);
    free(results);
    mysql_stmt_free_result(statement);
    return result;
}

// 열 이름 행(values == NULL)을 한 번 넘긴 뒤 데이터 행을 하나씩 넘김
static int DeliverQueryRows(MYSQL_STMT *statement, MYSQL_RES *metadata, MYSQL_BIND *results, char **buffers, DatabaseRowCallback callback, void *context, uint64_t *rowCount)
{
    unsigned int columnCount = mysql_num_fields(metadata);
    MYSQL_FIELD *fields = mysql_fetch_fields(metadata);
    const char *columnNames[columnCount > 0 ? columnCount : 1];
    const char *values[columnCount > 0 ? columnCount : 1];
    unsigned long lengths[columnCount > 0 ? columnCount : 1];
    my_bool isNull[columnCount > 0 ? columnCount : 1];
    DatabaseRow row = {.columnCount = columnCount, .columnNames = columnNames};
    int fetchResult;
    int callbackResult;

    for(unsigned int index = 0; index < columnCount; ++index)
    {
        columnNames[index] = fields[index].name;
        results[index].length = &lengths[index];
        results[index].is_null = &isNull[index];
    }
    if(mysql_stmt_bind_result(statement, results) != 0)
    {
        RLOG_WARN("MySQL result binding failed: %s", mysql_stmt_error(statement));
        return -1;
    }
    callbackResult = callback != NULL ? callback(&row, context) : 0;
    if(callbackResult != 0)
    {
        return callbackResult > 0 ? 0 : -1;
    }

    while((fetchResult = mysql_stmt_fetch(statement)) == 0)
    {
        for(unsigned int index = 0; index < columnCount; ++index)
        {
            buffers[index][lengths[index]] = '\0';
            values[index] = isNull[index] ? NULL : buffers[index];
        }
        row.values = values;
        row.valueLengths = lengths;
        ++*rowCount;
        callbackResult = callback != NULL ? callback(&row, context) : 0;
        if(callbackResult != 0)
        {
            return callbackResult > 0 ? 0 : -1;
        }
    }
    if(fetchResult != MYSQL_NO_DATA)
    {
        RLOG_WARN("MySQL result fetch failed: %s", mysql_stmt_error(statement));
        return -1;
    }
    return 0;
}

int QueryDatabaseValue(const char *query, const DatabaseValue *params, unsigned int paramCount, char *value, size_t valueSize)
{
    DatabaseValueContext context = {.value = value, .valueSize = valueSize};

    if(value == NULL || valueSize == 0)
    {
        RLOG_ERROR("QueryDatabaseValue: invalid value buffer");
        errno = EINVAL;
        return -1;
    }
    value[0] = '\0';
    if(ExecuteDatabaseQuery(query, params, paramCount, CopyFirstValue, &context, NULL) != 0)
    {
        return -1;
    }
    if(context.tooLong)
    {
        RLOG_ERROR("MySQL value longer than %zu bytes [%s]", valueSize - 1, query);
        errno = EOVERFLOW;
        return -1;
    }
    return context.found;
}

static int CopyFirstValue(const DatabaseRow *row, void *context)
{
    DatabaseValueContext *valueContext = (DatabaseValueContext *)context;

    // 열 이름 행은 건너뜀
    if(row->values == NULL)
    {
        return 0;
    }
    if(row->columnCount > 0 && row->values[0] != NULL)
    {
        if(row->valueLengths[0] >= valueContext->valueSize)
        {
            valueContext->tooLong = 1;
            return 1;
        }
        memcpy(valueContext->value, row->values[0], row->valueLengths[0]);
        valueContext->value[row->valueLengths[0]] = '\0';
        valueContext->found = 1;
    }
    return 1;
}

// MySQL 8.0.24+는 idle 연결을 끊은 뒤 ER_CLIENT_INTERACTION_TIMEOUT(4031)을 돌려줌 (libmariadb 헤더에는 없음)
#define MYSQL_ERROR_CLIENT_INTERACTION_TIMEOUT 4031

static int IsConnectionLost(unsigned int errorCode)
{
    return errorCode == CR_SERVER_GONE_ERROR || errorCode == CR_SERVER_LOST || errorCode == MYSQL_ERROR_CLIENT_INTERACTION_TIMEOUT;
}
