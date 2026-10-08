# 디자인과 실행 함수 분리

| 위치 | 역할 | 주요 파일 |
| --- | --- | --- |
| `screens/*.ui` | Qt Designer 레이아웃, 문구, 스타일 | `Pmainwindow.ui`, `Pdashboard.ui`, `Pbluetoothdialog.ui` |
| `Panel/*.h`, `Panel/*.cpp` | 화면 구성, 입력 표시, 애니메이션, 버튼 요청 신호 | `Paccesspanel`, `Paccesspaneltransition`, `Pdashboardpanel`, `Pbluetoothpanel` |
| `widget/*.h`, `widget/*.cpp` | 재사용하는 시각 요소 | `Pconnectioncanvas`, `Pfeaturedetailswidget`, `Pfanchartwidget`, `Pfanrotorwidget`, `Pgamingtheme` |
| `Core/Inc/*.h` | 실행 클래스 선언 | `Pmainwindow.h`, `Pdashboardwidget.h`, `Pbluetoothdialog.h` |
| `Core/Src/*.cpp` | 화면 흐름, 입력 검증, 패킷 생성, 기능 처리 | `Pmainwindowserver.cpp`, `Pmainwindowlogin.cpp`, `Pdashboardwidget.cpp`, `Pbluetoothdialog.cpp` |

`Panel` 클래스는 `QObject`로 기존 창의 UI를 관리합니다. 창이나 레이아웃을 하나 더 감싸지 않아 기존 화면 크기와 위젯 위치를 유지합니다. 생성된 `ui_*.h`는 `Panel`에서만 사용하며, 실행 코드는 `Read...()`, `Set...()`, `Show...()` 함수로 화면과 데이터를 주고받습니다.

## 로그인 호출 흐름

1. `AccessPanel`이 로그인 버튼 클릭 또는 Enter 입력을 `LoginSubmitted()` 신호로 전달합니다.
2. `MainWindow::SubmitLogin()`이 입력을 읽고 공용 통신 규격의 ID·PW 길이를 검사합니다.
3. 공용 `MakeLoginPacket()`으로 기존 로그인 패킷을 생성합니다.
4. `MainWindow::LoginRequested(const QByteArray &packet)` 신호로 송신부에 전달합니다.
5. 오류 문구와 입력칸 강조는 `AccessPanel`의 표시 함수를 통해 반영합니다.

디자인 변경은 `screens`, `Panel`, `widget`에서 하고 실행 기능 추가는 `Core`에서 합니다. 예를 들어 로그인 패킷 함수를 수정할 때 색상·버튼 배치·화면 전환 애니메이션을 수정할 필요가 없습니다.

대시보드의 DB 조회·전체 갱신·보드별 갱신·팬 적용 버튼도 각각 요청 신호와 실행 함수로 연결되어 있습니다. 블루투스 화면은 ID·PW를 읽는 표시 클래스와 입력 검증·요청 전달을 수행하는 실행 클래스로 나뉩니다. 실제 통신 기능은 기존과 같이 미연결 상태입니다.

새 소스 파일을 추가하면 `CMakeLists.txt`에 등록합니다. 실행은 기존 `Run-Mockup.cmd` 또는 `Run-Login-Mockup.cmd`를 사용합니다.
