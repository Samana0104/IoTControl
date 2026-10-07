#include "IotDatabaseCommand.h"
#include "IotDatabase.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <mysql.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef enum
{
    DATABASE_COMMAND_INSERT,
    DATABASE_COMMAND_UPDATE,
    DATABASE_COMMAND_SELECT
} DatabaseCommandType;

typedef struct _DatabasePrintContext
{
    FILE *output;
} DatabasePrintContext;

static int MatchSqlWord(const char *start, size_t length, const char *word);
static int FindDatabaseCommand(const char *sql, DatabaseCommandType *type);
static int ValidateDatabaseSql(const char *sql, DatabaseCommandType expectedType);
static int ExecuteDatabaseWrite(const char *sql, DatabaseCommandType type, uint64_t *affectedRows);
static void PrintDatabaseValue(FILE *output, const char *value, unsigned long length);
static int PrintDatabaseRow(const DatabaseRow *row, void *context);

static int MatchSqlWord(const char *start, size_t length, const char *word)
{
    return length == strlen(word) && strncasecmp(start, word, length) == 0;
}

static int FindDatabaseCommand(const char *sql, DatabaseCommandType *type)
{
    const char *start;
    size_t length;

    if(sql == NULL)
    {
        return -1;
    }
    while(isspace((unsigned char)*sql))
    {
        ++sql;
    }
    start = sql;
    while(isalnum((unsigned char)*sql) || *sql == '_')
    {
        ++sql;
    }
    length = (size_t)(sql - start);
    if(MatchSqlWord(start, length, "INSERT"))
    {
        *type = DATABASE_COMMAND_INSERT;
    }
    else if(MatchSqlWord(start, length, "UPDATE"))
    {
        *type = DATABASE_COMMAND_UPDATE;
    }
    else if(MatchSqlWord(start, length, "SELECT"))
    {
        *type = DATABASE_COMMAND_SELECT;
    }
    else
    {
        return -1;
    }
    return 0;
}

static int ValidateDatabaseSql(const char *sql, DatabaseCommandType expectedType)
{
    DatabaseCommandType actualType;
    size_t length;
    size_t depth = 0;
    int hasWhere = 0;
    char quote = '\0';

    if(sql == NULL || (length = strnlen(sql, DATABASE_COMMAND_MAX_SQL_SIZE + 1)) == 0 || length > DATABASE_COMMAND_MAX_SQL_SIZE || FindDatabaseCommand(sql, &actualType) != 0 || actualType != expectedType)
    {
        errno = EINVAL;
        return -1;
    }
    for(size_t index = 0; index < length; ++index)
    {
        unsigned char character = (unsigned char)sql[index];

        /* Avoid ambiguity with NO_BACKSLASH_ESCAPES and executable SQL comments. */
        if(character == '\\')
        {
            errno = EINVAL;
            return -1;
        }
        if(quote != '\0')
        {
            if(character == (unsigned char)quote)
            {
                if(index + 1 < length && sql[index + 1] == quote)
                {
                    ++index;
                }
                else
                {
                    quote = '\0';
                }
            }
            continue;
        }
        if(character == '\'' || character == '"' || character == '`')
        {
            quote = (char)character;
        }
        else if(character == '#' || (character == '-' && sql[index + 1] == '-') || (character == '/' && sql[index + 1] == '*'))
        {
            errno = EINVAL;
            return -1;
        }
        else if(character == ';')
        {
            for(size_t trailing = index + 1; trailing < length; ++trailing)
            {
                if(!isspace((unsigned char)sql[trailing]))
                {
                    errno = EINVAL;
                    return -1;
                }
            }
            break;
        }
        else if(character == '(')
        {
            ++depth;
        }
        else if(character == ')')
        {
            if(depth == 0)
            {
                errno = EINVAL;
                return -1;
            }
            --depth;
        }
        else if(isalnum(character) || character == '_' || character == '$')
        {
            size_t start = index;

            while(index + 1 < length && (isalnum((unsigned char)sql[index + 1]) || sql[index + 1] == '_' || sql[index + 1] == '$'))
            {
                ++index;
            }
            size_t wordLength = index - start + 1;
            if(depth == 0 && (start == 0 || (sql[start - 1] != '@' && sql[start - 1] != '.')) && MatchSqlWord(sql + start, wordLength, "WHERE"))
            {
                hasWhere = 1;
            }
            if(expectedType == DATABASE_COMMAND_SELECT && (MatchSqlWord(sql + start, wordLength, "INTO") || MatchSqlWord(sql + start, wordLength, "FOR") || MatchSqlWord(sql + start, wordLength, "LOCK") || MatchSqlWord(sql + start, wordLength, "LOAD_FILE")))
            {
                errno = EINVAL;
                return -1;
            }
            if(expectedType != DATABASE_COMMAND_SELECT && MatchSqlWord(sql + start, wordLength, "RETURNING"))
            {
                errno = EINVAL;
                return -1;
            }
        }
    }
    if(quote != '\0' || depth != 0 || (expectedType == DATABASE_COMMAND_UPDATE && !hasWhere))
    {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int ExecuteDatabaseWrite(const char *sql, DatabaseCommandType type, uint64_t *affectedRows)
{
    MYSQL *connection;
    my_ulonglong count;
    int result = -1;

    if(affectedRows != NULL)
    {
        *affectedRows = 0;
    }
    if(ValidateDatabaseSql(sql, type) != 0)
    {
        return -1;
    }
    connection = OpenDatabaseConnection();
    if(connection == NULL)
    {
        errno = EIO;
        return -1;
    }
    if(mysql_real_query(connection, sql, (unsigned long)strlen(sql)) != 0)
    {
        /* Do not echo SQL or server error text that may contain credential values. */
        fprintf(stderr, "Database write failed (MariaDB error %u)\n", mysql_errno(connection));
        goto cleanup;
    }
    if(mysql_field_count(connection) != 0)
    {
        fputs("Database write returned unexpected result columns\n", stderr);
        goto cleanup;
    }
    count = mysql_affected_rows(connection);
    if(count == (my_ulonglong)-1)
    {
        goto cleanup;
    }
    if(affectedRows != NULL)
    {
        *affectedRows = (uint64_t)count;
    }
    result = 0;

cleanup:
    CloseDatabaseConnection(connection);
    if(result != 0)
    {
        errno = EIO;
    }
    return result;
}

int InsertDatabaseData(const char *sql, uint64_t *affectedRows)
{
    return ExecuteDatabaseWrite(sql, DATABASE_COMMAND_INSERT, affectedRows);
}

int UpdateDatabaseData(const char *sql, uint64_t *affectedRows)
{
    return ExecuteDatabaseWrite(sql, DATABASE_COMMAND_UPDATE, affectedRows);
}

int SelectDatabaseData(const char *sql, DatabaseRowCallback callback, void *context, uint64_t *rowCount)
{
    MYSQL *connection = NULL;
    MYSQL_RES *resultSet = NULL;
    MYSQL_FIELD *fields;
    MYSQL_ROW values;
    const char **columnNames = NULL;
    DatabaseRow row = {0};
    uint64_t count = 0;
    int result = -1;
    int callbackResult;

    if(rowCount != NULL)
    {
        *rowCount = 0;
    }
    if(ValidateDatabaseSql(sql, DATABASE_COMMAND_SELECT) != 0)
    {
        return -1;
    }
    connection = OpenDatabaseConnection();
    if(connection == NULL)
    {
        errno = EIO;
        return -1;
    }
    if(mysql_real_query(connection, sql, (unsigned long)strlen(sql)) != 0 || (resultSet = mysql_use_result(connection)) == NULL)
    {
        fprintf(stderr, "Database select failed (MariaDB error %u)\n", mysql_errno(connection));
        goto cleanup;
    }
    row.columnCount = mysql_num_fields(resultSet);
    fields = mysql_fetch_fields(resultSet);
    columnNames = calloc(row.columnCount, sizeof(*columnNames));
    if(columnNames == NULL || fields == NULL)
    {
        goto cleanup;
    }
    for(unsigned int index = 0; index < row.columnCount; ++index)
    {
        columnNames[index] = fields[index].name;
    }
    row.columnNames = columnNames;
    callbackResult = callback != NULL ? callback(&row, context) : 0;
    if(callbackResult != 0)
    {
        result = callbackResult > 0 ? 0 : -1;
        goto cleanup;
    }
    while((values = mysql_fetch_row(resultSet)) != NULL)
    {
        row.values = (const char *const *)values;
        row.valueLengths = mysql_fetch_lengths(resultSet);
        if(row.valueLengths == NULL)
        {
            goto cleanup;
        }
        ++count;
        callbackResult = callback != NULL ? callback(&row, context) : 0;
        if(callbackResult != 0)
        {
            result = callbackResult > 0 ? 0 : -1;
            goto cleanup;
        }
    }
    if(mysql_errno(connection) == 0)
    {
        result = 0;
    }

cleanup:
    free(columnNames);
    if(resultSet != NULL)
    {
        mysql_free_result(resultSet);
    }
    CloseDatabaseConnection(connection);
    if(rowCount != NULL)
    {
        *rowCount = count;
    }
    if(result != 0)
    {
        errno = EIO;
    }
    return result;
}

static void PrintDatabaseValue(FILE *output, const char *value, unsigned long length)
{
    if(value == NULL)
    {
        fputs("NULL", output);
        return;
    }
    for(unsigned long index = 0; index < length; ++index)
    {
        unsigned char character = (unsigned char)value[index];

        if(character < 0x20 || character == 0x7F || character == '\\')
        {
            fprintf(output, "\\x%02X", character);
        }
        else
        {
            fputc(character, output);
        }
    }
}

static int PrintDatabaseRow(const DatabaseRow *row, void *context)
{
    DatabasePrintContext *printContext = context;

    for(unsigned int index = 0; index < row->columnCount; ++index)
    {
        if(index != 0)
        {
            fputc('\t', printContext->output);
        }
        const char *value = row->values != NULL ? row->values[index] : row->columnNames[index];
        unsigned long length = row->values != NULL ? row->valueLengths[index] : (unsigned long)strlen(value);
        PrintDatabaseValue(printContext->output, value, length);
    }
    fputc('\n', printContext->output);
    return ferror(printContext->output) ? -1 : 0;
}

int ExecuteDatabaseCliCommand(const char *sql, FILE *output)
{
    DatabaseCommandType type;
    DatabasePrintContext context = {.output = output};
    uint64_t count;
    int result;

    if(output == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    if(FindDatabaseCommand(sql, &type) != 0 || ValidateDatabaseSql(sql, type) != 0)
    {
        fputs("Usage: db <INSERT/UPDATE/SELECT SQL>; one statement only; UPDATE needs WHERE. Comments and backslashes are not supported.\n", output);
        errno = EINVAL;
        return -1;
    }
    result = type == DATABASE_COMMAND_SELECT ? SelectDatabaseData(sql, PrintDatabaseRow, &context, &count) : type == DATABASE_COMMAND_INSERT ? InsertDatabaseData(sql, &count) : UpdateDatabaseData(sql, &count);
    if(result != 0)
    {
        fputs("DB command failed. Check SQL syntax, constraints and DB permissions.\n", output);
    }
    else
    {
        fprintf(output, "%s: %" PRIu64 " row(s).\n", type == DATABASE_COMMAND_SELECT ? "Selected" : "Affected", count);
    }
    if(fflush(output) != 0 || ferror(output))
    {
        return -1;
    }
    return result;
}
