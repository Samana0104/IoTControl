# 서버 연결 버튼과 실행 함수

## 검토할 소스

- `Panel/PAccessPanel.cpp`: `AccessPanel::InitializeInputEvents()`가 버튼 클릭·Enter를 `ServerConnectRequested()` 신호로 전달합니다.
- `Core/Src/PMainWindowServer.cpp`: `MainWindow::InitializeServerControls()`에서 신호를 `MainWindow::ConnectToServer()`에 연결합니다.
- 같은 파일의 `MainWindow::ConnectToServer()`: 주소·포트 검증, 접속 중 UI 잠금, TCP 접속 요청을 수행합니다.
- `Core/Src/PServerConnection.cpp`: `ServerConnection::ConnectToServer()`의 `socket->connectToHost(...)`가 실제 TCP 접속을 시작합니다.
- 같은 파일의 `SERVER_CONNECTION_TIMEOUT_MS`: 최초 접속 제한 시간은 10초입니다.

```cpp
connect(accessPanel, &AccessPanel::ServerConnectRequested,
        this, &MainWindow::ConnectToServer);
```

```cpp
// ServerConnection::ConnectToServer()
active = true;
connectionTimer->start();
socket->connectToHost(host.trimmed(), port);
```

`connectToHost()`는 비동기입니다. `waitForConnected()`로 화면을 멈추지 않고 `Connected()` 신호에서 다음 단계를 진행합니다. 실패와 시간 초과는 `ConnectionFailed()`로 전달합니다.

## 접속 이후 흐름

1. TCP 연결 성공 → `MainWindow::HandleServerConnected()` → 서버 주소 표시, 로그인 활성화, 로그인 화면 전환.
2. 접속 실패·시간 초과 → `MainWindow::HandleServerConnectionFailed()` → 서버 화면 복귀, 버튼·입력칸 복원, 오류 표시.
3. 서버 연결 종료 → `MainWindow::HandleServerDisconnected()` → 비밀번호 지우기, 로그인 비활성화, 재접속 안내.
4. 서버 변경·창 종료 → `ServerConnection::DisconnectFromServer()` → 타이머 중지, 소켓 종료. 의도한 종료에는 오류 안내를 발생시키지 않습니다.

주소와 포트는 화면 입력값을 사용하며 코드에 우분투 서버의 IP를 고정하지 않았습니다. 포트 기본값은 5000이므로 실제 서버 설정에 맞게 입력합니다. DNS 호스트 이름과 IP 주소를 입력할 수 있습니다.

## 연결 해제 버튼

로그인 화면의 서버 주소 옆과 대시보드 왼쪽 하단에 **연결 해제** 버튼이 있습니다. 버튼 이름은 각각 `disconnectServerButton`, `dashboardDisconnectServerButton`입니다.

`AccessPanel` 또는 `DashboardWidget`의 `ServerDisconnectRequested()` 신호를 `MainWindow::InitializeServerControls()`에서 `MainWindow::DisconnectFromServer()`에 연결합니다. 실행 함수는 `Core/Src/PMainWindowServer.cpp`에 있습니다.

버튼을 누르면 `ShowServerConnection()`을 통해 소켓과 접속 타이머를 종료하고, 비밀번호를 지우고, 로그인과 대시보드 미리보기를 비활성화한 뒤 서버 접속 화면으로 돌아갑니다. 화면에는 `서버 연결을 해제했습니다.`를 표시합니다. 입력한 서버 주소·포트는 유지하여 다시 접속할 수 있습니다.

## 로그인 송신·수신 경계

`Core/Src/PMainWindowLogin.cpp`의 `MainWindow::SubmitLogin()`은 `ReadLoginMember()`로 화면의 ID·PW를 읽고 UTF-8 바이트 길이를 검사합니다. `SaveLoginUsername()`은 선택한 경우 ID만 저장합니다. 패킷 생성과 응답 처리는 `Core/Src/PServerLogin.cpp`의 `ServerLogin`이 담당합니다.

`ServerLogin::LoginToServer(member)` → 공용 `MakeLoginPacket()` → `ServerConnection::SendPacket()` → `QTcpSocket::write()` 순서로 `REQ_LOGIN`을 전송합니다. `MainWindow::LoginRequested(packet)`은 송신을 관찰하는 기존 신호로 유지하며, 이 신호에서 패킷을 다시 송신하지 않습니다. 전송 후 지역 `MemData`·UTF-8 비밀번호·프레임을 지우고 비밀번호 입력칸을 초기화합니다.

`ServerConnection::DataReceived()`의 TCP 청크를 `ServerLogin::ReceiveLoginData()`가 헤더와 payload 길이에 따라 조립합니다. CRC, `ACK_LOGIN` 길이, `ResultData`를 검증합니다. 기다리는 ACK가 아닌 정상 프레임은 넘기고, 공용 수신 한도를 넘는 타 명령의 payload는 메모리를 늘리지 않고 분할 폐기합니다. 기본 응답 대기 시간은 5초이며 조각이 도착해도 타이머를 다시 시작하지 않습니다. 응답 대기 중에는 중복 로그인과 대시보드 미리보기를 막고 서버 변경·연결 해제는 사용할 수 있습니다.

`LoginFinished(LoginResult)` → `MainWindow::HandleLoginResult()`에서 결과를 표시합니다.

| 결과 | 화면 동작 |
| --- | --- |
| `LOGIN_SUCCESS` | `HandleLoginSuccess()` → 대시보드 이동 → dht 전체 조회 |
| `LOGIN_REJECTED` | 로그인 화면에서 ID·PW 확인 안내, 재입력 허용 |
| `LOGIN_NO_SERVER` | 서버 접속 화면 복귀 |
| `LOGIN_TIMEOUT` | 연결 종료 후 서버 접속 화면에 시간 초과 안내 |
| `LOGIN_BAD_PACKET` | 연결 종료 후 서버 접속 화면에 잘못된 응답 안내 |

서버 변경·연결 해제·창 파괴 시 로그인 타이머와 수신 중인 프레임을 취소합니다. 수신 원문은 기존 `MainWindow::ServerDataReceived(const QByteArray &data)` 신호로도 전달합니다. 로그인 성공 직후와 이후 5초마다 dht 전체 조회를 요청하며 DB 새로고침도 같은 함수에 연결됩니다. 전체 현장 갱신 버튼은 `REQ_DHT_COLLECT`를 보내고, `ACK_DHT_COLLECT`로 요청 전송 여부를 확인합니다. 측정 결과는 이후 DB 조회로 반영합니다. 개별 기기 갱신만 준비 중이며 팬 명령은 후속 구현 대상입니다. 흐름과 DB 구조 확인 사항은 [dht 조회와 현장 갱신](LoginDhtQuery.md)을 참고합니다. 대시보드 UI 미리보기 버튼은 연결 성공 후 응답 대기 중이 아닐 때 사용할 수 있으며, 실제 DB 조회에는 로그인이 필요합니다.

## 검증

`tests/PServerConnectionTest.cpp`는 로컬 `QTcpServer`로 접속·접속 거절·재시도·연결 종료·의도한 취소·타임아웃 이벤트·UI 상태·로그인 프레임 실제 송신을 검증합니다. 연결 해제 버튼은 로그인 및 대시보드의 각 메뉴에서 소켓 종료, 비밀번호 초기화, 접속 화면 복귀, 재접속과 최소 창 크기의 버튼 배치를 검증합니다. 타임아웃 이벤트는 테스트에서 직접 발생시키며, 실제 외부 서버의 10초 무응답 상황을 재현하는 테스트는 아닙니다. 실제 우분투 서버 주소가 제공되지 않아 현장 서버 접속 검증은 수행하지 않았습니다.

`tests/PServerLoginTest.cpp`는 로컬 서버로 로그인 성공·거절·재시도, 한 바이트 단위의 분할 수신, 여러 프레임의 동시 수신, 로그인 전후에 걸친 센서 프레임 조각, 타 명령·큰 프레임 건너뛰기, CRC·길이·결과값 오류, 실제 단축 타이머의 시간 초과, 이벤트 루프 응답, 연결 종료·취소 후 재접속, 화면 상태를 검증합니다. UI 시간 초과 테스트는 타이머를 짧게 설정하여 실행합니다.

Qt와 컴파일러를 PATH에 넣은 빌드 환경에서:

```powershell
cmake -S . -B build -DIOT_BUILD_TESTS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

실행은 기존 `Run-Mockup.cmd` 또는 `Run-Login-Mockup.cmd`를 사용합니다.
