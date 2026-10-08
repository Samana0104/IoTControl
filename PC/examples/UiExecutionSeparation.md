# 디자인과 실행 함수 분리

| 위치 | 역할 | 주요 파일 |
| --- | --- | --- |
| `screens/*.ui` | Qt Designer 레이아웃, 문구, 스타일 | `PMainWindow.ui`, `PDashboard.ui`, `PBluetoothDialog.ui` |
| `Panel/*.h`, `Panel/*.cpp` | 화면 구성, 입력 표시, 애니메이션, 버튼 요청 신호 | `PAccessPanel`, `PAccessPanelTransition`, `PDashboardPanel`, `PBluetoothPanel` |
| `widget/*.h`, `widget/*.cpp` | 재사용하는 시각 요소 | `PConnectionCanvas`, `PFeatureDetailsWidget`, `PFanChartWidget`, `PFanRotorWidget`, `PGamingTheme` |
| `Core/Inc/*.h` | 실행 클래스·로그인 결과 선언 | `PMainWindow.h`, `PServerLogin.h`, `PLoginResult.h`, `PDashboardWidget.h`, `PBluetoothDialog.h` |
| `Core/Src/*.cpp` | 화면 흐름, 입력 검증, 패킷 송수신, 기능 처리 | `PMainWindowServer.cpp`, `PServerConnection.cpp`, `PServerLogin.cpp`, `PMainWindowLogin.cpp`, `PDashboardWidget.cpp`, `PBluetoothDialog.cpp` |

`Panel` 클래스는 `QObject`로 기존 창의 UI를 관리합니다. 창이나 레이아웃을 하나 더 감싸지 않아 기존 화면 크기와 위젯 위치를 유지합니다. 생성된 `ui_*.h`는 `Panel`에서만 사용하며, 실행 코드는 `Read...()`, `Set...()`, `Show...()` 함수로 화면과 데이터를 주고받습니다.

## 로그인 호출 흐름

1. `AccessPanel`이 로그인 버튼 클릭 또는 Enter 입력을 `LoginSubmitted()` 신호로 전달합니다.
2. `MainWindow::SubmitLogin()` → `ReadLoginMember()`에서 입력을 읽고 공용 통신 규격의 ID·PW 길이를 검사합니다.
3. `ServerLogin::LoginToServer()`가 공용 `MakeLoginPacket()`으로 패킷을 만들고 `ServerConnection::SendPacket()`으로 전송합니다.
4. `ServerLogin::ReceiveLoginData()`가 TCP 조각을 모아 CRC·길이·`ACK_LOGIN` 결과를 검사합니다. 5초 응답 타이머는 Qt 이벤트 루프에서 동작합니다.
5. `LoginFinished(LoginResult)` → `MainWindow::HandleLoginResult()`에서 성공 시 `HandleLoginSuccess()`를 호출하여 대시보드로 이동하고 dht 전체 조회를 시작합니다. 나머지 결과는 로그인·서버 화면에 표시합니다.
6. `AccessPanel::SetLoginBusy()`가 요청 대기 중 입력·로그인·미리보기 버튼을 잠급니다. 연결 해제·서버 변경은 그대로 사용할 수 있습니다.

`MainWindow::LoginRequested(const QByteArray &packet)`은 기존 송신 관찰 신호입니다. 로그인 요청의 실제 송신은 `ServerLogin`에서 수행하므로 이 신호에 송신 함수를 추가로 연결하지 않습니다.

디자인 변경은 `screens`, `Panel`, `widget`에서 하고 실행 기능 추가는 `Core`에서 합니다. 예를 들어 로그인 패킷 함수를 수정할 때 색상·버튼 배치·화면 전환 애니메이션을 수정할 필요가 없습니다.

대시보드의 DB 조회·전체 갱신·보드별 갱신·팬 적용 버튼도 각각 요청 신호와 실행 함수로 연결되어 있습니다. 블루투스 화면은 ID·PW를 읽는 표시 클래스와 입력 검증·요청 전달을 수행하는 실행 클래스로 나뉩니다. 서버 TCP 접속과 로그인 패킷 송신·ACK 결과, dht 전체 조회와 새로고침은 구현되어 있습니다. `PMainWindowData.cpp`가 조회 실행과 화면 연결을 맡고, `PServerDhtQuery`가 패킷 송수신을 맡습니다. 장치 제어는 후속 구현 대상입니다. 접속 흐름은 [서버 연결 버튼 설명](ServerPreviewButton.md), 조회는 [로그인 후 dht 조회](LoginDhtQuery.md)를 참고합니다.

로그인 후 DB 조회는 `PMainWindowData.cpp`의 5초 타이머로 반복합니다. 같은 파일의 `RequestFieldDataUpdate()`에서 전체 갱신을 `ServerDhtQuery::RequestDhtCollect()`에 연결하고, 개별 갱신은 별도 대상 ID 규격이 없어 준비 중임을 표시합니다. `IoTDhtQuery.c/.h`는 제거하고 행 구조·직렬화 함수를 기존 `IoTPacket`·`IoTPacketCodec`에 통합했습니다. Raspberry 소스는 이번 변경에서 수정하지 않았습니다.

새 소스 파일을 추가하면 `CMakeLists.txt`에 등록합니다. 실행은 기존 `Run-Mockup.cmd` 또는 `Run-Login-Mockup.cmd`를 사용합니다.

## 헤더 의존성 규칙

프로젝트 헤더 사이에는 순환 include가 없습니다. 다른 프로젝트 클래스의 포인터를 멤버로 보관할 때는 헤더에서 전방 선언하고, 객체 생성·멤버 호출·삭제가 있는 구현 파일에서 해당 헤더를 포함합니다.

```cpp
// Core/Inc/PBluetoothDialog.h
class BluetoothPanel;
// BluetoothDialog 클래스 내부
BluetoothPanel *panel;
```

```cpp
// Core/Src/PBluetoothDialog.cpp
#include "PBluetoothDialog.h"

#include "PBluetoothPanel.h"
```

`PBluetoothDialog.h`를 포함하는 모든 파일이 `PBluetoothPanel.h`까지 포함할 필요는 없습니다. 구현 파일은 자신의 헤더를 먼저 포함해 다른 include가 누락을 숨기지 않도록 합니다. 같은 클래스 구현을 나눈 `PMainWindowLogin.cpp`, `PMainWindowServer.cpp`도 `PMainWindow.h`를 먼저 포함합니다.

Qt 타입은 사용할 위치에서 직접 명시합니다. `PMainWindow.h`는 값 멤버인 `QSize`, `QString`과 정수 타입을, `PServerConnection.h`는 `QString`, `QByteArray`, 정수 타입을 포함합니다. `PDashboardPanel.h`와 샘플 데이터를 만드는 `PDashboardWidget.cpp`는 필요한 `QList`, `QStringList`를 각각 포함합니다.

`Ui::MainWindow`, `Ui::DashboardWidget` 등은 Qt가 생성한 UI 보조 클래스의 전방 선언입니다. Core의 `MainWindow`, `DashboardWidget`과 다른 네임스페이스에 있으므로 서로 include하는 구조가 아닙니다. 생성된 `ui_*.h`는 Panel 구현에서 사용합니다.
