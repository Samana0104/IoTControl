#include "RConfig.h"
#include "RLog.h"

#include <cjson/cJSON.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// DB 비밀번호가 있으므로 소유자 전용 권한(600)일 때만 읽고, 그 권한으로 저장
#define CONFIG_FILE_PATH "ServerConfig.json"
#define CONFIG_FILE_MAX_SIZE 65536

// JSON 키 하나. textSize > 0이면 문자열(char 배열), 0이면 정수(int, minimum..maximum)
typedef struct _ConfigField
{
    const char *key;
    size_t offset;
    size_t textSize;
    int allowEmpty;
    int minimum;
    int maximum;
} ConfigField;

// 설정 파일 안의 구역 하나 ("server": {...}). RConfig 안의 offset 위치에 채움
typedef struct _ConfigSection
{
    const char *name;
    size_t offset;
    const ConfigField *fields;
    size_t fieldCount;
} ConfigSection;

#define CONFIG_TEXT(type, member, allowEmpty) {#member, offsetof(type, member), sizeof(((type *)0)->member), allowEmpty, 0, 0}
#define CONFIG_NUMBER(type, member, minimum, maximum) {#member, offsetof(type, member), 0, 0, minimum, maximum}

static const ConfigField SERVER_FIELDS[] =
{
    CONFIG_TEXT(RServerConfig, ip, 0),
    CONFIG_NUMBER(RServerConfig, port, 1, 65535),
    CONFIG_NUMBER(RServerConfig, workerCount, 1, 64)
};

static const ConfigField DATABASE_FIELDS[] =
{
    CONFIG_TEXT(RDatabaseConfig, host, 0),
    CONFIG_NUMBER(RDatabaseConfig, port, 1, 65535),
    CONFIG_TEXT(RDatabaseConfig, user, 0),
    CONFIG_TEXT(RDatabaseConfig, password, 1),
    CONFIG_TEXT(RDatabaseConfig, name, 0),
    CONFIG_NUMBER(RDatabaseConfig, timeoutSeconds, 1, 3600)
};

static const ConfigField TLS_FIELDS[] =
{
    CONFIG_TEXT(RTlsConfig, certificateFile, 0),
    CONFIG_TEXT(RTlsConfig, privateKeyFile, 0)
};

#define CONFIG_SECTION(name, member, fields) {name, offsetof(RConfig, member), fields, sizeof(fields) / sizeof(fields[0])}

static const ConfigSection SECTIONS[] =
{
    CONFIG_SECTION("server", server, SERVER_FIELDS),
    CONFIG_SECTION("database", database, DATABASE_FIELDS),
    CONFIG_SECTION("tls", tls, TLS_FIELDS)
};

#define CONFIG_SECTION_COUNT (sizeof(SECTIONS) / sizeof(SECTIONS[0]))

static RConfig config;

static char *ReadConfigFile(size_t *length);
static int ParseConfig(const cJSON *json, RConfig *staging);
static int ParseConfigSection(const cJSON *json, const ConfigSection *section, unsigned char *base);
static int ParseConfigField(const cJSON *json, const ConfigSection *section, const ConfigField *field, unsigned char *base);
static const ConfigSection *FindConfigSection(const char *name);
static const ConfigField *FindConfigField(const ConfigSection *section, const char *key);
static cJSON *BuildConfigJson(void);
static int WriteConfigFile(const char *text);

RConfig *RConfigGet(void)
{
    return &config;
}

int RConfigLoad(void)
{
    RConfig staging;
    cJSON *json;
    char *text;
    size_t length;
    int result;

    text = ReadConfigFile(&length);
    if(text == NULL)
    {
        return -1;
    }
    json = cJSON_ParseWithLength(text, length);
    explicit_bzero(text, length);
    free(text);
    if(json == NULL)
    {
        RLOG_ERROR("%s: invalid JSON", CONFIG_FILE_PATH);
        return -1;
    }

    // 모든 구역이 맞을 때만 반영 (실패하면 기존 설정 유지)
    memset(&staging, 0, sizeof(staging));
    result = ParseConfig(json, &staging);
    cJSON_Delete(json);
    if(result == 0)
    {
        config = staging;
        RLOG_INFO("Config loaded: %s", CONFIG_FILE_PATH);
    }
    explicit_bzero(&staging, sizeof(staging));
    return result;
}

int RConfigSave(void)
{
    cJSON *json = BuildConfigJson();
    char *text;
    int result;

    if(json == NULL)
    {
        RLOG_ERROR("%s: JSON build failed", CONFIG_FILE_PATH);
        return -1;
    }
    text = cJSON_Print(json);
    cJSON_Delete(json);
    if(text == NULL)
    {
        RLOG_ERROR("%s: JSON print failed", CONFIG_FILE_PATH);
        return -1;
    }
    result = WriteConfigFile(text);
    // DB 비밀번호가 들어 있음
    explicit_bzero(text, strlen(text));
    cJSON_free(text);
    if(result == 0)
    {
        RLOG_INFO("Config saved: %s", CONFIG_FILE_PATH);
    }
    return result;
}

// 일반 파일이고 소유자 전용 권한일 때만 읽음. 성공하면 malloc한 내용 (호출자가 free)
static char *ReadConfigFile(size_t *length)
{
    struct stat fileStatus;
    char *text;
    size_t readLength = 0;
    int fd = open(CONFIG_FILE_PATH, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);

    if(fd < 0)
    {
        RLOG_ERROR("Cannot open %s: %s", CONFIG_FILE_PATH, strerror(errno));
        return NULL;
    }
    if(fstat(fd, &fileStatus) != 0 || !S_ISREG(fileStatus.st_mode) || fileStatus.st_size <= 0 || fileStatus.st_size > CONFIG_FILE_MAX_SIZE)
    {
        RLOG_ERROR("%s must be a regular file of 1..%d bytes", CONFIG_FILE_PATH, CONFIG_FILE_MAX_SIZE);
        close(fd);
        return NULL;
    }
    if(fileStatus.st_mode & (S_IRWXG | S_IRWXO))
    {
        RLOG_ERROR("%s permissions are too open. Run: chmod 600 %s", CONFIG_FILE_PATH, CONFIG_FILE_PATH);
        close(fd);
        return NULL;
    }
    text = malloc((size_t)fileStatus.st_size);
    if(text == NULL)
    {
        RLOG_ERROR("%s: out of memory", CONFIG_FILE_PATH);
        close(fd);
        return NULL;
    }
    while(readLength < (size_t)fileStatus.st_size)
    {
        ssize_t result = read(fd, text + readLength, (size_t)fileStatus.st_size - readLength);

        if(result < 0 && errno == EINTR)
        {
            continue;
        }
        if(result <= 0)
        {
            RLOG_ERROR("Cannot read %s", CONFIG_FILE_PATH);
            explicit_bzero(text, readLength);
            free(text);
            close(fd);
            return NULL;
        }
        readLength += (size_t)result;
    }
    close(fd);
    *length = readLength;
    return text;
}

static int ParseConfig(const cJSON *json, RConfig *staging)
{
    const cJSON *item;

    if(!cJSON_IsObject(json))
    {
        RLOG_ERROR("%s: top level must be a JSON object", CONFIG_FILE_PATH);
        return -1;
    }

    // 오타난 구역/키가 조용히 무시되지 않게 거부
    cJSON_ArrayForEach(item, json)
    {
        if(FindConfigSection(item->string) == NULL)
        {
            RLOG_ERROR("%s: unknown section \"%s\"", CONFIG_FILE_PATH, item->string != NULL ? item->string : "");
            return -1;
        }
    }
    for(size_t index = 0; index < CONFIG_SECTION_COUNT; ++index)
    {
        if(ParseConfigSection(json, &SECTIONS[index], (unsigned char *)staging) != 0)
        {
            return -1;
        }
    }
    return 0;
}

static int ParseConfigSection(const cJSON *json, const ConfigSection *section, unsigned char *base)
{
    const cJSON *object = cJSON_GetObjectItemCaseSensitive(json, section->name);
    const cJSON *item;

    if(!cJSON_IsObject(object))
    {
        RLOG_ERROR("%s: missing \"%s\" object", CONFIG_FILE_PATH, section->name);
        return -1;
    }
    cJSON_ArrayForEach(item, object)
    {
        if(FindConfigField(section, item->string) == NULL)
        {
            RLOG_ERROR("%s: unknown key \"%s.%s\"", CONFIG_FILE_PATH, section->name, item->string != NULL ? item->string : "");
            return -1;
        }
    }
    for(size_t index = 0; index < section->fieldCount; ++index)
    {
        if(ParseConfigField(object, section, &section->fields[index], base + section->offset) != 0)
        {
            return -1;
        }
    }
    return 0;
}

static int ParseConfigField(const cJSON *json, const ConfigSection *section, const ConfigField *field, unsigned char *base)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, field->key);

    if(item == NULL)
    {
        RLOG_ERROR("%s: missing \"%s.%s\"", CONFIG_FILE_PATH, section->name, field->key);
        return -1;
    }
    if(field->textSize > 0)
    {
        size_t length;

        if(!cJSON_IsString(item) || item->valuestring == NULL)
        {
            RLOG_ERROR("%s: \"%s.%s\" must be a string", CONFIG_FILE_PATH, section->name, field->key);
            return -1;
        }
        length = strlen(item->valuestring);
        if(length >= field->textSize || (length == 0 && !field->allowEmpty))
        {
            RLOG_ERROR("%s: \"%s.%s\" must be %s1..%zu bytes", CONFIG_FILE_PATH, section->name, field->key, field->allowEmpty ? "0 or " : "", field->textSize - 1);
            return -1;
        }
        memcpy(base + field->offset, item->valuestring, length + 1);
        return 0;
    }

    // 범위를 먼저 확인해야 int 변환이 안전함
    if(!cJSON_IsNumber(item) || item->valuedouble < field->minimum || item->valuedouble > field->maximum || item->valuedouble != (double)(int)item->valuedouble)
    {
        RLOG_ERROR("%s: \"%s.%s\" must be an integer %d..%d", CONFIG_FILE_PATH, section->name, field->key, field->minimum, field->maximum);
        return -1;
    }
    *(int *)(void *)(base + field->offset) = (int)item->valuedouble;
    return 0;
}

static const ConfigSection *FindConfigSection(const char *name)
{
    for(size_t index = 0; name != NULL && index < CONFIG_SECTION_COUNT; ++index)
    {
        if(strcmp(SECTIONS[index].name, name) == 0)
        {
            return &SECTIONS[index];
        }
    }
    return NULL;
}

static const ConfigField *FindConfigField(const ConfigSection *section, const char *key)
{
    for(size_t index = 0; key != NULL && index < section->fieldCount; ++index)
    {
        if(strcmp(section->fields[index].key, key) == 0)
        {
            return &section->fields[index];
        }
    }
    return NULL;
}

static cJSON *BuildConfigJson(void)
{
    cJSON *json = cJSON_CreateObject();

    for(size_t sectionIndex = 0; json != NULL && sectionIndex < CONFIG_SECTION_COUNT; ++sectionIndex)
    {
        const ConfigSection *section = &SECTIONS[sectionIndex];
        const unsigned char *base = (const unsigned char *)&config + section->offset;
        cJSON *object = cJSON_AddObjectToObject(json, section->name);

        for(size_t index = 0; object != NULL && index < section->fieldCount; ++index)
        {
            const ConfigField *field = &section->fields[index];

            if(field->textSize > 0)
            {
                object = cJSON_AddStringToObject(object, field->key, (const char *)(base + field->offset)) != NULL ? object : NULL;
            }
            else
            {
                object = cJSON_AddNumberToObject(object, field->key, *(const int *)(const void *)(base + field->offset)) != NULL ? object : NULL;
            }
        }
        if(object == NULL)
        {
            cJSON_Delete(json);
            json = NULL;
        }
    }
    return json;
}

// 임시 파일에 다 쓴 뒤 rename으로 교체해서, 쓰다가 실패해도 기존 파일이 깨지지 않게 함
static int WriteConfigFile(const char *text)
{
    const char *temporaryPath = CONFIG_FILE_PATH ".tmp";
    size_t length = strlen(text);
    size_t written = 0;
    int fd = open(temporaryPath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);

    if(fd < 0)
    {
        RLOG_ERROR("Cannot write %s: %s", temporaryPath, strerror(errno));
        return -1;
    }
    while(written < length)
    {
        ssize_t result = write(fd, text + written, length - written);

        if(result < 0 && errno == EINTR)
        {
            continue;
        }
        if(result <= 0)
        {
            RLOG_ERROR("Cannot write %s: %s", temporaryPath, strerror(errno));
            close(fd);
            unlink(temporaryPath);
            return -1;
        }
        written += (size_t)result;
    }
    written = write(fd, "\n", 1) == 1 && fsync(fd) == 0;
    if(close(fd) != 0 || !written || rename(temporaryPath, CONFIG_FILE_PATH) != 0)
    {
        RLOG_ERROR("Cannot replace %s: %s", CONFIG_FILE_PATH, strerror(errno));
        unlink(temporaryPath);
        return -1;
    }
    return 0;
}
