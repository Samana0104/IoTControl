#include "RDatabase.h"
#include "RLog.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <mysql.h>
#include <mysqld_error.h>
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DB_CONFIG_FILE "db_config.txt"
#define DB_CONFIG_VALUE_SIZE 256
#define DB_CONFIG_PASSWORD_SIZE 1024
#define DB_CONFIG_LINE_SIZE 2048
#define DB_DEFAULT_HOST "127.0.0.1"
#define DB_DEFAULT_PORT 3306
#define DB_TIMEOUT_SECONDS 5
#define PASSWORD_HASH_BUFFER_SIZE 256

typedef struct _DatabaseConfig
{
    char host[DB_CONFIG_VALUE_SIZE];
    char user[DB_CONFIG_VALUE_SIZE];
    char password[DB_CONFIG_PASSWORD_SIZE];
    char name[DB_CONFIG_VALUE_SIZE];
    unsigned int port;
} DatabaseConfig;

typedef struct _DatabaseConfigField
{
    const char *key;
    char *value;
    size_t valueSize;
    int required;
    int allowEmpty;
    int seen;
} DatabaseConfigField;

static const char MEMBER_QUERY[] = "SELECT pw_hash FROM member WHERE id = ? LIMIT 1";
static const char BLUETOOTH_DEVICE_QUERY[] = "SELECT mac_address FROM bluetooth WHERE id = ? LIMIT 1";
static const char BLUETOOTH_REGISTER_QUERY[] = "INSERT INTO bluetooth(id, mac_address) VALUES(?, ?)";
static const char DHT_UPDATE_QUERY[] = "UPDATE dht SET temp = ?, humi = ? WHERE id = ?";
static const char FAN_UPDATE_QUERY[] = "UPDATE fan SET speed = ? WHERE singleton_id = 1";
static const char CON_UPDATE_QUERY[] = "UPDATE con_data SET temp = ? WHERE singleton_id = 1";
static char dummyPasswordHash[crypto_pwhash_STRBYTES];
static DatabaseConfig databaseConfig;
static int databaseConfigLoaded;

static char *TrimDatabaseConfigText(char *text);
static int ReadDatabaseConfigLine(FILE *file, char *line, size_t lineSize);
static int LoadDatabaseConfig(const char *filePath, DatabaseConfig *config);
static MYSQL *ConnectDatabase(void);
static int ExecuteSensorUpdate(const char *query, MYSQL_BIND *parameters, uint64_t *affectedRows);

int InitializeDatabase(void)
{
    static const char DUMMY_PASSWORD[] = "invalid-password";
    DatabaseConfig loadedConfig;
    MYSQL *connection;

    databaseConfigLoaded = 0;
    sodium_memzero(&databaseConfig, sizeof(databaseConfig));
    if(LoadDatabaseConfig(DB_CONFIG_FILE, &loadedConfig) != 0)
    {
        return -1;
    }
    databaseConfig = loadedConfig;
    databaseConfigLoaded = 1;
    sodium_memzero(&loadedConfig, sizeof(loadedConfig));

    if(sodium_init() < 0)
    {
        RLOG_ERROR("libsodium initialization failed");
        return -1;
    }

    if(crypto_pwhash_str(dummyPasswordHash, DUMMY_PASSWORD, sizeof(DUMMY_PASSWORD) - 1, crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0)
    {
        RLOG_ERROR("dummy password hash initialization failed");
        return -1;
    }

    if(mysql_library_init(0, NULL, NULL) != 0)
    {
        RLOG_ERROR("MariaDB client initialization failed");
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

MYSQL *OpenDatabaseConnection(void)
{
    MYSQL *connection;

    if(mysql_thread_init() != 0)
    {
        RLOG_ERROR("MariaDB thread initialization failed");
        return NULL;
    }
    connection = ConnectDatabase();
    if(connection == NULL)
    {
        mysql_thread_end();
    }
    return connection;
}

void CloseDatabaseConnection(MYSQL *connection)
{
    if(connection != NULL)
    {
        mysql_close(connection);
        mysql_thread_end();
    }
}

static int ExecuteSensorUpdate(const char *query, MYSQL_BIND *parameters, uint64_t *affectedRows)
{
    MYSQL *connection;
    MYSQL_STMT *statement = NULL;
    my_ulonglong count;
    int result = -1;

    if(affectedRows != NULL)
    {
        *affectedRows = 0;
    }
    connection = OpenDatabaseConnection();
    if(connection == NULL)
    {
        errno = EIO;
        return -1;
    }
    /* This dedicated connection must persist the UPDATE even if the DB default differs. */
    if(mysql_autocommit(connection, 1) != 0)
    {
        RLOG_ERROR("Sensor DB autocommit failed (MariaDB error %u)", mysql_errno(connection));
        goto cleanup;
    }
    statement = mysql_stmt_init(connection);
    if(statement == NULL)
    {
        RLOG_ERROR("Sensor DB statement initialization failed");
        goto cleanup;
    }
    if(mysql_stmt_prepare(statement, query, (unsigned long)strlen(query)) != 0 || mysql_stmt_bind_param(statement, parameters) != 0 || mysql_stmt_execute(statement) != 0)
    {
        RLOG_WARN("Sensor DB UPDATE failed (MariaDB error %u)", mysql_stmt_errno(statement));
        goto cleanup;
    }
    count = mysql_stmt_affected_rows(statement);
    if(count == (my_ulonglong)-1)
    {
        RLOG_ERROR("Sensor DB UPDATE affected-row retrieval failed");
        goto cleanup;
    }
    if(affectedRows != NULL)
    {
        *affectedRows = (uint64_t)count;
    }
    result = 0;

cleanup:
    if(statement != NULL)
    {
        mysql_stmt_close(statement);
    }
    CloseDatabaseConnection(connection);
    if(result != 0)
    {
        errno = EIO;
    }
    return result;
}

int UpdateDhtData(const char *memberId, size_t memberIdLength, const DhtData *data, uint64_t *affectedRows)
{
    MYSQL_BIND parameters[3] = {0};
    unsigned long idLength = (unsigned long)memberIdLength;

    if(affectedRows != NULL)
    {
        *affectedRows = 0;
    }
    if(memberId == NULL || memberIdLength == 0 || memberIdLength > MEM_ID_SIZE || data == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    parameters[0].buffer_type = MYSQL_TYPE_SHORT;
    parameters[0].buffer = (void *)&data->temp;
    parameters[0].buffer_length = sizeof(data->temp);
    parameters[0].is_unsigned = 1;
    parameters[1].buffer_type = MYSQL_TYPE_SHORT;
    parameters[1].buffer = (void *)&data->humi;
    parameters[1].buffer_length = sizeof(data->humi);
    parameters[1].is_unsigned = 1;
    parameters[2].buffer_type = MYSQL_TYPE_STRING;
    parameters[2].buffer = (void *)memberId;
    parameters[2].buffer_length = idLength;
    parameters[2].length = &idLength;
    return ExecuteSensorUpdate(DHT_UPDATE_QUERY, parameters, affectedRows);
}

int UpdateFanData(const FanData *data, uint64_t *affectedRows)
{
    MYSQL_BIND parameters[1] = {0};

    if(affectedRows != NULL)
    {
        *affectedRows = 0;
    }
    if(data == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    parameters[0].buffer_type = MYSQL_TYPE_SHORT;
    parameters[0].buffer = (void *)&data->fanSpeed;
    parameters[0].buffer_length = sizeof(data->fanSpeed);
    parameters[0].is_unsigned = 1;
    return ExecuteSensorUpdate(FAN_UPDATE_QUERY, parameters, affectedRows);
}

int UpdateConData(const ConData *data, uint64_t *affectedRows)
{
    MYSQL_BIND parameters[1] = {0};

    if(affectedRows != NULL)
    {
        *affectedRows = 0;
    }
    if(data == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    parameters[0].buffer_type = MYSQL_TYPE_SHORT;
    parameters[0].buffer = (void *)&data->tempData;
    parameters[0].buffer_length = sizeof(data->tempData);
    parameters[0].is_unsigned = 1;
    return ExecuteSensorUpdate(CON_UPDATE_QUERY, parameters, affectedRows);
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
        RLOG_ERROR("MariaDB thread initialization failed");
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
        RLOG_ERROR("MariaDB statement initialization failed");
        goto cleanup;
    }

    if(mysql_stmt_prepare(statement, MEMBER_QUERY, sizeof(MEMBER_QUERY) - 1) != 0)
    {
        RLOG_ERROR("MariaDB statement prepare failed: %s", mysql_stmt_error(statement));
        goto cleanup;
    }

    memset(parameterBind, 0, sizeof(parameterBind));
    parameterBind[0].buffer_type = MYSQL_TYPE_STRING;
    parameterBind[0].buffer = (void *)memberId;
    parameterBind[0].buffer_length = parameterLength;
    parameterBind[0].length = &parameterLength;

    if(mysql_stmt_bind_param(statement, parameterBind) != 0 || mysql_stmt_execute(statement) != 0)
    {
        RLOG_ERROR("MariaDB member query failed: %s", mysql_stmt_error(statement));
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
        RLOG_ERROR("MariaDB result binding failed: %s", mysql_stmt_error(statement));
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
        RLOG_ERROR("MariaDB member result fetch failed: %s", mysql_stmt_error(statement));
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
        RLOG_ERROR("MariaDB thread initialization failed");
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
        RLOG_ERROR("MariaDB statement initialization failed");
        goto cleanup;
    }

    if(mysql_stmt_prepare(statement, BLUETOOTH_DEVICE_QUERY, sizeof(BLUETOOTH_DEVICE_QUERY) - 1) != 0)
    {
        RLOG_ERROR("MariaDB Bluetooth query prepare failed: %s", mysql_stmt_error(statement));
        goto cleanup;
    }

    memset(parameterBind, 0, sizeof(parameterBind));
    parameterBind[0].buffer_type = MYSQL_TYPE_STRING;
    parameterBind[0].buffer = (void *)memberId;
    parameterBind[0].buffer_length = parameterLength;
    parameterBind[0].length = &parameterLength;
    if(mysql_stmt_bind_param(statement, parameterBind) != 0 || mysql_stmt_execute(statement) != 0)
    {
        RLOG_ERROR("MariaDB Bluetooth query failed: %s", mysql_stmt_error(statement));
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
        RLOG_ERROR("MariaDB Bluetooth result binding failed: %s", mysql_stmt_error(statement));
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
        RLOG_ERROR("MariaDB Bluetooth result fetch failed: %s", mysql_stmt_error(statement));
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
        RLOG_ERROR("MariaDB thread initialization failed");
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
        RLOG_ERROR("MariaDB statement initialization failed");
        goto cleanup;
    }

    if(mysql_stmt_prepare(statement, BLUETOOTH_REGISTER_QUERY, sizeof(BLUETOOTH_REGISTER_QUERY) - 1) != 0)
    {
        RLOG_ERROR("MariaDB Bluetooth register prepare failed: %s", mysql_stmt_error(statement));
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
        RLOG_ERROR("MariaDB Bluetooth register binding failed: %s", mysql_stmt_error(statement));
        goto cleanup;
    }
    if(mysql_stmt_execute(statement) != 0)
    {
        if(mysql_stmt_errno(statement) == ER_DUP_ENTRY)
        {
            registerResult = 0;
            goto cleanup;
        }
        RLOG_ERROR("MariaDB Bluetooth register failed: %s", mysql_stmt_error(statement));
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

static char *TrimDatabaseConfigText(char *text)
{
    size_t length;

    while(isspace((unsigned char)*text))
    {
        ++text;
    }
    length = strlen(text);
    while(length > 0 && isspace((unsigned char)text[length - 1]))
    {
        text[--length] = '\0';
    }
    return text;
}

static int ReadDatabaseConfigLine(FILE *file, char *line, size_t lineSize)
{
    size_t length = 0;
    int character;

    while((character = fgetc(file)) != EOF && character != '\n')
    {
        if(character == '\0' || length + 1 >= lineSize)
        {
            return -1;
        }
        line[length++] = (char)character;
    }
    line[length] = '\0';
    if(ferror(file))
    {
        return -1;
    }
    return character == EOF && length == 0 ? 0 : 1;
}

static int LoadDatabaseConfig(const char *filePath, DatabaseConfig *config)
{
    char line[DB_CONFIG_LINE_SIZE] = {0};
    char portText[16] = {0};
    DatabaseConfigField fields[] =
    {
        {"IOT_DB_HOST", config->host, sizeof(config->host), 0, 0, 0},
        {"IOT_DB_PORT", portText, sizeof(portText), 0, 0, 0},
        {"IOT_DB_USER", config->user, sizeof(config->user), 1, 0, 0},
        {"IOT_DB_PASSWORD", config->password, sizeof(config->password), 1, 1, 0},
        {"IOT_DB_NAME", config->name, sizeof(config->name), 1, 0, 0}
    };
    struct stat fileStatus;
    int fileDescriptor = -1;
    FILE *file = NULL;
    int readResult;
    int result = -1;
    size_t lineNumber = 0;
    unsigned long port;
    char *endPointer;

    memset(config, 0, sizeof(*config));
    memcpy(config->host, DB_DEFAULT_HOST, sizeof(DB_DEFAULT_HOST));
    snprintf(portText, sizeof(portText), "%u", (unsigned int)DB_DEFAULT_PORT);
    fileDescriptor = open(filePath, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if(fileDescriptor < 0)
    {
        RLOG_ERROR("Cannot open DB config file '%s': %s", filePath, strerror(errno));
        goto cleanup;
    }
    if(fstat(fileDescriptor, &fileStatus) != 0 || !S_ISREG(fileStatus.st_mode))
    {
        RLOG_ERROR("DB config must be a regular file");
        goto cleanup;
    }
    if(fileStatus.st_mode & (S_IRWXG | S_IRWXO))
    {
        RLOG_ERROR("DB config permissions are too open. Run: chmod 600 %s", filePath);
        goto cleanup;
    }
    file = fdopen(fileDescriptor, "r");
    if(file == NULL)
    {
        RLOG_ERROR("Cannot read DB config file");
        goto cleanup;
    }
    fileDescriptor = -1;

    while((readResult = ReadDatabaseConfigLine(file, line, sizeof(line))) > 0)
    {
        char *key;
        char *value;
        char *separator;
        size_t valueLength;
        DatabaseConfigField *field = NULL;

        ++lineNumber;
        key = line;
        if(lineNumber == 1 && strncmp(key, "\xEF\xBB\xBF", 3) == 0)
        {
            key += 3;
        }
        key = TrimDatabaseConfigText(key);
        if(*key == '\0' || *key == '#')
        {
            continue;
        }
        separator = strchr(key, '=');
        if(separator == NULL)
        {
            RLOG_ERROR("Expected KEY=value in DB config at line %zu", lineNumber);
            goto cleanup;
        }
        *separator = '\0';
        key = TrimDatabaseConfigText(key);
        value = TrimDatabaseConfigText(separator + 1);
        valueLength = strlen(value);
        if(*value == '\'' || *value == '"')
        {
            if(valueLength < 2 || value[valueLength - 1] != *value)
            {
                RLOG_ERROR("Unmatched quotes in DB config at line %zu", lineNumber);
                goto cleanup;
            }
            value[valueLength - 1] = '\0';
            ++value;
            valueLength -= 2;
        }
        for(size_t index = 0; index < sizeof(fields) / sizeof(fields[0]); ++index)
        {
            if(strcmp(key, fields[index].key) == 0)
            {
                field = &fields[index];
                break;
            }
        }
        if(field == NULL || field->seen)
        {
            RLOG_ERROR("Unknown or duplicate DB config key at line %zu", lineNumber);
            goto cleanup;
        }
        if(valueLength >= field->valueSize || (!field->allowEmpty && valueLength == 0))
        {
            RLOG_ERROR("Invalid value length for %s at line %zu", field->key, lineNumber);
            goto cleanup;
        }
        memcpy(field->value, value, valueLength + 1);
        field->seen = 1;
    }
    if(readResult < 0)
    {
        RLOG_ERROR("Unreadable, binary or oversized DB config line at line %zu", lineNumber + 1);
        goto cleanup;
    }
    for(size_t index = 0; index < sizeof(fields) / sizeof(fields[0]); ++index)
    {
        if(fields[index].required && !fields[index].seen)
        {
            RLOG_ERROR("Missing %s in DB config", fields[index].key);
            goto cleanup;
        }
    }
    for(size_t index = 0; portText[index] != '\0'; ++index)
    {
        if(!isdigit((unsigned char)portText[index]))
        {
            RLOG_ERROR("Invalid IOT_DB_PORT in DB config");
            goto cleanup;
        }
    }
    errno = 0;
    port = strtoul(portText, &endPointer, 10);
    if(errno != 0 || *endPointer != '\0' || port == 0 || port > UINT16_MAX)
    {
        RLOG_ERROR("Invalid IOT_DB_PORT in DB config");
        goto cleanup;
    }
    config->port = (unsigned int)port;
    result = 0;

cleanup:
    if(file != NULL)
    {
        fclose(file);
    }
    if(fileDescriptor >= 0)
    {
        close(fileDescriptor);
    }
    sodium_memzero(line, sizeof(line));
    sodium_memzero(portText, sizeof(portText));
    if(result != 0)
    {
        sodium_memzero(config, sizeof(*config));
    }
    return result;
}

static MYSQL *ConnectDatabase(void)
{
    MYSQL *connection;
    unsigned int timeoutSeconds = DB_TIMEOUT_SECONDS;

    if(!databaseConfigLoaded)
    {
        RLOG_ERROR("DB configuration has not been loaded");
        return NULL;
    }

    connection = mysql_init(NULL);
    if(connection == NULL)
    {
        RLOG_ERROR("MariaDB connection initialization failed");
        return NULL;
    }

    mysql_options(connection, MYSQL_OPT_CONNECT_TIMEOUT, &timeoutSeconds);
    mysql_options(connection, MYSQL_OPT_READ_TIMEOUT, &timeoutSeconds);
    mysql_options(connection, MYSQL_OPT_WRITE_TIMEOUT, &timeoutSeconds);

    if(mysql_real_connect(connection, databaseConfig.host, databaseConfig.user, databaseConfig.password, databaseConfig.name, databaseConfig.port, NULL, 0) == NULL)
    {
        RLOG_ERROR("MariaDB connection failed: %s", mysql_error(connection));
        mysql_close(connection);
        return NULL;
    }

    return connection;
}
