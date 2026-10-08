#include "Paccesspanel.h"
#include "Pdashboardwidget.h"
#include "Pmainwindow.h"

void MainWindow::InitializeServerControls()
{
    connect(dashboard, &DashboardWidget::ServerChangeRequested, this,
            &MainWindow::ShowServerConnection);
    connect(accessPanel, &AccessPanel::ServerChangeRequested, this,
            &MainWindow::ShowServerConnection);
    // 디자인 코드가 전달한 요청을 실행 함수에 연결합니다.
    connect(accessPanel, &AccessPanel::ServerPreviewRequested, this,
            &MainWindow::ConfirmServerPreview);
}

void MainWindow::ConfirmServerPreview()
{
    const QString HOST = accessPanel->ReadServerHost().trimmed();
    bool validPort = false;
    const int PORT = accessPanel->ReadServerPort().toInt(&validPort);
    accessPanel->ClearServerFeedback();
    if (HOST.isEmpty())
    {
        accessPanel->ShowServerError(tr("우분투 서버 주소를 입력해 주세요."),
                                     true);
        return;
    }
    if (!validPort || PORT < 1 || PORT > 65535)
    {
        accessPanel->ShowServerError(tr("포트를 1–65535 범위로 입력해 주세요."),
                                     false);
        return;
    }
    // 설정 검토 완료 상태이며 실제 소켓 연결 상태는 아닙니다.
    serverPreviewReady = true;
    accessPanel->SetServerSummary(HOST, PORT);
    dashboard->SetServerPreview(HOST, PORT);
    accessPanel->SetLoginEnabled(true);
    ShowLogin();
}
