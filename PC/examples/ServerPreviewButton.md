# 서버 연결 버튼과 실행 함수

## 검토할 소스

- `Panel/Paccesspanel.cpp`: `AccessPanel::InitializeInputEvents()`가 버튼 클릭·Enter를 `ServerConnectRequested()` 신호로 전달합니다.
- `Core/Src/Pmainwindowserver.cpp`: `MainWindow::InitializeServerControls()`에서 신호를 `MainWindow::ConnectToServer()`에 연결합니다.
- 같은 파일의 `MainWindow::ConnectToServer()`: 주소·포트 검증, 접속 중 UI 잠금, TCP 접속 요청을 수행합니다.
- `Core/Src/Pserverconnection.cpp`: `ServerConnection::ConnectToServer()`의 `socket->connectToHost(...)`가 실제 TCP 접속을 시작합니다.
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

`AccessPanel` 또는 `DashboardWidget`의 `ServerDisconnectRequested()` 신호를 `MainWindow::InitializeServerControls()`에서 `MainWindow::DisconnectFromServer()`에 연결합니다. 실행 함수는 `Core/Src/Pmainwindowserver.cpp`에 있습니다.

버튼을 누르면 `ShowServerConnection()`을 통해 소켓과 접속 타이머를 종료하고, 비밀번호를 지우고, 로그인과 대시보드 미리보기를 비활성화한 뒤 서버 접속 화면으로 돌아갑니다. 화면에는 `서버 연결을 해제했습니다.`를 표시합니다. 입력한 서버 주소·포트는 유지하여 다시 접속할 수 있습니다.

## 로그인 송신·수신 경계

`Core/Src/Pmainwindowlogin.cpp`의 `MainWindow::SubmitLogin()`은 기존 공용 `MakeLoginPacket()`으로 `REQ_LOGIN` 프레임을 생성합니다. `LoginRequested(packet)` → `MainWindow::SendLoginPacket()` → `ServerConnection::SendPacket()` → `QTcpSocket::write()`로 같은 TCP 연결에 전송합니다.

수신 바이트는 `MainWindow::ServerDataReceived(const QByteArray &data)`로 전달합니다. 이 값은 TCP 청크이므로 한 번의 신호가 한 패킷이라는 보장은 없습니다. **이번 구현은 접속과 바이트 송수신까지이며 `ACK_LOGIN` 프레임 조립·인증 결과 처리, DB 조회·장치 명령은 아직 구현하지 않았습니다.** 로그인 응답을 받았다고 대시보드로 자동 이동하지 않습니다. 기존 대시보드 UI 미리보기 버튼은 연결 성공 후 사용할 수 있습니다.

## 검증

`tests/Pserverconnectiontest.cpp`는 로컬 `QTcpServer`로 접속·접속 거절·재시도·연결 종료·의도한 취소·타임아웃 이벤트·UI 상태·로그인 프레임 실제 송신을 검증합니다. 연결 해제 버튼은 로그인 및 대시보드의 각 메뉴에서 소켓 종료, 비밀번호 초기화, 접속 화면 복귀, 재접속과 최소 창 크기의 버튼 배치를 검증합니다. 타임아웃 이벤트는 테스트에서 직접 발생시키며, 실제 외부 서버의 10초 무응답 상황을 재현하는 테스트는 아닙니다. 실제 우분투 서버 주소가 제공되지 않아 현장 서버 접속 검증은 수행하지 않았습니다.

Qt와 컴파일러를 PATH에 넣은 빌드 환경에서:

```powershell
cmake -S . -B build -DIOT_BUILD_TESTS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

실행은 기존 `Run-Mockup.cmd` 또는 `Run-Login-Mockup.cmd`를 사용합니다.
