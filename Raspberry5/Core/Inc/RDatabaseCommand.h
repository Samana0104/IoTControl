#pragma once

#include <stdint.h>
#include <stdio.h>

#define DATABASE_COMMAND_MAX_SQL_SIZE 4096

typedef struct _DatabaseRow
{
    unsigned int columnCount;
    const char *const *columnNames;
    /* values == NULL: column metadata. Otherwise NULL values[i] means SQL NULL. */
    const char *const *values;
    const unsigned long *valueLengths;
} DatabaseRow;

/* Callback receives borrowed data valid only during the call.
   Return 0 to continue, 1 to finish early, -1 to report a processing failure.
   Do not run nested DB operations from this callback. */
typedef int (*DatabaseRowCallback)(const DatabaseRow *row, void *context);

/* For trusted local administrative SQL, not arbitrary remote input.
   InitializeDatabase() must succeed first. Returns 0 on success, -1 on error.
   Only one statement is allowed; comments/backslashes are not supported.
   UPDATE requires a top-level WHERE; SELECT INTO/locking clauses are rejected. */
int InsertDatabaseData(const char *sql, uint64_t *affectedRows);
int UpdateDatabaseData(const char *sql, uint64_t *affectedRows);
int SelectDatabaseData(const char *sql, DatabaseRowCallback callback, void *context, uint64_t *rowCount);
/* Accepts SQL starting with insert/update/select (without the CLI's 'db' prefix). */
int ExecuteDatabaseCliCommand(const char *sql, FILE *output);
