#include "IotDatabase.h"

#include <errno.h>
#include <mysql.h>
#include <mysqld_error.h>
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DB_DEFAULT_HOST "127.0.0.1"
#define DB_DEFAULT_PORT 3306
#define DB_TIMEOUT_SECONDS 5
#define PASSWORD_HASH_BUFFER_SIZE 256

static const char MEMBER_QUERY[] = "SELECT pw_hash FROM member WHERE id = ? LIMIT 1";
static const char BLUETOOTH_DEVICE_QUERY[] = "SELECT mac_address FROM bluetooth WHERE id = ? LIMIT 1";
static const char BLUETOOTH_REGISTER_QUERY[] = "INSERT INTO bluetooth(id, mac_address) VALUES(?, ?)";
static char dummyPasswordHash[crypto_pwhash_STRBYTES];

static unsigned int GetDatabasePort(void);
static const char *GetEnvironmentOrDefault(const char *name, const char *defaultValue);
static MYSQL *ConnectDatabase(void);

int InitializeDatabase(void)
{
    static const char DUMMY_PASSWORD[] = "invalid-password";
    MYSQL *connection;

    if(getenv("IOT_DB_USER") == NULL || getenv("IOT_DB_PASSWORD") == NULL || getenv("IOT_DB_NAME") == NULL)
    {
        fputs("IOT_DB_USER, IOT_DB_PASSWORD and IOT_DB_NAME must be set\n", stderr);
        return -1;
    }

    if(sodium_init() < 0)
    {
        fputs("libsodium initialization failed\n", stderr);
        return -1;
    }

    if(crypto_pwhash_str(dummyPasswordHash, DUMMY_PASSWORD, sizeof(DUMMY_PASSWORD) - 1, crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0)
    {
        fputs("dummy password hash initialization failed\n", stderr);
        return -1;
    }

    if(mysql_library_init(0, NULL, NULL) != 0)
    {
        fputs("MariaDB client initialization failed\n", stderr);
        return -1;
    }

    connection = ConnectDatabase();
    if(connection == NULL)
    {
        return -1;
    }

    mysql_close(connection);
    return 0;
}

int VerifyMember(const char *memberId, size_t memberIdLength, const char *password, size_t passwordLength)
{
    MYSQL *connection = NULL;
    MYSQL_STMT *statement = NULL;
    MYSQL_BIND parameterBind[1];
    MYSQL_BIND resultBind[1];
    unsigned long parameterLength = (unsigned long)memberIdLength;
    unsigned long resultLength = 0;
    my_bool resultIsNull = 0;
    my_bool resultError = 0;
    char passwordHash[PASSWORD_HASH_BUFFER_SIZE];
    int fetchResult;
    int verifyResult = -1;

    if(memberId == NULL || memberIdLength == 0 || password == NULL || passwordLength == 0)
    {
        return 0;
    }

    if(mysql_thread_init() != 0)
    {
        fputs("MariaDB thread initialization failed\n", stderr);
        return -1;
    }

    connection = ConnectDatabase();
    if(connection == NULL)
    {
        goto cleanup;
    }

    statement = mysql_stmt_init(connection);
    if(statement == NULL)
    {
        fputs("MariaDB statement initialization failed\n", stderr);
        goto cleanup;
    }

    if(mysql_stmt_prepare(statement, MEMBER_QUERY, sizeof(MEMBER_QUERY) - 1) != 0)
    {
        fprintf(stderr, "MariaDB statement prepare failed: %s\n", mysql_stmt_error(statement));
        goto cleanup;
    }

    memset(parameterBind, 0, sizeof(parameterBind));
    parameterBind[0].buffer_type = MYSQL_TYPE_STRING;
    parameterBind[0].buffer = (void *)memberId;
    parameterBind[0].buffer_length = parameterLength;
    parameterBind[0].length = &parameterLength;

    if(mysql_stmt_bind_param(statement, parameterBind) != 0 || mysql_stmt_execute(statement) != 0)
    {
        fprintf(stderr, "MariaDB member query failed: %s\n", mysql_stmt_error(statement));
        goto cleanup;
    }

    memset(passwordHash, 0, sizeof(passwordHash));
    memset(resultBind, 0, sizeof(resultBind));
    resultBind[0].buffer_type = MYSQL_TYPE_STRING;
    resultBind[0].buffer = passwordHash;
    resultBind[0].buffer_length = sizeof(passwordHash) - 1;
    resultBind[0].length = &resultLength;
    resultBind[0].is_null = &resultIsNull;
    resultBind[0].error = &resultError;

    if(mysql_stmt_bind_result(statement, resultBind) != 0 || mysql_stmt_store_result(statement) != 0)
    {
        fprintf(stderr, "MariaDB result binding failed: %s\n", mysql_stmt_error(statement));
        goto cleanup;
    }

    fetchResult = mysql_stmt_fetch(statement);
    if(fetchResult == MYSQL_NO_DATA)
    {
        int dummyVerifyResult = crypto_pwhash_str_verify(dummyPasswordHash, password, passwordLength);

        (void)dummyVerifyResult;
        verifyResult = 0;
        goto cleanup;
    }

    if(fetchResult != 0 || resultIsNull || resultError || resultLength >= sizeof(passwordHash))
    {
        fprintf(stderr, "MariaDB member result fetch failed: %s\n", mysql_stmt_error(statement));
        goto cleanup;
    }

    passwordHash[resultLength] = '\0';
    verifyResult = crypto_pwhash_str_verify(passwordHash, password, passwordLength) == 0 ? 1 : 0;

cleanup:
    sodium_memzero(passwordHash, sizeof(passwordHash));
    if(statement != NULL)
    {
        mysql_stmt_close(statement);
    }
    if(connection != NULL)
    {
        mysql_close(connection);
    }
    mysql_thread_end();
    return verifyResult;
}

int GetMemberBluetoothDevice(const char *memberId, size_t memberIdLength, BluetoothDeviceRecord *deviceRecord)
{
    MYSQL *connection = NULL;
    MYSQL_STMT *statement = NULL;
    MYSQL_BIND parameterBind[1];
    MYSQL_BIND resultBind[1];
    unsigned long parameterLength = (unsigned long)memberIdLength;
    unsigned long macLength = 0;
    my_bool resultIsNull = 0;
    my_bool resultError = 0;
    int fetchResult;
    int queryResult = -1;

    if(memberId == NULL || memberIdLength == 0 || deviceRecord == NULL)
    {
        return -1;
    }

    memset(deviceRecord, 0, sizeof(*deviceRecord));
    if(mysql_thread_init() != 0)
    {
        fputs("MariaDB thread initialization failed\n", stderr);
        return -1;
    }

    connection = ConnectDatabase();
    if(connection == NULL)
    {
        goto cleanup;
    }

    statement = mysql_stmt_init(connection);
    if(statement == NULL)
    {
        fputs("MariaDB statement initialization failed\n", stderr);
        goto cleanup;
    }

    if(mysql_stmt_prepare(statement, BLUETOOTH_DEVICE_QUERY, sizeof(BLUETOOTH_DEVICE_QUERY) - 1) != 0)
    {
        fprintf(stderr, "MariaDB Bluetooth query prepare failed: %s\n", mysql_stmt_error(statement));
        goto cleanup;
    }

    memset(parameterBind, 0, sizeof(parameterBind));
    parameterBind[0].buffer_type = MYSQL_TYPE_STRING;
    parameterBind[0].buffer = (void *)memberId;
    parameterBind[0].buffer_length = parameterLength;
    parameterBind[0].length = &parameterLength;
    if(mysql_stmt_bind_param(statement, parameterBind) != 0 || mysql_stmt_execute(statement) != 0)
    {
        fprintf(stderr, "MariaDB Bluetooth query failed: %s\n", mysql_stmt_error(statement));
        goto cleanup;
    }

    memset(resultBind, 0, sizeof(resultBind));
    resultBind[0].buffer_type = MYSQL_TYPE_STRING;
    resultBind[0].buffer = deviceRecord->mac;
    resultBind[0].buffer_length = sizeof(deviceRecord->mac) - 1;
    resultBind[0].length = &macLength;
    resultBind[0].is_null = &resultIsNull;
    resultBind[0].error = &resultError;
    if(mysql_stmt_bind_result(statement, resultBind) != 0 || mysql_stmt_store_result(statement) != 0)
    {
        fprintf(stderr, "MariaDB Bluetooth result binding failed: %s\n", mysql_stmt_error(statement));
        goto cleanup;
    }

    fetchResult = mysql_stmt_fetch(statement);
    if(fetchResult == MYSQL_NO_DATA)
    {
        queryResult = 0;
        goto cleanup;
    }
    if(fetchResult != 0 || resultIsNull || resultError || macLength >= sizeof(deviceRecord->mac))
    {
        fprintf(stderr, "MariaDB Bluetooth result fetch failed: %s\n", mysql_stmt_error(statement));
        goto cleanup;
    }

    deviceRecord->mac[macLength] = '\0';
    queryResult = 1;

cleanup:
    if(statement != NULL)
    {
        mysql_stmt_close(statement);
    }
    if(connection != NULL)
    {
        mysql_close(connection);
    }
    mysql_thread_end();
    return queryResult;
}

int RegisterMemberBluetoothDevice(const char *memberId, size_t memberIdLength, const char *bluetoothMac, size_t bluetoothMacLength)
{
    MYSQL *connection = NULL;
    MYSQL_STMT *statement = NULL;
    MYSQL_BIND parameterBind[2];
    unsigned long parameterLength[2];
    int registerResult = -1;

    if(memberId == NULL || memberIdLength == 0 || bluetoothMac == NULL || bluetoothMacLength != BLUETOOTH_MAC_SIZE)
    {
        return -1;
    }

    if(mysql_thread_init() != 0)
    {
        fputs("MariaDB thread initialization failed\n", stderr);
        return -1;
    }

    connection = ConnectDatabase();
    if(connection == NULL)
    {
        goto cleanup;
    }

    statement = mysql_stmt_init(connection);
    if(statement == NULL)
    {
        fputs("MariaDB statement initialization failed\n", stderr);
        goto cleanup;
    }

    if(mysql_stmt_prepare(statement, BLUETOOTH_REGISTER_QUERY, sizeof(BLUETOOTH_REGISTER_QUERY) - 1) != 0)
    {
        fprintf(stderr, "MariaDB Bluetooth register prepare failed: %s\n", mysql_stmt_error(statement));
        goto cleanup;
    }

    parameterLength[0] = (unsigned long)memberIdLength;
    parameterLength[1] = (unsigned long)bluetoothMacLength;
    memset(parameterBind, 0, sizeof(parameterBind));
    parameterBind[0].buffer_type = MYSQL_TYPE_STRING;
    parameterBind[0].buffer = (void *)memberId;
    parameterBind[0].buffer_length = parameterLength[0];
    parameterBind[0].length = &parameterLength[0];
    parameterBind[1].buffer_type = MYSQL_TYPE_STRING;
    parameterBind[1].buffer = (void *)bluetoothMac;
    parameterBind[1].buffer_length = parameterLength[1];
    parameterBind[1].length = &parameterLength[1];

    if(mysql_stmt_bind_param(statement, parameterBind) != 0)
    {
        fprintf(stderr, "MariaDB Bluetooth register binding failed: %s\n", mysql_stmt_error(statement));
        goto cleanup;
    }
    if(mysql_stmt_execute(statement) != 0)
    {
        if(mysql_stmt_errno(statement) == ER_DUP_ENTRY)
        {
            registerResult = 0;
            goto cleanup;
        }
        fprintf(stderr, "MariaDB Bluetooth register failed: %s\n", mysql_stmt_error(statement));
        goto cleanup;
    }

    registerResult = 1;

cleanup:
    if(statement != NULL)
    {
        mysql_stmt_close(statement);
    }
    if(connection != NULL)
    {
        mysql_close(connection);
    }
    mysql_thread_end();
    return registerResult;
}

static unsigned int GetDatabasePort(void)
{
    const char *portText = getenv("IOT_DB_PORT");
    char *endPointer;
    unsigned long port;

    if(portText == NULL || *portText == '\0')
    {
        return DB_DEFAULT_PORT;
    }

    errno = 0;
    port = strtoul(portText, &endPointer, 10);
    if(errno != 0 || *endPointer != '\0' || port == 0 || port > 65535)
    {
        return 0;
    }

    return (unsigned int)port;
}

static const char *GetEnvironmentOrDefault(const char *name, const char *defaultValue)
{
    const char *value = getenv(name);

    return value == NULL || *value == '\0' ? defaultValue : value;
}

static MYSQL *ConnectDatabase(void)
{
    MYSQL *connection;
    unsigned int port = GetDatabasePort();
    unsigned int timeoutSeconds = DB_TIMEOUT_SECONDS;

    if(port == 0)
    {
        fputs("Invalid IOT_DB_PORT\n", stderr);
        return NULL;
    }

    connection = mysql_init(NULL);
    if(connection == NULL)
    {
        fputs("MariaDB connection initialization failed\n", stderr);
        return NULL;
    }

    mysql_options(connection, MYSQL_OPT_CONNECT_TIMEOUT, &timeoutSeconds);
    mysql_options(connection, MYSQL_OPT_READ_TIMEOUT, &timeoutSeconds);
    mysql_options(connection, MYSQL_OPT_WRITE_TIMEOUT, &timeoutSeconds);

    if(mysql_real_connect(connection, GetEnvironmentOrDefault("IOT_DB_HOST", DB_DEFAULT_HOST), getenv("IOT_DB_USER"), getenv("IOT_DB_PASSWORD"), getenv("IOT_DB_NAME"), port, NULL, 0) == NULL)
    {
        fprintf(stderr, "MariaDB connection failed: %s\n", mysql_error(connection));
        mysql_close(connection);
        return NULL;
    }

    return connection;
}
