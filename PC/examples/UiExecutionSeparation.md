# 디자인과 실행 함수 분리

| 위치 | 역할 | 주요 파일 |
| --- | --- | --- |
| `screens/*.ui` | Qt Designer 레이아웃, 문구, 스타일 | `Pmainwindow.ui`, `Pdashboard.ui`, `Pbluetoothdialog.ui` |
| `Panel/*.h`, `Panel/*.cpp` | 화면 구성, 입력 표시, 애니메이션, 버튼 요청 신호 | `Paccesspanel`, `Paccesspaneltransition`, `Pdashboardpanel`, `Pbluetoothpanel` |
| `widget/*.h`, `widget/*.cpp` | 재사용하는 시각 요소 | `Pconnectioncanvas`, `Pfeaturedetailswidget`, `Pfanchartwidget`, `Pfanrotorwidget`, `Pgamingtheme` |
| `Core/Inc/*.h` | 실행 클래스 선언 | `Pmainwindow.h`, `Pdashboardwidget.h`, `Pbluetoothdialog.h` |
| `Core/Src/*.cpp` | 화면 흐름, 입력 검증, 패킷 생성, 기능 처리 | `Pmainwindowserver.cpp`, `Pserverconnection.cpp`, `Pmainwindowlogin.cpp`, `Pdashboardwidget.cpp`, `Pbluetoothdialog.cpp` |

`Panel` 클래스는 `QObject`로 기존 창의 UI를 관리합니다. 창이나 레이아웃을 하나 더 감싸지 않아 기존 화면 크기와 위젯 위치를 유지합니다. 생성된 `ui_*.h`는 `Panel`에서만 사용하며, 실행 코드는 `Read...()`, `Set...()`, `Show...()` 함수로 화면과 데이터를 주고받습니다.

## 로그인 호출 흐름

1. `AccessPanel`이 로그인 버튼 클릭 또는 Enter 입력을 `LoginSubmitted()` 신호로 전달합니다.
2. `MainWindow::SubmitLogin()`이 입력을 읽고 공용 통신 규격의 ID·PW 길이를 검사합니다.
3. 공용 `MakeLoginPacket()`으로 기존 로그인 패킷을 생성합니다.
4. `MainWindow::LoginRequested(const QByteArray &packet)` 신호를 `MainWindow::SendLoginPacket()`에 연결해 서버 소켓으로 전송합니다.
5. 오류 문구와 입력칸 강조는 `AccessPanel`의 표시 함수를 통해 반영합니다.

디자인 변경은 `screens`, `Panel`, `widget`에서 하고 실행 기능 추가는 `Core`에서 합니다. 예를 들어 로그인 패킷 함수를 수정할 때 색상·버튼 배치·화면 전환 애니메이션을 수정할 필요가 없습니다.

대시보드의 DB 조회·전체 갱신·보드별 갱신·팬 적용 버튼도 각각 요청 신호와 실행 함수로 연결되어 있습니다. 블루투스 화면은 ID·PW를 읽는 표시 클래스와 입력 검증·요청 전달을 수행하는 실행 클래스로 나뉩니다. 서버 TCP 접속과 로그인 패킷 송신은 구현되어 있습니다. 로그인 ACK 처리, DB 조회·장치 제어는 후속 구현 대상입니다. 접속 흐름은 [서버 연결 버튼 설명](ServerPreviewButton.md)을 참고합니다.

새 소스 파일을 추가하면 `CMakeLists.txt`에 등록합니다. 실행은 기존 `Run-Mockup.cmd` 또는 `Run-Login-Mockup.cmd`를 사용합니다.

## 헤더 의존성 규칙

프로젝트 헤더 사이에는 순환 include가 없습니다. 다른 프로젝트 클래스의 포인터를 멤버로 보관할 때는 헤더에서 전방 선언하고, 객체 생성·멤버 호출·삭제가 있는 구현 파일에서 해당 헤더를 포함합니다.

```cpp
// Core/Inc/Pbluetoothdialog.h
class BluetoothPanel;
// BluetoothDialog 클래스 내부
BluetoothPanel *panel;
```

```cpp
// Core/Src/Pbluetoothdialog.cpp
#include "Pbluetoothdialog.h"

#include "Pbluetoothpanel.h"
```

`Pbluetoothdialog.h`를 포함하는 모든 파일이 `Pbluetoothpanel.h`까지 포함할 필요는 없습니다. 구현 파일은 자신의 헤더를 먼저 포함해 다른 include가 누락을 숨기지 않도록 합니다. 같은 클래스 구현을 나눈 `Pmainwindowlogin.cpp`, `Pmainwindowserver.cpp`도 `Pmainwindow.h`를 먼저 포함합니다.

Qt 타입은 사용할 위치에서 직접 명시합니다. `Pmainwindow.h`는 값 멤버인 `QSize`, `QString`과 정수 타입을, `Pserverconnection.h`는 `QString`, `QByteArray`, 정수 타입을 포함합니다. `Pdashboardpanel.h`와 샘플 데이터를 만드는 `Pdashboardwidget.cpp`는 필요한 `QList`, `QStringList`를 각각 포함합니다.

`Ui::MainWindow`, `Ui::DashboardWidget` 등은 Qt가 생성한 UI 보조 클래스의 전방 선언입니다. Core의 `MainWindow`, `DashboardWidget`과 다른 네임스페이스에 있으므로 서로 include하는 구조가 아닙니다. 생성된 `ui_*.h`는 Panel 구현에서 사용합니다.
