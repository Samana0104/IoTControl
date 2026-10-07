#include "RDatabase.h"
#include "RConfig.h"
#include "RDatabaseQuery.h"
#include "RLog.h"

#include <errno.h>
#include <mysql.h>
#include <pthread.h>
#include <sodium.h>
#include <string.h>

#define PASSWORD_HASH_BUFFER_SIZE 256

static char dummyPasswordHash[crypto_pwhash_STRBYTES];
static int databaseInitialized;
// 스레드마다 MySQL 연결 하나를 열어 두고 재사용. 스레드가 끝나면 소멸자가 닫음
static pthread_key_t connectionKey;
static pthread_once_t connectionKeyOnce = PTHREAD_ONCE_INIT;
static int connectionKeyReady;

static MYSQL *ConnectDatabase(void);
static void CreateConnectionKey(void);
static void CloseThreadConnection(void *connection);

int InitializeDatabase(void)
{
    static const char DUMMY_PASSWORD[] = "invalid-password";
    MYSQL *connection;

    databaseInitialized = 0;

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
        RLOG_ERROR("MySQL client library initialization failed");
        return -1;
    }

    // 설정이 맞는지 시작할 때 한 번 접속해 봄
    databaseInitialized = 1;
    connection = ConnectDatabase();
    if(connection == NULL)
    {
        databaseInitialized = 0;
        return -1;
    }
    mysql_close(connection);
    return 0;
}

MYSQL *GetDatabaseConnection(void)
{
    MYSQL *connection;

    pthread_once(&connectionKeyOnce, CreateConnectionKey);
    if(!connectionKeyReady)
    {
        RLOG_ERROR("MySQL connection key creation failed");
        return NULL;
    }
    connection = pthread_getspecific(connectionKey);
    if(connection != NULL)
    {
        return connection;
    }

    if(mysql_thread_init() != 0)
    {
        RLOG_ERROR("MySQL client thread initialization failed");
        return NULL;
    }
    connection = ConnectDatabase();
    if(connection == NULL)
    {
        mysql_thread_end();
        return NULL;
    }
    // DB 기본값과 관계없이 실행 즉시 반영
    if(mysql_autocommit(connection, 1) != 0 || pthread_setspecific(connectionKey, connection) != 0)
    {
        RLOG_ERROR("MySQL connection setup failed: %s", mysql_error(connection));
        CloseThreadConnection(connection);
        return NULL;
    }
    return connection;
}

void ResetDatabaseConnection(void)
{
    MYSQL *connection;

    if(!connectionKeyReady)
    {
        return;
    }
    connection = pthread_getspecific(connectionKey);
    if(connection != NULL)
    {
        pthread_setspecific(connectionKey, NULL);
        CloseThreadConnection(connection);
    }
}

int VerifyMember(const char *memberId, size_t memberIdLength, const char *password, size_t passwordLength)
{
    char id[MEM_ID_SIZE + 1];
    char passwordHash[PASSWORD_HASH_BUFFER_SIZE];
    DatabaseValue params[1];
    int found;
    int verifyResult;

    if(memberId == NULL || memberIdLength == 0 || memberIdLength > MEM_ID_SIZE || password == NULL || passwordLength == 0)
    {
        return 0;
    }
    memcpy(id, memberId, memberIdLength);
    id[memberIdLength] = '\0';
    params[0] = DATABASE_TEXT(id);

    found = QueryDatabaseValue(QUERY_SELECT_MEMBER_PASSWORD, params, 1, passwordHash, sizeof(passwordHash));
    if(found < 0)
    {
        sodium_memzero(passwordHash, sizeof(passwordHash));
        return -1;
    }
    if(found == 0)
    {
        // 없는 회원도 해시 검증 시간만큼 걸리게 해서 응답 시간으로 회원 존재를 알 수 없게 함
        int dummyResult = crypto_pwhash_str_verify(dummyPasswordHash, password, passwordLength);

        (void)dummyResult;
        return 0;
    }
    verifyResult = crypto_pwhash_str_verify(passwordHash, password, passwordLength) == 0 ? 1 : 0;
    sodium_memzero(passwordHash, sizeof(passwordHash));
    return verifyResult;
}

static MYSQL *ConnectDatabase(void)
{
    const RDatabaseConfig *database = &RConfigGet()->database;
    unsigned int timeoutSeconds = (unsigned int)database->timeoutSeconds;
    MYSQL *connection;

    if(!databaseInitialized)
    {
        RLOG_ERROR("Database is not initialized");
        return NULL;
    }

    connection = mysql_init(NULL);
    if(connection == NULL)
    {
        RLOG_ERROR("MySQL connection initialization failed");
        return NULL;
    }

    mysql_options(connection, MYSQL_OPT_CONNECT_TIMEOUT, &timeoutSeconds);
    mysql_options(connection, MYSQL_OPT_READ_TIMEOUT, &timeoutSeconds);
    mysql_options(connection, MYSQL_OPT_WRITE_TIMEOUT, &timeoutSeconds);

    if(mysql_real_connect(connection, database->host, database->user, database->password, database->name, (unsigned int)database->port, NULL, 0) == NULL)
    {
        RLOG_ERROR("MySQL connection failed: %s", mysql_error(connection));
        mysql_close(connection);
        return NULL;
    }

    return connection;
}

int RegisterMember(const char *memberId, const char *password, const char *type)
{
    static const char *const MEMBER_TYPES[] = {"STM32", "ARDUINO", "PC"};
    char passwordHash[crypto_pwhash_STRBYTES];
    char existingId[MEM_ID_SIZE + 1];
    DatabaseValue params[3];
    size_t memberIdLength;
    uint64_t insertedRows = 0;
    int typeKnown = 0;
    int found;
    int result;

    if(memberId == NULL || password == NULL || type == NULL)
    {
        RLOG_ERROR("RegisterMember: NULL argument");
        return -1;
    }
    memberIdLength = strlen(memberId);
    for(size_t index = 0; index < sizeof(MEMBER_TYPES) / sizeof(MEMBER_TYPES[0]); ++index)
    {
        typeKnown |= strcmp(type, MEMBER_TYPES[index]) == 0;
    }
    if(memberIdLength == 0 || memberIdLength > MEM_ID_SIZE || password[0] == '\0' || strlen(password) > MEM_PW_SIZE || !typeKnown)
    {
        RLOG_WARN("Member sign-up rejected: invalid ID, password or type");
        return -1;
    }

    params[0] = DATABASE_TEXT(memberId);
    found = QueryDatabaseValue(QUERY_SELECT_MEMBER, params, 1, existingId, sizeof(existingId));
    if(found != 0)
    {
        return found > 0 ? 0 : -1;
    }

    // VerifyMember가 같은 libsodium 함수로 검증
    if(crypto_pwhash_str(passwordHash, password, strlen(password), crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0)
    {
        RLOG_ERROR("Password hashing failed (out of memory)");
        return -1;
    }
    params[1] = DATABASE_TEXT(passwordHash);
    params[2] = DATABASE_TEXT(type);
    result = ExecuteDatabaseQuery(QUERY_INSERT_MEMBER, params, 3, NULL, NULL, &insertedRows);
    sodium_memzero(passwordHash, sizeof(passwordHash));
    if(result != 0 || insertedRows != 1)
    {
        return -1;
    }
    RLOG_INFO("Member signed up: id=%s, type=%s", memberId, type);
    return 1;
}

static void CreateConnectionKey(void)
{
    connectionKeyReady = pthread_key_create(&connectionKey, CloseThreadConnection) == 0;
}

static void CloseThreadConnection(void *connection)
{
    mysql_close((MYSQL *)connection);
    mysql_thread_end();
}
