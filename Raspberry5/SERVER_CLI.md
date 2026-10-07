# 서버 관리용 CLI

설정은 `ServerConfig.json` 하나에서 읽습니다(`server` / `database` 구역).
두 파일 모두 현재 작업 디렉터리 기준이므로 실제 설정을 수정한 뒤 `Raspberry5` 폴더에서 실행합니다.
`Raspberry5` 폴더에서 빌드한 뒤 포트 인자 없이 실행합니다.

```sh
cmake -S . -B build
cmake --build build --target iot_server --parallel
./build/iot_server
```

CLI가 먼저 열립니다. `server start <포트>`로 접속 수락을 시작합니다.
`server start` 전에는 DB 초기화나 포트 바인딩을 하지 않습니다.

```text
iot-server> server status
Server: not started
iot-server> server start 5000
IoT server started on port 5000
iot-server> server sessions
```

터미널에서는 `iot-server>` 프롬프트와 도움말이 표시됩니다.
클라이언트의 명령 입력 화면과는 별개로, 서버 실행 터미널에서 입력하는 관리 명령입니다.

명령은 STM32/Arduino CLI처럼 그룹 단위로 나뉩니다. 그룹 이름만 입력하면 해당 그룹의 명령 목록을 표시하며, 명령 이름은 대소문자를 구분하지 않습니다.

- `server start <포트>`: 지정한 포트(1..65535)로 서버 시작
- `server status`: 서버 포트와 세션 수(전체/TCP/BT) 조회
- `server sessions`: TCP 클라이언트와 BT 장치 세션을 함께 조회 (FD, 종류, IP/MAC, 회원 ID, 인증 상태). FD는 `fan set`에 사용
- `bt list`: 현재 연결된 BT 세션의 FD, 회원 ID, MAC 조회
- `bt connect <회원ID>`: DB에 등록된 해당 회원의 Bluetooth MAC으로 연결 또는 재연결, 성공하면 세션 FD 표시
- `fan set <FD> <0..100>`: 해당 FD 세션의 장치에 팬 속도(%) 요청 (`REQ_FAN`). 장치의 `ACK_FAN` 결과는 서버 로그에 표시
- `member add <ID> <비밀번호> <stm32|arduino|pc>`: 회원가입. 서버가 비밀번호를 argon2(libsodium)로 해시해 `member` 테이블에 저장합니다. ID는 1..8바이트, 비밀번호는 공백 없이 입력합니다. 이미 있는 ID는 거부합니다.
- `member list`: 등록된 회원 ID와 타입 조회
- `help`: 명령 목록 조회
- `clear`: 터미널 화면 지우기
- `quit` 또는 `exit`: 새 접속 수락을 중단하고 TCP/BT 작업을 정리한 뒤 서버 종료

TCP 클라이언트와 BT 장치는 같은 세션 테이블에서 fd로 관리합니다. 연결이 끊기면 그 세션은 바로 정리되어 목록에서 사라집니다.
`bt list`는 DB 등록 목록이 아니라 현재 연결된 BT 세션을 표시합니다.
BT 세션은 요청한 PC 클라이언트 세션과 별개이므로 그 클라이언트가 종료돼도 유지됩니다.
fd 번호는 연결이 끊긴 뒤 다른 세션에 재사용될 수 있으므로, `fan set` 전에 `server sessions`로 확인합니다.
조회 명령은 DB 내용을 변경하거나 클라이언트에 패킷을 보내지 않습니다.
잘못된 포트, DB 연결 실패나 포트 사용 중 오류가 발생하면 CLI에서 다시 `server start <포트>`를 입력할 수 있습니다.
DB 초기화가 실패했다면 파일을 수정한 뒤 `server start <포트>`로 재시도할 수 있습니다. 성공적으로 읽어 둔 DB 설정은 자동 갱신하지 않으므로, 실행 중 설정을 바꾸려면 프로그램을 재시작합니다.
서버가 이미 실행 중이면 추가 `server start` 요청은 거부하며 기존 연결을 유지합니다. 실행 중 포트를 바꾸려면 서버를 종료한 뒤 다시 실행합니다.

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

## DB 조회와 수정

서버 콘솔에는 DB 명령이 없습니다. 데이터 조회·수정은 MySQL Workbench 등 DB 도구를 사용합니다.
서버 코드가 쓰는 쿼리는 `Database/Inc/RDatabaseQuery.h`에 모여 있습니다.

## 센서 수신 시 DB UPDATE

`RPacketDhtReceive` / `RPacketFanReceive` / `RPacketConReceive`는 정상 패킷을 받은 뒤 DB UPDATE를 실행합니다.
TCP/WiFi와 Bluetooth 모두 기존 공용 수신 경로를 사용하며, 패킷 형식은 바꾸지 않았습니다.
각 함수가 `Database/Inc/RDatabaseQuery.h`의 `QUERY_UPDATE_DHT` / `QUERY_UPDATE_FAN` / `QUERY_UPDATE_CON`을 `ExecuteDatabaseQuery()`로 바로 실행합니다. 실패하면 MySQL 오류 메시지와 쿼리가 로그에 남습니다.

```sql
UPDATE dht SET temp = ?, humi = ? WHERE id = ?;
UPDATE fan SET speed = ? WHERE singleton_id = 1;
UPDATE con SET temp = ? WHERE singleton_id = 1;
```

DHT의 ID는 패킷에 추가하지 않고 TCP 세션의 인증된 회원 ID 또는 BT 수신 컨텍스트의 회원 ID를 사용합니다.
숫자와 ID는 prepared statement로 바인딩하며, 수신한 uint16_t 값을 그대로 저장합니다. 별도 소수점 배율 변환은 하지 않습니다.
FAN/CON은 공용 단일 행이므로 여러 장치가 보내면 마지막으로 성공한 UPDATE 값이 남습니다.

INSERT 또는 upsert는 하지 않습니다. `dht`의 해당 ID 행과 `fan` / `con`의 `singleton_id=1` 행을 미리 준비해야 합니다.
변경 행 수는 `DB UPDATE: ... affected=N` 로그로 확인합니다. `affected=0`은 값이 같거나 대상 행이 없는 경우입니다.
각 호출은 별도 DB 연결을 사용하며 autocommit으로 반영합니다. DB 계정에는 각 테이블에 대한 UPDATE 권한이 필요합니다.
DB 오류는 로그로 남기고 TCP/BT 연결을 유지합니다. 오류 데이터에 대한 자동 재시도·저장 큐나 클라이언트 저장 완료 응답은 추가하지 않았습니다.
`dht.created_at`은 행을 추가할 때의 시각(`DEFAULT CURRENT_TIMESTAMP`)이며, UPDATE로 값을 바꿔도 갱신되지 않습니다.

## 클라이언트 DHT 송신

TCP 클라이언트는 로그인(`REQ_LOGIN` → `ACK_LOGIN` 성공) 후 `NFY_DHT`를 보냅니다.
프레임은 `common/IoTPacketCodec.h`의 `MakeDhtPacket()`으로 만듭니다.

패킷은 8바이트 헤더 + 페이로드이며 한 번에 보냅니다. 정의는 `common/IoTProtocol.h`에 있습니다(모든 값 little-endian).

```text
offset 0  uint16 cmd       bit15 ACK | bit14 NFY | bit13..0 ID (0x0001~0x3FFF)
offset 2  uint16 length    페이로드 길이 (최대 MAX_PAYLOAD_SIZE)
offset 4  uint16 reserved  0 (받는 쪽은 무시)
offset 6  uint16 crc16     CRC-16/CCITT-FALSE, offset 0~5 + 페이로드
offset 8  payload          형식은 cmd가 정함 (common/IoTPacket.h)

REQ = ID (ACK 필요), ACK = ID | 0x8000 (페이로드 첫 바이트 = 결과), NFY = ID | 0x4000 (응답 없음)
한 연결에서 요청은 한 번에 하나만 보내고, ACK는 cmd로 짝을 맞춥니다.
```

| ID | 메시지 (cmd) | 응답 |
|---|---|---|
| 0x0001 | `REQ_LOGIN` (0x0001): id, pw | `ACK_LOGIN` (0x8001), 실패 시 ACK 후 연결 종료 |
| 0x0002 | `NFY_CHAT` (0x4002): 텍스트 0~255바이트 | 없음 |
| 0x0003 | `NFY_DHT` (0x4003) | 없음 |
| 0x0004 | `NFY_FAN` (0x4004) | 없음 |
| 0x0005 | `NFY_CON` (0x4005) | 없음 |
| 0x0006 | `REQ_BT_REGISTER` (0x0006): mac, pin | `ACK_BT_REGISTER` (0x8006), 실패 시 ACK 후 연결 종료 |
| 0x0007 | `REQ_BT_CONNECT` (0x0007): id, pw, mac | `ACK_BT_CONNECT` (0x8007) |

헤더를 받은 뒤 5초 안에 페이로드가 오지 않거나, cmd·길이·권한·CRC가 맞지 않으면 서버는 연결을 끊습니다.

`SendDhtData(client, &dhtData)`는 `MakeDhtPacket()`으로 만든 프레임을 `SendPacket`으로 보냅니다.
온도·습도는 `DhtData` 구조체에 담고, 와이어 형식은 `common/IoTPacketCodec.c`가 정합니다(16비트 값은 little-endian). 음수·소수·65535 초과·누락/추가 인자는 거부하고 연결을 유지합니다.

```text
클라이언트 → 서버: cmd=NFY_DHT (0x4003) | length=4 | reserved=0 | crc16 | temp=10 (u16) + humi=10 (u16)
서버:              CRC 확인 → 인증된 회원 ID의 dht 행 UPDATE (응답 없음)
```

`NFY_DHT`에는 응답이 없습니다. 서버의 `DHT DB UPDATE: id=test, affected=N` 로그나 MySQL Workbench에서 실제 값을 확인합니다.
해당 ID의 `dht` 행은 미리 존재해야 합니다.

## 등록된 Bluetooth 장치 연결 요청

서버를 시작한 뒤 서버 CLI에서 회원 ID를 지정합니다. MAC 자체가 아니라 `bluetooth.id`를 사용합니다.
TCP 클라이언트가 하나도 없어도 서버가 Pi 내장 Bluetooth를 이용해 등록된 장치로 RFCOMM 연결을 시도합니다.

```text
iot-server> server start 5000
iot-server> bt connect test
iot-server> bt list
```

외부 요청은 **이미 열려 있는 TCP 연결**로 BT 연결 명령과 ID·PW·MAC을 보냅니다.
별도 요청 프로그램을 실행하거나 TCP에 새로 접속하지 않습니다. 같은 연결로 결과를 받고 기존 통신을 계속합니다.

요청에 담은 BT 회원의 ID·비밀번호로 검증하므로, 클라이언트의 로그인 ID와 BT 회원 ID가 달라도 됩니다.
요청은 `REQ_BT_CONNECT`(0x07, `MakeBluetoothConnectPacket()`), 결과는 `ACK_BT_CONNECT`의 RESULT 1바이트입니다.
결과를 받은 뒤에도 같은 연결로 계속 통신할 수 있고, 클라이언트가 끊겨도 서버의 BT 연결은 유지됩니다.
패킷은 아래 순서입니다. 요청/결과 구조체는 `common/IoTPacket.h`에 정의했습니다.

```text
외부 → 서버: cmd=REQ_BT_CONNECT (0x0007) | length=89 | reserved | crc16 | id[8] + pw[64] + mac[17]
서버:       비밀번호 검증 → DB의 ID/MAC 일치 확인 → 서버 소유 BT 클라이언트 생성/연결
외부 ← 서버: cmd=ACK_BT_CONNECT (0x8007) | length=1 | reserved | crc16 | result (RESULT_SUCCESS=0 / RESULT_FAIL=1)
외부:       같은 기존 연결로 계속 통신 가능; 나중에 외부가 종료돼도 서버 ↔ BT 유지
```

서버는 회원 비밀번호 해시를 검증하고, 전달된 MAC이 해당 회원의 DB 등록 MAC과 일치할 때만 연결합니다.
틀린 비밀번호, 미등록 MAC, ID/MAC 불일치, DB 오류, 연결 실패에는 모두 `RESULT_FAIL`을 보냅니다.
이 요청은 일반 TCP 로그인 상태를 만들지 않으며, 센서·채팅·등록 명령은 여전히 TCP 로그인이 필요합니다.
BT 링크로 들어오는 연결 관리 명령은 거부합니다. 기존 센서·채팅의 TCP/BT 공용 패킷 처리는 유지합니다.

서버의 `RequestRegisteredBluetoothConnection()`을 외부 요청과 CLI 연결 경로가 공용으로 사용합니다.
CLI는 `RPacketBtConnectMember(id)`를 통해 DB MAC을 조회하고, 외부 요청은 검증된 ID와 요청 MAC을 전달합니다.
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

## 설정 파일 (ServerConfig.json)

모든 설정은 `Config/Src/RConfig.c`가 이 파일 하나에서 읽고 씁니다. 코드에서는 `RConfigGet()->database.host`처럼 하나뿐인 설정 구조체에서 바로 읽습니다.
경로는 실행 디렉터리 기준이므로 `Raspberry5/`에서 서버를 실행합니다. JSON 라이브러리는 cJSON(`sudo apt install libcjson-dev`)입니다.

```json
{
    "server":   { "ip": "127.0.0.1", "port": 8080, "workerCount": 8 },
    "database": { "host": "127.0.0.1", "port": 3306, "user": "iot", "password": "DB 계정 비밀번호", "name": "iot", "timeoutSeconds": 5 }
}
```

- DB 비밀번호가 있으므로 `chmod 600`이 필수이고 Git에서 제외합니다. 일반 파일만 읽으며 심볼릭 링크와 그룹/다른 사용자 권한을 거부합니다.
- 모든 구역과 키가 필수입니다. 빠진 구역·키, 알 수 없는 구역·키(오타), 잘못된 타입, 범위를 벗어난 값은 거부하고 기존 설정을 그대로 유지합니다. 오류 로그에 `"server.port"`처럼 위치가 나옵니다.
- `database.password`는 MySQL 접속 계정의 비밀번호이며 회원 테이블의 `pw_hash`와는 별개입니다. 빈 문자열을 허용합니다.
- 시작할 때와 `server start`마다 다시 읽으므로, 파일을 고친 뒤 `server start`로 재시도할 수 있습니다.
- `RConfigSave()`는 임시 파일에 쓴 뒤 교체하며 600 권한으로 저장합니다.

## 테스트

저장소에는 자동 테스트가 없습니다. 실제 DB 대신 모의 함수를 연결한 별도 하네스로 TCP 로그인·권한, 프레임 분할/CRC/타임아웃, 모의 BT 장치의 DHT·`REQ_FAN`/`ACK_FAN`, 동시 접속 부하를 확인했습니다.

## 클라이언트 접속

클라이언트(PC 앱 등)는 `server.ip:server.port`로 일반 TCP 접속한 뒤 `common/IoTPacketCodec.h`의 `Make*Packet()` 프레임을 그대로 보냅니다.
암호화가 없으므로 로그인·BT 연결 요청의 비밀번호가 네트워크에 평문으로 지나갑니다. 신뢰할 수 있는 내부망에서만 사용합니다.
같은 IP에서 새로 접속하면 그 IP의 기존 TCP 연결은 닫습니다(재접속한 클라이언트의 끊긴 연결이 남지 않도록). IP당 TCP 연결은 하나입니다.
