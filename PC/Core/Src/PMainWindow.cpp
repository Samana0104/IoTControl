#include "PMainWindow.h"

#include "PAccessPanel.h"
#include "PDashboardWidget.h"
#include "PPacketMonitor.h"
#include "PServerBluetooth.h"
#include "PServerConnection.h"
#include "PServerDhtQuery.h"
#include "PServerFanQuery.h"
#include "PServerLogin.h"
#include "PServerSessionQuery.h"

#include <QStackedWidget>
#include <QTimer>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), accessPanel(new AccessPanel(this)), serverConnection(new ServerConnection(this)), packetMonitor(new PacketMonitor(serverConnection, this)), serverLogin(new ServerLogin(serverConnection, this)), serverDhtQuery(new ServerDhtQuery(serverConnection, this)), serverBluetooth(new ServerBluetooth(serverConnection, this)), serverFanQuery(new ServerFanQuery(serverConnection, this)), serverSessionQuery(new ServerSessionQuery(serverConnection, this)), dhtPollTimer(new QTimer(this)), sessionPollTimer(new QTimer(this))
{
    pages = new QStackedWidget(this);
    pages->setObjectName(QStringLiteral("pageStack"));
    pages->addWidget(takeCentralWidget());
    dashboard = new DashboardWidget(pages);
    connect(packetMonitor, &PacketMonitor::LineReady, dashboard, &DashboardWidget::AppendPacketLog);
    pages->addWidget(dashboard);
    setCentralWidget(pages);
    connect(accessPanel, &AccessPanel::DashboardPreviewRequested, this, &MainWindow::ShowDashboard);
    connect(dashboard, &DashboardWidget::ReturnToLogin, this, &MainWindow::ShowLogin);
    InitializeServerControls();
    InitializeLoginControls();
    InitializeDhtControls();
    InitializeFanControls();
    InitializeSessionControls();
    InitializeBluetoothControls();
    loginWindowSize = size();
    ShowServerConnection();
}

MainWindow::~MainWindow()
{
    dhtPollTimer->stop();
    sessionPollTimer->stop();
    serverLogin->CancelLogin();
    serverDhtQuery->CancelQuery();
    serverFanQuery->CancelQuery();
    serverSessionQuery->CancelQuery();
    serverConnection->DisconnectFromServer();
    delete accessPanel;
}

void MainWindow::ShowDashboard()
{
    if (serverLogin->IsLoggingIn())
        return;
    if (!serverConnection->IsConnected())
    {
        ShowServerConnection();
        accessPanel->SetServerFeedback(tr("먼저 서버에 접속해 주세요."));
        return;
    }
    if (pages->currentWidget() == dashboard)
        return;
    accessPanel->FinishAccessTransition();
    loginWindowSize = size();
    pages->setCurrentWidget(dashboard);
    setWindowTitle(tr("IoT Control — UI 목업"));
    resize(1280, 850);
}

void MainWindow::ShowServerConnection()
{
    serverFailureMessage.clear();
    const bool FROM_DASHBOARD = pages->currentWidget() == dashboard;
    const bool ANIMATE = !FROM_DASHBOARD && accessPanel->IsAccessVisible() && !isMinimized();
    authenticated = false;
    backgroundDhtQuery = false;
    dhtPollTimer->stop();
    sessionPollTimer->stop();
    serverDhtQuery->CancelQuery();
    serverFanQuery->CancelQuery();
    serverSessionQuery->CancelQuery();
    dashboard->SetDhtLoading(false);
    dashboard->ResetDhtView();
    serverLogin->CancelLogin();
    accessPanel->SetLoginBusy(false);
    serverConnection->DisconnectFromServer();
    accessPanel->SetServerConnecting(false);
    pages->setCurrentIndex(0);
    if (FROM_DASHBOARD)
        resize(loginWindowSize);
    setWindowTitle(tr("IoT Control — 서버 연결"));
    accessPanel->ResetPassword();
    accessPanel->SetLoginEnabled(false);
    accessPanel->ClearLoginFeedback();
    accessPanel->ClearServerFeedback();
    accessPanel->ShowServerPage(ANIMATE);
}

void MainWindow::ShowLogin()
{
    if (serverFanQuery->IsApplying() || serverDhtQuery->IsRefreshing() || serverBluetooth->IsBusy())
    {
        dashboard->SetDataFeedback(tr("장치 응답을 기다리고 있습니다. 완료 후 로그인 화면으로 이동할 수 있습니다."));
        return;
    }
    if (!serverConnection->IsConnected())
    {
        ShowServerConnection();
        return;
    }
    authenticated = false;
    backgroundDhtQuery = false;
    dhtPollTimer->stop();
    sessionPollTimer->stop();
    serverDhtQuery->PauseQuery();
    serverFanQuery->PauseQuery();
    serverSessionQuery->PauseQuery();
    dashboard->SetFanUpdateMode(false);
    dashboard->SetDhtLoading(false);
    const bool FROM_DASHBOARD = pages->currentWidget() == dashboard;
    const bool ANIMATE = !FROM_DASHBOARD && accessPanel->IsAccessVisible() && !isMinimized();
    pages->setCurrentIndex(0);
    if (FROM_DASHBOARD)
        resize(loginWindowSize);
    setWindowTitle(tr("IoT Control — 로그인"));
    accessPanel->ResetPassword();
    accessPanel->ClearLoginFeedback();
    accessPanel->ShowLoginPage(ANIMATE);
}
