#include "PMainWindow.h"

#include "PDashboardWidget.h"
#include "PServerConnection.h"
#include "PServerSessionQuery.h"

#include <QTimer>

namespace
{
constexpr int SESSION_POLL_INTERVAL_MS = 5000;
} // namespace

void MainWindow::InitializeSessionControls()
{
    sessionPollTimer->setObjectName(QStringLiteral("sessionPollTimer"));
    sessionPollTimer->setInterval(SESSION_POLL_INTERVAL_MS);
    connect(sessionPollTimer, &QTimer::timeout, this, &MainWindow::LoadSessionStatus);
    connect(dashboard, &DashboardWidget::ReloadDhtRequested, this, &MainWindow::LoadSessionStatus);
    connect(serverSessionQuery, &ServerSessionQuery::SessionsLoaded, this, &MainWindow::HandleSessionsLoaded);
    connect(serverSessionQuery, &ServerSessionQuery::QueryFailed, this, &MainWindow::HandleSessionQueryFailed);
}

void MainWindow::LoadSessionStatus()
{
    if (!authenticated || !serverConnection->IsConnected() || serverSessionQuery->IsLoading())
        return;
    serverSessionQuery->LoadSessions();
}

void MainWindow::HandleSessionsLoaded(const SessionRecords &records) { dashboard->DisplaySessionRecords(records); }

void MainWindow::HandleSessionQueryFailed(const QString &message)
{
    dashboard->SetSessionError(message);
    if (!serverConnection->IsConnected())
        ShowServerFailure(message);
}
