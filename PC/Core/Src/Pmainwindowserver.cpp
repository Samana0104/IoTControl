#include "Pmainwindow.h"

#include "Paccesspanel.h"
#include "Pdashboardwidget.h"
#include "Pserverconnection.h"

void MainWindow::InitializeServerControls()
{
    connect(dashboard, &DashboardWidget::ServerChangeRequested, this, &MainWindow::ShowServerConnection);
    connect(accessPanel, &AccessPanel::ServerChangeRequested, this, &MainWindow::ShowServerConnection);
    connect(accessPanel, &AccessPanel::ServerConnectRequested, this, &MainWindow::ConnectToServer);
    connect(accessPanel, &AccessPanel::ServerDisconnectRequested, this, &MainWindow::DisconnectFromServer);
    connect(dashboard, &DashboardWidget::ServerDisconnectRequested, this, &MainWindow::DisconnectFromServer);
    connect(serverConnection, &ServerConnection::Connected, this, &MainWindow::HandleServerConnected);
    connect(serverConnection, &ServerConnection::Disconnected, this, &MainWindow::HandleServerDisconnected);
    connect(serverConnection, &ServerConnection::ConnectionFailed, this, &MainWindow::HandleServerConnectionFailed);
    connect(serverConnection, &ServerConnection::DataReceived, this, &MainWindow::ServerDataReceived);
}

void MainWindow::ConnectToServer()
{
    // 버튼과 Enter가 동시에 발생해도 한 번만 접속합니다.
    if (serverConnection->IsConnecting())
        return;
    const QString HOST = accessPanel->ReadServerHost().trimmed();
    bool validPort = false;
    const int PORT = accessPanel->ReadServerPort().toInt(&validPort);
    accessPanel->ClearServerFeedback();
    if (HOST.isEmpty())
    {
        accessPanel->ShowServerError(tr("우분투 서버 주소를 입력해 주세요."), true);
        return;
    }
    if (!validPort || PORT < 1 || PORT > 65535)
    {
        accessPanel->ShowServerError(tr("포트를 1–65535 범위로 입력해 주세요."), false);
        return;
    }
    // 비동기 접속 결과가 오기 전까지 입력값과 로그인 상태를 고정합니다.
    serverHost = HOST;
    serverPort = static_cast<quint16>(PORT);
    accessPanel->SetLoginEnabled(false);
    accessPanel->SetServerConnecting(true);
    accessPanel->SetServerFeedback(tr("%1:%2 서버에 연결 중입니다…").arg(HOST).arg(PORT));
    serverConnection->ConnectToServer(serverHost, serverPort);
}

void MainWindow::DisconnectFromServer()
{
    // 서버 화면 복귀 시 소켓·타이머 종료와 비밀번호 초기화를 함께 수행합니다.
    ShowServerConnection();
    accessPanel->SetServerFeedback(tr("서버 연결을 해제했습니다."));
}

void MainWindow::HandleServerConnected()
{
    accessPanel->SetServerConnecting(false);
    accessPanel->ClearServerFeedback();
    accessPanel->SetServerSummary(serverHost, serverPort);
    dashboard->SetServerEndpoint(serverHost, serverPort);
    accessPanel->SetLoginEnabled(true);
    ShowLogin();
}

void MainWindow::HandleServerDisconnected()
{
    ShowServerConnection();
    accessPanel->SetServerFeedback(tr("서버와의 연결이 종료되었습니다. 다시 접속해 주세요."));
}

void MainWindow::HandleServerConnectionFailed(const QString &message)
{
    ShowServerConnection();
    accessPanel->SetServerFeedback(tr("서버 접속 실패: %1").arg(message));
}
