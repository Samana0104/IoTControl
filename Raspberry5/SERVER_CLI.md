# 서버 관리용 CLI

DB 설정은 `db_config.txt`, 서버 TLS 인증서·키 경로는 `tls_config.txt`에서 읽습니다.
두 파일 모두 현재 작업 디렉터리 기준이므로 실제 설정을 수정한 뒤 `Raspberry5` 폴더에서 실행합니다.
`Raspberry5` 폴더에서 빌드한 뒤 포트 인자 없이 실행합니다.

```sh
cmake -S . -B build
cmake --build build --target iot_server --parallel
./build/iot_server
```

CLI가 먼저 열립니다. `start <포트>`로 접속 수락을 시작합니다.
`start` 전에는 DB/TLS 초기화나 포트 바인딩을 하지 않습니다.

```text
iot-server> status
Server: not started
iot-server> start 5000
IoT server started on port 5000
iot-server> clients
```

터미널에서는 `iot-server>` 프롬프트와 도움말이 표시됩니다.
클라이언트의 명령 입력 화면과는 별개로, 서버 실행 터미널에서 입력하는 관리 명령입니다.

- `start <포트>`: 지정한 포트(1..65535)로 서버 시작
- `help`: 명령 목록 조회
- `status`: 서버 포트, TCP 세션 수, BT 소켓 수와 동작 중인 수신 스레드 수 조회
- `clients`: TCP 클라이언트의 슬롯, FD, IP, 회원 ID, 인증 상태 조회
- `bluetooth`: 현재 서버가 보유한 BT 소켓의 회원 ID, MAC, FD, 수신 상태 조회
- `bt-connect <회원ID>`: DB에 등록된 해당 회원의 Bluetooth MAC으로 연결 또는 재연결
- `db <SQL>`: DB의 INSERT / UPDATE / SELECT 실행. UPDATE는 WHERE 필수
- `clear`: 터미널 화면 지우기
- `quit` 또는 `exit`: 새 접속 수락을 중단하고 TCP/BT 작업을 정리한 뒤 서버 종료

`bluetooth`는 DB 등록 목록이 아니라 현재 BT 세션의 소켓 상태를 표시합니다.
BT 세션은 TCP 세션과 별개로 관리하므로 요청한 PC 클라이언트가 종료돼도 유지됩니다.
BT 수신 스레드가 종료되면 다음 연결 요청, 슬롯 재사용 또는 서버 종료 때 소켓을 정리하므로, `RX=stopped`를 함께 확인합니다.
조회 명령은 DB 내용을 변경하거나 클라이언트에 패킷을 보내지 않습니다.
잘못된 포트, DB 연결 실패나 포트 사용 중 오류가 발생하면 CLI에서 다시 `start <포트>`를 입력할 수 있습니다.
DB 초기화가 실패했다면 파일을 수정한 뒤 `start <포트>`로 재시도할 수 있습니다. 성공적으로 읽어 둔 DB 설정은 자동 갱신하지 않으므로, 실행 중 설정을 바꾸려면 프로그램을 재시작합니다.
TLS 설정 파일 읽기 또는 인증서 초기화가 실패했다면 파일을 수정한 뒤 `start <포트>`로 재시도할 수 있습니다.
TLS 컨텍스트가 성공적으로 생성된 이후에는 자동으로 다시 읽지 않으므로, 인증서/키 경로나 파일 내용을 바꾸면 서버를 재시작합니다.
서버가 이미 실행 중이면 추가 `start` 요청은 거부하며 기존 연결을 유지합니다. 실행 중 포트를 바꾸려면 서버를 종료한 뒤 다시 실행합니다.

기존 포트 인자 방식도 유지합니다. 이 방식은 즉시 서버를 시작하며, 비터미널에서는 CLI가 기본적으로 비활성화됩니다.

```sh
./build/iot_server 5000
./build/iot_server 5000 --cli
./build/iot_server 5000 --no-cli
```

`./build/iot_server --cli`도 포트 없이 CLI를 먼저 여는 방식입니다. `--no-cli`는 포트 인자가 필요합니다.
포트 없이 실행하면 비터미널 입력에서도 CLI 명령을 받습니다.
시작 전 입력이 닫히면 프로세스가 종료되고, 시작 후에는 CLI만 비활성화되어 서버는 계속 동작합니다.
`Ctrl+C` 또는 `SIGTERM`으로도 TCP/BT 정리 후 종료합니다.
DB 조회나 BT 연결·페어링이 진행 중이라면 해당 작업이 끝나거나 제한 시간이 만료될 때까지 종료를 기다릴 수 있습니다.
수신 로그도 같은 터미널에 출력되므로 입력 프롬프트와 로그가 섞여 보일 수 있습니다.

## DB 관리 명령과 재사용 함수

`start <포트>`로 DB를 초기화한 뒤 서버 터미널에서 입력합니다. `db` 다음에 SQL을 그대로 작성하며,
SQL은 대소문자를 구분하지 않습니다. 다음 INSERT는 `fan`에 해당 키가 없을 때 사용하는 예제입니다.

```text
iot-server> db select id, type FROM member;
iot-server> db insert INTO fan (singleton_id, speed) VALUES (1, 900);
Affected: 1 row(s).
iot-server> db update fan SET speed=800 WHERE singleton_id=1;
iot-server> db select id, temp, humi, updated_at FROM dht LIMIT 20;
```

SELECT는 열 이름과 행을 탭으로 구분해 출력하고 마지막에 조회 행 수를 표시합니다.
SQL NULL은 `NULL`, 출력 제어 문자와 역슬래시는 `\xHH`로 표시합니다.
INSERT/UPDATE는 변경 행 수를 표시합니다. 값이 이미 같으면 UPDATE의 변경 행 수가 0일 수 있습니다.
DB 제약조건과 `db_config.txt`의 DB 계정 권한을 그대로 적용합니다.
SQL 문법·권한·제약조건 오류가 나도 서버는 종료하지 않습니다.

- 한 줄에 SQL 한 문장만 실행합니다. 마지막 세미콜론은 선택 사항이며 SQL 길이는 최대 4096바이트입니다.
- INSERT / UPDATE / SELECT만 지원하며 DELETE / DROP / ALTER 등은 거부합니다.
- 실수 방지를 위해 UPDATE에는 최상위 WHERE가 필요합니다. `WHERE 1=1`처럼 모든 행을 선택하는 조건까지 방지하는 것은 아니므로 조건을 먼저 SELECT로 확인합니다.
- 주석과 역슬래시는 지원하지 않습니다. SQL 문자열의 작은따옴표는 `''`로 표현합니다.
- SELECT의 INTO, 잠금 절과 LOAD_FILE, 쓰기 명령의 RETURNING은 지원하지 않습니다.
- 서버 실행 터미널의 신뢰된 관리자용 기능입니다. SQL 검사기는 완전한 보안 샌드박스가 아니며 외부 클라이언트 패킷에는 이 기능을 추가하지 않았습니다.
- SQL과 DB 오류 원문은 별도로 기록하지 않지만 SELECT로 조회한 값은 터미널에 출력됩니다. 비밀번호·해시 등 민감한 열은 필요할 때만 조회합니다.

DB 명령 실행부는 `Core/Inc/IotDatabaseCommand.h`와 `Core/Src/IotDatabaseCommand.c`로 분리했습니다.
CLI 없이 다른 서버 코드에서도 다음 함수를 호출할 수 있습니다. 먼저 `InitializeDatabase()`가 성공해야 합니다.

```c
#include "IotDatabaseCommand.h"

uint64_t affectedRows = 0;
int result = UpdateDatabaseData("UPDATE fan SET speed=800 WHERE singleton_id=1", &affectedRows);
```

- `InsertDatabaseData(sql, &affectedRows)` / `UpdateDatabaseData(sql, &affectedRows)`: 성공 0, 오류 -1. 변경 행 수는 선택적 출력 인자입니다.
- `SelectDatabaseData(sql, callback, context, &rowCount)`: 결과를 한 행씩 콜백에 전달합니다. 성공 0, 오류 -1. 콜백과 행 수 인자는 NULL로 생략할 수 있습니다.
- `ExecuteDatabaseCliCommand(sql, output)`: `db` 접두어 없는 SQL을 실행하고 `FILE *`에 출력하는 CLI용 함수입니다.

SELECT 콜백은 먼저 `values == NULL`인 열 정보 이벤트를 한 번 받습니다. 이후 `DatabaseRow`의
`columnNames`, `values`, `valueLengths`로 실제 행을 읽습니다. 행의 `values[i] == NULL`은 SQL NULL입니다.
콜백 반환값은 0 계속, 1 조기 종료, -1 처리 실패입니다. `rowCount`는 전달한 행 수이며 처리 실패 시 부분 결과일 수 있습니다.
문자열은 길이를 기준으로 읽고, 콜백 밖에서 보관하려면 복사해야 합니다. 콜백 내부에서 다른 DB 함수를 중첩 호출하지 않습니다.
각 호출은 기존 DB 설정으로 별도 연결을 열고 닫으며, SELECT 결과는 메모리에 모두 쌓지 않고 순차 처리합니다.
SQL은 호출자가 제공하는 신뢰된 관리용 문자열입니다. 외부 입력을 문자열로 붙이지 말고, 회원 인증·등록처럼 외부 데이터를 다루는 경로는 기존 prepared statement 방식을 사용합니다.
작업은 동기 실행이므로 오래 걸리는 쿼리 동안 CLI 입력과 새 TCP 접속 수락이 잠시 기다릴 수 있습니다.
기존 TCP/BT 수신 스레드는 유지됩니다.

CMake의 `iot_server`에 새 소스를 등록했습니다. 직접 gcc로 빌드한다면 기존 서버 소스 목록에
`Core/Src/IotDatabaseCommand.c`도 포함해야 합니다.

## 등록된 Bluetooth 장치 연결 요청

서버를 시작한 뒤 서버 CLI에서 회원 ID를 지정합니다. MAC 자체가 아니라 `bluetooth.id`를 사용합니다.
TCP 클라이언트가 하나도 없어도 서버가 Pi 내장 Bluetooth를 이용해 등록된 장치로 RFCOMM 연결을 시도합니다.

```text
iot-server> start 5000
iot-server> bt-connect test
iot-server> bluetooth
```

외부 요청은 **이미 열려 있는 TCP/TLS 연결**로 BT 연결 명령과 ID·PW·MAC을 보냅니다.
별도 요청 프로그램을 실행하거나 TCP에 새로 접속하지 않습니다. 같은 연결로 결과를 받고 기존 통신을 계속합니다.
예제 `iot_client`도 처음 서버에 접속한 후, 기존 명령 입력 화면에서 요청합니다.

```text
bt-connect test 98:DA:60:09:9B:C8
Password: (test 회원의 비밀번호를 숨김 입력)
BT result: 1 (connected)
```

명령 형식은 `bt-connect <BT 회원ID> <MAC>`입니다. 외부 PC의 로그인 ID와 BT 회원 ID가 달라도 요청에 담은 BT 회원의 비밀번호로 검증합니다.
`bt-connect <MAC>` 단축형은 현재 로그인 ID를 사용합니다. 비밀번호는 숨김 입력으로 받으므로 명령 인자·셸 히스토리에 넣지 않습니다.
`BT result: 1 (connected)` 또는 `BT result: 0 (failed)`를 출력한 뒤 클라이언트를 종료하지 않습니다.
성공/실패 결과 뒤에도 채팅이나 다음 BT 명령을 같은 연결로 전송할 수 있습니다.
원할 때 `quit`로 외부 클라이언트를 종료해도 서버의 BT 연결은 유지됩니다.
이전의 실행 인자 `iot_client ... bt-connect ...` 일회성 모드는 제거했습니다.

클라이언트의 `RequestBluetoothConnection(client, id, pw, mac)`이 `CMD_BLUETOOTH_CONNECT`(값 6) 요청을 보냅니다.
이미 접속된 `client`를 그대로 전달하는 함수이며 TCP 접속/해제를 수행하지 않습니다.
반환값은 연결 성공 1, 서버의 인증/등록/연결 실패 0, 로컬 인자/통신/응답 오류 -1입니다.
패킷은 아래 순서입니다. 요청/결과 구조체는 `common/IoTPacket.h`에 정의했습니다.

```text
외부 → 서버: ok | cmd=6 | dataLen=sizeof(BluetoothConnectData) (=89)
외부 ← 서버: RQ | cmd=6 | flag=0 (5초 대기 시 flag=1 재요청)
외부 → 서버: id[8] + pw[64] + mac[17]
서버:       비밀번호 검증 → DB의 ID/MAC 일치 확인 → 서버 소유 BT 클라이언트 생성/연결
외부 ← 서버: RS | cmd=6 | dataLen=sizeof(BluetoothConnectResult) (=1) + connected[1]
외부:       같은 기존 연결로 계속 통신 가능; 나중에 외부가 종료돼도 서버 ↔ BT 유지
```

서버는 회원 비밀번호 해시를 검증하고, 전달된 MAC이 해당 회원의 DB 등록 MAC과 일치할 때만 연결합니다.
틀린 비밀번호, 미등록 MAC, ID/MAC 불일치, DB 오류, 연결 실패에는 모두 결과 0을 보냅니다.
이 요청은 일반 TCP 로그인 상태를 만들지 않으며, 센서·채팅·등록 명령은 여전히 TCP 로그인이 필요합니다.
BT 링크로 들어오는 연결 관리 명령은 거부합니다. 기존 센서·채팅의 TCP/BT 공용 패킷 처리는 유지합니다.

서버의 `RequestRegisteredBluetoothConnection()`을 외부 요청과 CLI 연결 경로가 공용으로 사용합니다.
CLI는 `RequestMemberBluetoothConnection(id)`를 통해 DB MAC을 조회하고, 외부 요청은 검증된 ID와 요청 MAC을 전달합니다.
BT 클라이언트는 서버의 별도 `bluetoothClients` 배열에 보관하고 자체 수신 스레드로 처리합니다.
일반 TCP 로그인만 했을 때는 BT에 자동 연결하지 않습니다. 신규 BT 등록 후 연결은 유지합니다.
이미 같은 회원/MAC의 수신 스레드가 동작 중이면 소켓을 추가로 만들지 않습니다.
끊어진 연결은 기존 수신 스레드와 FD를 정리한 뒤 다시 연결합니다. 다른 회원이 같은 MAC으로 연결 중이면 거부합니다.
연결 후 BT 데이터는 기존 공용 패킷 수신기로 처리하며, 서버 종료 시 BT 세션도 정리합니다.
등록되지 않았거나 장치가 꺼져 있는 등 연결 실패가 발생해도 외부 요청자의 TCP 연결/로그인 상태를 유지하므로 재시도할 수 있습니다.

`RQ`는 데이터 송신 요청이고, `RS`의 결과 바이트가 실제 BT 연결 시도 결과입니다.
결과 송신에 실패하거나 요청자가 먼저 끊겨도 이미 연결된 서버 소유 BT 세션을 정리하지 않습니다.
외부 결과 응답은 추가 RQ 교환 없이 `RS` 헤더와 1바이트 결과를 연속해서 보냅니다.
클라이언트는 결과 대기 시 소켓 수신 제한 시간을 일시적으로 90초로 늘리고 요청 종료 후 원래 값으로 복구합니다.
연결 작업은 직렬화되어 처리되며, CLI에서는 DB 조회·SDP 서비스 검색·RFCOMM 연결 시도 중 입력 처리가 잠시 기다릴 수 있습니다.

재연결 명령에는 PIN을 전달하거나 저장하지 않습니다. 처음 페어링은 기존 `bt-register <MAC>` 명령으로 PIN을 한 번 입력합니다.
DB에 MAC만 수동 등록한 장치가 아직 Pi와 페어링되지 않았다면 먼저 별도 최초 페어링이 필요할 수 있습니다.

## DB 설정 파일

`db_config.txt`를 생성해 두었으며, 예제 템플릿은 `db_config.example.txt`입니다. 실제 DB 접속 정보에 맞게 값을 바꿉니다.

```text
IOT_DB_HOST=127.0.0.1
IOT_DB_PORT=3306
IOT_DB_USER=iot_server
IOT_DB_PASSWORD=실제_DB_계정_비밀번호
IOT_DB_NAME=iot_control
```

이 이름들은 환경 변수가 아니라 파일 안의 키입니다. DB 연결에는 기존 `IOT_DB_*` 환경 변수를 사용하지 않습니다.
USER·PASSWORD·NAME 키는 필수이고, HOST·PORT를 생략하면 `127.0.0.1:3306`을 사용합니다.
`IOT_DB_PASSWORD`는 MariaDB 접속 계정의 비밀번호이며 회원 테이블의 `pw_hash`와는 별개입니다.

```sh
chmod 600 db_config.txt
```

비밀번호가 평문이므로 다른 사용자 권한이 설정된 파일은 거부합니다. 실제 설정 파일은 `.gitignore`에 추가했고, 빌드 폴더로 복사하지 않습니다.
일반 파일만 지원하며 심볼릭 링크는 허용하지 않습니다.

형식은 한 줄에 `KEY=값` 하나입니다. 빈 줄과 `#`로 시작하는 주석은 허용합니다.
값 앞뒤 공백은 제거하며, 공백 자체가 비밀번호에 포함되면 `IOT_DB_PASSWORD="  비밀번호  "`처럼 따옴표로 감쌉니다.
값 안의 `#`, `=`, `$`, 역슬래시는 그대로 사용합니다. 셸 치환·이스케이프 변환·인라인 주석은 처리하지 않습니다.
HOST·USER·NAME은 각각 최대 255바이트, PASSWORD는 최대 1023바이트이며, 중복 키·알 수 없는 키·누락된 필수 키·잘못된 포트는 거부합니다.

## 테스트

테스트 소스와 스크립트는 `tests/`, 실행 파일과 생성 결과는 `tests/artifacts/`에 모아 둡니다.
기존 `/tmp`의 테스트·임시 빌드 결과는 `tests/artifacts/archived/`에 보관했습니다.
폴더 구성과 실행 방법은 `tests/README.md`를 참고합니다.

```sh
sh tests/run_server_cli_test.sh
sh tests/run_bluetooth_receive_test.sh
sh tests/run_database_config_test.sh
sh tests/run_database_command_test.sh
sh tests/run_tls_config_test.sh
sh tests/run_client_tls_config_test.sh
```

CLI 입력 처리, 상태 스냅샷, BT 수신 상태, TCP/BT 종료 및 실제 로컬 TLS 접속을 테스트합니다.
BT 수신 테스트에는 CLI/외부 공용 연결 요청, 재연결·중복 MAC 거부·TCP와 독립적인 연결 수명 및 클라이언트 요청 헤더 검증도 포함합니다.
TLS 통합 테스트는 기존 `iot_client` 연결 하나에서 BT 요청의 결과 1/0을 받고 계속 채팅한 뒤 종료해도 모의 BT 장치의 DHT 데이터를 서버가 수신하는 것을 확인합니다.
외부 TCP 클라이언트 없이 서버 CLI만으로 BT를 연결하는 경로도 확인합니다.
DB 설정 테스트는 파일 파싱·권한·오류 처리·비밀번호 로그 노출 방지와 DB 연결에 전달하는 설정 값을 검사합니다.
DB 명령 테스트는 실제 DB 대신 모의 MariaDB API로 단일 문장 검사, WHERE 누락 거부, 변경 행 수, SELECT 콜백·NULL·바이너리 출력과 오류 시 자원 정리를 확인합니다.
TLS 설정 테스트는 경로 파싱·권한·누락/중복 키·크기 제한을 검사하며, TLS 통합 테스트에서 파일 기반 인증서 로딩과 실패 후 재시도를 확인합니다.
클라이언트 TLS 설정 테스트도 파일 파싱을 확인하고, 통합 테스트에서 환경 변수 무시·잘못된 CA 거부·서버 IP 검증 유지를 확인합니다.
실제 DB나 Bluetooth 장치에 접속하지 않으며, TLS 테스트 인증서는 `/tmp`에 별도로 생성합니다.

## 서버 TLS 설정 파일

`tls_config.txt`를 생성해 두었습니다. 예제 템플릿은 `tls_config.example.txt`입니다.
기존 인증서와 개인 키 파일은 그대로 사용하며, 설정 파일에는 그 **경로만** 저장합니다.

```text
IOT_TLS_CERT_FILE=./cert/server.crt
IOT_TLS_KEY_FILE=./cert/server.key
```

두 키는 필수이며 환경 변수가 아니라 파일 안의 설정 이름입니다.
서버는 기존 `IOT_TLS_CERT_FILE`, `IOT_TLS_KEY_FILE` 환경 변수를 읽거나 대체 값으로 사용하지 않습니다.
TLS 설정은 CLI의 `start <포트>` 또는 포트 인자로 서버를 시작할 때 읽습니다.
설정/인증서 읽기에 실패하면 수신 포트를 열지 않고, CLI에서 설정을 고친 뒤 시작을 재시도할 수 있습니다.

상대 경로는 실행 시 작업 디렉터리 기준입니다. 위 설정이면 `Raspberry5` 폴더에서 `./build/iot_server`를 실행합니다.
다른 경로에서 실행할 때는 그 작업 디렉터리에 설정 파일을 놓고 인증서/키 경로를 맞추거나 절대 경로를 입력합니다.
`$PWD`, `~`나 환경 변수는 확장하지 않습니다. 경로에 공백이 있으면 따옴표로 감쌀 수 있습니다.
한 줄에 `KEY=경로` 하나를 쓰고 빈 줄과 `#`로 시작하는 주석을 사용할 수 있습니다. 인라인 주석·이스케이프 변환은 지원하지 않습니다.
각 경로는 최대 4095바이트이며, 빈 값·알 수 없는 키·중복 키·NUL 문자·과도하게 긴 줄은 거부합니다.
기존과 동일하게 TLS 1.2 이상을 사용하고 인증서/개인 키의 일치 여부를 검사합니다.

```sh
chmod 600 tls_config.txt
chmod 600 cert/server.key
```

설정 파일은 일반 파일만 허용하고 심볼릭 링크·그룹/다른 사용자 권한을 거부합니다.
실제 `tls_config.txt`는 Git에서 제외했으며 빌드 폴더로 자동 복사하지 않습니다.
실행 중 경로나 인증서/키 내용을 바꿨으면 서버를 재시작해야 합니다.
Bluetooth 에이전트 설정은 기존 환경 변수 방식을 유지합니다.

## 클라이언트 TLS CA 설정 파일

`iot_client`도 환경 변수 대신 현재 작업 디렉터리의 `tls_client_config.txt`를 읽습니다.
서버용 `tls_config.txt`와 별개이며, 서버의 개인 키는 클라이언트에 전달하지 않습니다.

```text
IOT_TLS_CA_FILE=./cert/server.crt
```

이 경로는 서버 인증서를 검증할 신뢰된 CA 인증서 파일입니다. 현재 자체 서명 방식에서는 신뢰할 서버의
`server.crt`를 사용합니다. CA가 서명한 서버 인증서로 전환하면 해당 CA 인증서 경로로 바꿉니다.
상대 경로는 실행 작업 디렉터리 기준이며, `$PWD`·`~`·환경 변수는 확장하지 않습니다.
공백이 있는 경로는 따옴표로 감쌀 수 있고, 빈 줄과 `#`로 시작하는 주석을 지원합니다.
키는 `IOT_TLS_CA_FILE` 하나만 허용하며 필수입니다. 빈 값·중복 키·알 수 없는 키·NUL 문자·긴 줄은 거부합니다.
경로는 최대 4095바이트이며 인라인 주석이나 이스케이프 변환은 지원하지 않습니다.

```sh
chmod 600 tls_client_config.txt
cmake --build build --target iot_client --parallel
./build/iot_client <서버_IP> 5000
```

CA 경로는 신뢰 대상을 결정하므로 설정 파일은 일반 파일만 지원하며 심볼릭 링크와 그룹/다른 사용자 권한을 거부합니다.
실제 설정 파일은 Git에서 제외했고 빌드 폴더로 자동 복사하지 않습니다.
기존 `IOT_TLS_CA_FILE` 환경 변수는 대체 값으로도 사용하지 않습니다. 설정 파일이 없거나 잘못되면 접속하지 않습니다.
`ConnectClient()`를 호출할 때마다 다시 읽으며 TLS 1.2 이상, 인증서 체인 검증, 서버 IP 검증은 그대로 유지합니다.
인증서의 IP 항목과 접속한 서버 IP가 일치해야 하므로, 예를 들어 실제 Pi IP만 포함한 인증서는 `127.0.0.1`로 접속하면 실패합니다.
