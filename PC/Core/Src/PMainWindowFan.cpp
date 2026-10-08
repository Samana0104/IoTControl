#include "PMainWindow.h"

#include "PAccessPanel.h"
#include "PDashboardWidget.h"
#include "PServerConnection.h"
#include "PServerFanQuery.h"

void MainWindow::InitializeFanControls()
{
    connect(serverFanQuery, &ServerFanQuery::FanLoaded, this, &MainWindow::HandleFanLoaded);
    connect(serverFanQuery, &ServerFanQuery::QueryFailed, this, &MainWindow::HandleFanQueryFailed);
}

void MainWindow::LoadFanSpeed()
{
    if (!authenticated || !serverConnection->IsConnected() || serverFanQuery->IsLoading())
        return;
    dashboard->SetFanLoading();
    serverFanQuery->LoadFanSpeed();
}

void MainWindow::HandleFanLoaded(int percent) { dashboard->DisplayFanSpeed(percent); }

void MainWindow::HandleFanQueryFailed(const QString &message)
{
    if (!serverConnection->IsConnected())
    {
        ShowServerConnection();
        accessPanel->SetServerFeedback(message);
    }
    else
        dashboard->SetFanError(message);
}
