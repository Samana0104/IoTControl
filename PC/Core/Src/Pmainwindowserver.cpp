#include "Pdashboardwidget.h"
#include "Pmainwindow.h"
#include "ui_Pmainwindow.h"

#include <QIntValidator>

void MainWindow::InitializeServerControls()
{
    connect(dashboard, &DashboardWidget::ServerChangeRequested, this,
            &MainWindow::ShowServerConnection);
    connect(ui->changeServerButton, &QPushButton::clicked, this,
            &MainWindow::ShowServerConnection);
    // 버튼 함수 연결 예제: setupUi(this) 이후 버튼의 clicked 신호를 함수에
    // 연결합니다. 함수 포인터 방식이므로 ConfirmServerPreview는 일반 private
    // 멤버 함수여도 됩니다.
    connect(ui->connectServerButton, &QPushButton::clicked, this,
            &MainWindow::ConfirmServerPreview);
    ui->serverPortInput->setValidator(
        new QIntValidator(1, 65535, ui->serverPortInput));
    for (auto *input : {ui->serverHostInput, ui->serverPortInput})
    {
        QPalette palette = input->palette();
        palette.setColor(QPalette::PlaceholderText, QColor("#878d9a"));
        input->setPalette(palette);
        connect(input, &QLineEdit::textChanged, this,
                &MainWindow::ClearServerFeedback);
        connect(input, &QLineEdit::returnPressed, this,
                &MainWindow::ConfirmServerPreview);
    }
}

void MainWindow::ConfirmServerPreview()
{
    // 버튼 함수 구현 예제 1: 화면의 입력값을 읽고 유효성을 검사합니다.
    const QString HOST = ui->serverHostInput->text().trimmed();
    ClearServerFeedback();
    QLineEdit *invalidInput = nullptr;
    if (HOST.isEmpty())
    {
        invalidInput = ui->serverHostInput;
        ui->serverFeedback->setText(tr("우분투 서버 주소를 입력해 주세요."));
    }
    else if (!ui->serverPortInput->hasAcceptableInput())
    {
        invalidInput = ui->serverPortInput;
        ui->serverFeedback->setText(tr("포트를 1–65535 범위로 입력해 주세요."));
    }
    if (invalidInput)
    {
        invalidInput->setProperty("invalid", true);
        RefreshStyle(invalidInput);
        invalidInput->setFocus();
        return;
    }

    // 버튼 함수 구현 예제 2: 검증된 서버 설정을 목업 화면에 반영합니다.
    // serverPreviewReady는 설정 검토 완료를 뜻하며 실제 소켓 연결 상태는
    // 아닙니다.
    const int PORT = ui->serverPortInput->text().toInt();
    serverPreviewReady = true;
    ui->serverHostInput->setText(HOST);
    ui->loginServerSummary->setText(tr("SERVER / %1:%2").arg(HOST).arg(PORT));
    ui->loginServerSummary->setToolTip(ui->loginServerSummary->text());
    dashboard->SetServerPreview(HOST, PORT);
    ui->loginButton->setEnabled(true);
    ui->previewDashboardButton->setEnabled(true);
    // 버튼 함수 구현 예제 3: 설정 검토가 끝나면 로그인 화면으로 전환합니다.
    ShowLogin();
}

void MainWindow::ClearServerFeedback()
{
    ui->serverFeedback->clear();
    for (auto *input : {ui->serverHostInput, ui->serverPortInput})
    {
        input->setProperty("invalid", false);
        RefreshStyle(input);
    }
}
