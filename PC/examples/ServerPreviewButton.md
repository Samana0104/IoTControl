# 서버 연결 미리보기 버튼에 함수 연결하기

현재 프로그램에서 실행되는 버튼 연결 예제입니다. UI 파일은 `screens`, 함수 선언은 `Core/Inc`, 함수 구현은 `Core/Src`에 있습니다.

## 1. 버튼 이름 확인

`screens/Pmainwindow.ui`의 서버 연결 미리보기 버튼은 `objectName`이 `connectServerButton`입니다. Qt Designer에서 이 이름을 확인할 수 있습니다.

## 2. 헤더에 함수 선언

`Core/Inc/Pmainwindow.h`의 `MainWindow` 클래스 `private` 영역:

```cpp
void ConfirmServerPreview();
```

## 3. 버튼 클릭과 함수 연결

`Core/Src/Pmainwindow.cpp`의 생성자는 `ui->setupUi(this)` 이후 `InitializeServerControls()`를 호출합니다. 버튼 연결 코드는 `Core/Src/Pmainwindowserver.cpp`의 `MainWindow::InitializeServerControls()`에 있습니다.

```cpp
connect(ui->connectServerButton, &QPushButton::clicked,
        this, &MainWindow::ConfirmServerPreview);
```

- `ui->connectServerButton`: 클릭을 받는 버튼
- `&QPushButton::clicked`: 버튼을 누르면 발생하는 신호
- `this`: 함수를 실행할 현재 `MainWindow` 객체
- `&MainWindow::ConfirmServerPreview`: 클릭할 때 실행할 함수

함수 포인터로 연결하므로 `ConfirmServerPreview()`는 일반 `private` 멤버 함수로 선언할 수 있습니다. 현재 프로그램에는 위 연결이 이미 적용되어 있습니다.

## 4. 호출되는 함수 구현

`Core/Src/Pmainwindowserver.cpp`의 `void MainWindow::ConfirmServerPreview()`가 실행됩니다. 실제 함수의 입력값 읽기 부분은 다음과 같습니다.

```cpp
const QString HOST = ui->serverHostInput->text().trimmed();
const int PORT = ui->serverPortInput->text().toInt();
```

현재 함수는 서버 주소가 비어 있는지, 포트가 1–65535 범위인지 검사합니다. 잘못된 입력이면 `serverFeedback`에 안내하고 입력칸으로 포커스를 이동합니다. 검증에 통과하면 서버 설정을 화면에 반영하고 `ShowLogin()`으로 로그인 화면을 엽니다.

주소·포트 입력칸에서 Enter를 눌러도 같은 `ConfirmServerPreview()` 함수를 호출합니다. 이 예제는 서버 설정을 검토하는 UI 목업이며 실제 소켓 연결은 하지 않습니다.

다른 버튼에도 같은 방식으로 적용할 수 있습니다. 헤더에 새 함수(예: `HandleButtonClick()`)를 선언하고, 해당 기능의 `.cpp`에 `void MainWindow::HandleButtonClick()`을 구현한 다음, 위 `connect()`의 버튼과 함수 이름을 바꾸면 됩니다. 여러 `.cpp` 파일로 나뉘어도 선언과 구현은 같은 `MainWindow` 클래스에 속합니다.
