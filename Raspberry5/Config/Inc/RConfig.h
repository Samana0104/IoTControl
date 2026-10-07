#pragma once

#include <netinet/in.h>

// ServerConfig.json: DB 비밀번호가 있으므로 chmod 600 필수, git 제외

// 문자열 설정 값의 최대 크기 (NUL 포함)
#define CONFIG_TEXT_SIZE 256
#define CONFIG_PASSWORD_SIZE 1024
#define CONFIG_PATH_SIZE 4096

// "server" 구역
typedef struct _RServerConfig
{
    char ip[INET_ADDRSTRLEN]; // listen IP ("0.0.0.0": 모든 인터페이스)
    int port;                 // listen 포트 1..65535
    int workerCount;          // 네트워크 워커 스레드 수
} RServerConfig;

// "database" 구역
typedef struct _RDatabaseConfig
{
    char host[CONFIG_TEXT_SIZE];
    int port;
    char user[CONFIG_TEXT_SIZE];
    char password[CONFIG_PASSWORD_SIZE];
    char name[CONFIG_TEXT_SIZE];
    int timeoutSeconds; // 접속/읽기/쓰기 타임아웃
} RDatabaseConfig;

// "tls" 구역
typedef struct _RTlsConfig
{
    char certificateFile[CONFIG_PATH_SIZE];
    char privateKeyFile[CONFIG_PATH_SIZE];
} RTlsConfig;

typedef struct _RConfig
{
    RServerConfig server;
    RDatabaseConfig database;
    RTlsConfig tls;
} RConfig;

// 프로그램 전체에서 하나뿐인 설정. 로드 전에는 0으로 채워져 있음.
// 다른 스레드가 읽는 동안 다시 로드하지 않음 (서버가 연결을 받기 전에만 로드)
RConfig *RConfigGet(void);

// 실행 디렉터리 기준 ServerConfig.json을 읽음 ("server", "database", "tls" 구역).
// 형식이 틀리거나 값이 빠지면 기존 값을 모두 유지하고 -1 (로그 남김)
int RConfigLoad(void);
// 현재 값을 ServerConfig.json에 저장 (임시 파일에 쓴 뒤 교체, 권한 600). 0: 성공, -1: 실패
int RConfigSave(void);
