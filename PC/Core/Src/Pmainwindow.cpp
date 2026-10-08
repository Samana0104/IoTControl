#include "Pmainwindow.h"
#include "Paccesspanel.h"
#include "Pdashboardwidget.h"

#include <QStackedWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), accessPanel(new AccessPanel(this))
{
    pages = new QStackedWidget(this);
    pages->setObjectName(QStringLiteral("pageStack"));
    pages->addWidget(takeCentralWidget());
    dashboard = new DashboardWidget(pages);
    pages->addWidget(dashboard);
    setCentralWidget(pages);
    connect(accessPanel, &AccessPanel::DashboardPreviewRequested, this,
            &MainWindow::ShowDashboard);
    connect(dashboard, &DashboardWidget::ReturnToLogin, this,
            &MainWindow::ShowLogin);
    InitializeServerControls();
    InitializeLoginControls();
    loginWindowSize = size();
    ShowServerConnection();
}

MainWindow::~MainWindow() { delete accessPanel; }

void MainWindow::ShowDashboard()
{
    if (!serverPreviewReady)
    {
        ShowServerConnection();
        accessPanel->SetServerFeedback(
            tr("먼저 서버 주소와 포트를 확인해 주세요."));
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
    const bool FROM_DASHBOARD = pages->currentWidget() == dashboard;
    const bool ANIMATE =
        !FROM_DASHBOARD && accessPanel->IsAccessVisible() && !isMinimized();
    serverPreviewReady = false;
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
    if (!serverPreviewReady)
    {
        ShowServerConnection();
        return;
    }
    const bool FROM_DASHBOARD = pages->currentWidget() == dashboard;
    const bool ANIMATE =
        !FROM_DASHBOARD && accessPanel->IsAccessVisible() && !isMinimized();
    pages->setCurrentIndex(0);
    if (FROM_DASHBOARD)
        resize(loginWindowSize);
    setWindowTitle(tr("IoT Control — 로그인"));
    accessPanel->ResetPassword();
    accessPanel->ClearLoginFeedback();
    accessPanel->ShowLoginPage(ANIMATE);
}
