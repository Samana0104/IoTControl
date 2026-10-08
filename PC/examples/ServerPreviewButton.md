# 서버 연결 미리보기 버튼에 함수 연결하기

화면은 `Panel`, 실행 함수의 선언은 `Core/Inc`, 구현은 `Core/Src`에 있습니다. Qt Designer 파일은 `screens`에 유지합니다.

## 1. 디자인에서 클릭 신호 전달

`screens/Pmainwindow.ui`의 버튼 이름은 `connectServerButton`입니다.
`Panel/Paccesspanel.cpp`의 `AccessPanel::InitializeInputEvents()`는 버튼 클릭을 화면의 요청 신호로 전달합니다.

```cpp
connect(ui->connectServerButton, &QPushButton::clicked,
        this, &AccessPanel::ServerPreviewRequested);
```

주소·포트 입력칸에서 Enter를 눌러도 같은 신호가 발생합니다. 디자인 클래스에서는 소켓 연결이나 로그인 패킷을 처리하지 않습니다.

## 2. Core에서 실행 함수 연결

`Core/Inc/Pmainwindow.h`의 `MainWindow` 클래스에 실행 함수를 선언합니다.

```cpp
void ConfirmServerPreview();
```

`Core/Src/Pmainwindowserver.cpp`의 `MainWindow::InitializeServerControls()`에서 화면 신호를 함수에 연결합니다.

```cpp
connect(accessPanel, &AccessPanel::ServerPreviewRequested,
        this, &MainWindow::ConfirmServerPreview);
```

`MainWindow` 생성자는 화면을 만든 뒤 `InitializeServerControls()`를 호출합니다. 실행 함수는 일반 `private` 멤버 함수여도 됩니다.

## 3. 입력 읽기와 결과 표시

같은 파일의 `MainWindow::ConfirmServerPreview()`가 입력을 읽고 서버 주소와 포트를 검증합니다.

```cpp
const QString HOST = accessPanel->ReadServerHost().trimmed();
bool validPort = false;
const int PORT = accessPanel->ReadServerPort().toInt(&validPort);
```

잘못된 입력은 `accessPanel->ShowServerError(...)`로 표시하고, 검증된 값은 `accessPanel->SetServerSummary(HOST, PORT)`로 반영합니다. 이후 `ShowLogin()`으로 로그인 화면을 엽니다.

현재는 서버 설정을 검토하는 UI 목업이며 실제 소켓 연결은 하지 않습니다. 통신을 구현할 때는 이 실행 함수에서 연결 서비스를 호출하고, 연결 성공 결과를 받은 뒤 로그인 화면을 열면 됩니다.

다른 기능도 **버튼 → Panel 요청 신호 → Core 실행 함수 → Panel 결과 표시** 순서로 연결합니다. 로그인은 `AccessPanel::LoginSubmitted`가 `MainWindow::SubmitLogin()`에 연결되어 있으며, 기존 공용 규격의 패킷은 `MainWindow::LoginRequested(QByteArray)`로 송신부에 전달됩니다.
