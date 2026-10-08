#pragma once

#include <stddef.h>
#include <stdint.h>

/* ============================================================================
   MySQL 8.4 쿼리 모음 (접속은 MySQL C API 호환 libmariadb 클라이언트 라이브러리 사용).
   모든 쿼리는 prepared statement용이며 값은 ?로 바인딩.
   테이블 구조 (실제 DB 기준, 바꾸면 여기와 Database/Sql도 같이 수정):

   member    (id VARCHAR(8) PK, pw_hash VARCHAR(255), type ENUM('STM32','ARDUINO','PC'))
   bluetooth (id VARCHAR(8) PK → member.id, mac_address VARCHAR(17))
   dht       (data_id BIGINT PK AUTO_INCREMENT, id VARCHAR(8) → member.id,
              temp FLOAT, humi FLOAT, created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP)
   fan       (singleton_id TINYINT PK = 1, speed INT)
   con       (singleton_id TINYINT PK = 1, temp INT)
   ============================================================================ */

/* ---- member ---- */
// ? = id
#define QUERY_SELECT_MEMBER_PASSWORD "SELECT pw_hash FROM member WHERE id = ? LIMIT 1"
// ? = id
#define QUERY_SELECT_MEMBER "SELECT id, type FROM member WHERE id = ? LIMIT 1"
// ? = id
#define QUERY_SELECT_MEMBER_TYPE "SELECT type FROM member WHERE id = ? LIMIT 1"
#define QUERY_SELECT_MEMBER_ALL "SELECT id, type FROM member ORDER BY id"
// ? = id, pw_hash, type
#define QUERY_INSERT_MEMBER "INSERT INTO member(id, pw_hash, type) VALUES(?, ?, ?)"
// ? = pw_hash, id
#define QUERY_UPDATE_MEMBER_PASSWORD "UPDATE member SET pw_hash = ? WHERE id = ?"
// ? = id (bluetooth, dht 행은 FK로 함께 삭제)
#define QUERY_DELETE_MEMBER "DELETE FROM member WHERE id = ?"

/* ---- bluetooth (회원당 HC-05 하나) ---- */
// ? = id
#define QUERY_SELECT_BLUETOOTH_MAC "SELECT mac_address FROM bluetooth WHERE id = ? LIMIT 1"
// ? = mac_address
#define QUERY_SELECT_BLUETOOTH_MEMBER "SELECT id FROM bluetooth WHERE mac_address = ? LIMIT 1"
#define QUERY_SELECT_BLUETOOTH_ALL "SELECT id, mac_address FROM bluetooth ORDER BY id"
// ? = id, mac_address
#define QUERY_INSERT_BLUETOOTH "INSERT INTO bluetooth(id, mac_address) VALUES(?, ?)"
// ? = mac_address, id
#define QUERY_UPDATE_BLUETOOTH_MAC "UPDATE bluetooth SET mac_address = ? WHERE id = ?"
// ? = id
#define QUERY_DELETE_BLUETOOTH "DELETE FROM bluetooth WHERE id = ?"

/* ---- dht (측정 기록: 수신할 때마다 한 행) ---- */
// ? = id, temp, humi
#define QUERY_INSERT_DHT "INSERT INTO dht(id, temp, humi) VALUES(?, ?, ?)"
// ? = temp, humi, id (회원의 모든 기록 행을 덮어씀)
#define QUERY_UPDATE_DHT "UPDATE dht SET temp = ?, humi = ? WHERE id = ?"
// ? = id
#define QUERY_SELECT_DHT_LATEST "SELECT temp, humi, created_at FROM dht WHERE id = ? ORDER BY data_id DESC LIMIT 1"
// ? = id, 개수
#define QUERY_SELECT_DHT_RECENT "SELECT temp, humi, created_at FROM dht WHERE id = ? ORDER BY data_id DESC LIMIT ?"
// ? = id, 시작 시각, 끝 시각
#define QUERY_SELECT_DHT_RANGE "SELECT temp, humi, created_at FROM dht WHERE id = ? AND created_at BETWEEN ? AND ? ORDER BY data_id"
// ? = 기준 시각 (이전 기록 삭제)
#define QUERY_DELETE_DHT_BEFORE "DELETE FROM dht WHERE created_at < ?"

/* ---- fan (단일 행) ---- */
#define QUERY_SELECT_FAN "SELECT speed FROM fan WHERE singleton_id = 1"
// ? = speed
#define QUERY_UPDATE_FAN "UPDATE fan SET speed = ? WHERE singleton_id = 1"

/* ---- con (단일 행) ---- */
#define QUERY_SELECT_CON "SELECT temp FROM con WHERE singleton_id = 1"
// ? = temp
#define QUERY_UPDATE_CON "UPDATE con SET temp = ? WHERE singleton_id = 1"

// 사용자 제공 최신값 테이블 dht(id PK, temp, humi, updated_at) 전체 조회.
#define QUERY_SELECT_DHT_ALL "SELECT d.id, d.temp, d.humi, d.created_at, m.type FROM dht AS d LEFT JOIN member AS m ON m.id = d.id ORDER BY d.id"


/* ============================================================================
   위 QUERY_* 실행 (prepared statement)
   ============================================================================ */

#define DATABASE_QUERY_MAX_PARAMS 8

// ? 자리에 순서대로 들어갈 값. text가 NULL이 아니면 문자열, NULL이면 정수(number)
typedef struct _DatabaseValue
{
    const char *text;
    long long number;
} DatabaseValue;

#define DATABASE_TEXT(value) ((DatabaseValue){.text = (value), .number = 0})
#define DATABASE_NUMBER(value) ((DatabaseValue){.text = NULL, .number = (value)})

// 결과 행 하나. 모든 값은 문자열이며 values[i] == NULL은 SQL NULL
typedef struct _DatabaseRow
{
    unsigned int columnCount;
    const char *const *columnNames;
    const char *const *values;
    const unsigned long *valueLengths;
} DatabaseRow;

// 결과를 한 행씩 받음. 데이터는 호출 동안만 유효. 0: 계속, 1: 그만 받기, -1: 처리 실패
typedef int (*DatabaseRowCallback)(const DatabaseRow *row, void *context);

// QUERY_* 하나를 params로 실행. InitializeDatabase()가 먼저 성공해야 함.
// 결과가 있는 쿼리는 행마다 callback 호출(NULL이면 버림), *rowCount = 받은 행 수
// 결과가 없는 쿼리는 *rowCount = 변경된 행 수. rowCount는 NULL 가능
// 0: 성공, -1: 실패 (errno: EINVAL 인자/? 개수 불일치, EIO DB 오류)
int ExecuteDatabaseQuery(const char *query, const DatabaseValue *params, unsigned int paramCount, DatabaseRowCallback callback, void *context, uint64_t *rowCount);
// 결과 첫 행의 첫 열을 value에 문자열로 복사 (예: ID로 MAC, 비밀번호 해시 조회)
// 1: 값 복사, 0: 행 없음 또는 SQL NULL, -1: 실패 (값이 valueSize보다 길어도 실패)
int QueryDatabaseValue(const char *query, const DatabaseValue *params, unsigned int paramCount, char *value, size_t valueSize);

