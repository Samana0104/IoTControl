#include "PMainWindow.h"

#include "PAccessPanel.h"
#include "PDashboardWidget.h"
#include "PServerConnection.h"
#include "PServerFanQuery.h"

void MainWindow::InitializeFanControls()
{
    connect(serverFanQuery, &ServerFanQuery::FanLoaded, this, &MainWindow::HandleFanLoaded);
    connect(serverFanQuery, &ServerFanQuery::QueryFailed, this, &MainWindow::HandleFanQueryFailed);
    connect(dashboard, &DashboardWidget::FanUpdateRequested, this, &MainWindow::UpdateFanSpeed);
    connect(serverFanQuery, &ServerFanQuery::FanUpdated, this, &MainWindow::HandleFanUpdated);
    connect(serverFanQuery, &ServerFanQuery::UpdateFailed, this, &MainWindow::HandleFanUpdateFailed);
}

void MainWindow::LoadFanSpeed()
{
    if (!authenticated || !serverConnection->IsConnected() || serverFanQuery->IsLoading())
        return;
    dashboard->SetFanUpdateBusy(true);
    dashboard->SetFanLoading();
    serverFanQuery->LoadFanSpeed();
}

void MainWindow::HandleFanLoaded(int percent)
{
    dashboard->SetFanUpdateBusy(false);
    dashboard->DisplayFanSpeed(percent);
}

void MainWindow::UpdateFanSpeed(int percent)
{
    if (!authenticated || !serverConnection->IsConnected() || serverFanQuery->IsLoading())
        return;
    dashboard->SetFanUpdateBusy(true);
    dashboard->SetDataFeedback(tr("목표 팬 속도 %1%를 서버 DB에 저장 중입니다…").arg(percent));
    serverFanQuery->UpdateFanSpeed(percent);
}

void MainWindow::HandleFanUpdated(int percent)
{
    dashboard->SetFanUpdateBusy(false);
    dashboard->DisplayFanSaved(percent);
    dashboard->SetDataFeedback(tr("팬 DB 저장 완료: %1%. 실제 팬 제어는 아직 요청하지 않았습니다.").arg(percent));
}

void MainWindow::HandleFanUpdateFailed(const QString &message)
{
    dashboard->SetFanUpdateBusy(false);
    if (!serverConnection->IsConnected())
    {
        ShowServerFailure(message);
    }
    else
        dashboard->SetDataFeedback(message);
}

void MainWindow::HandleFanQueryFailed(const QString &message)
{
    dashboard->SetFanUpdateBusy(false);
    if (!serverConnection->IsConnected())
    {
        ShowServerFailure(message);
    }
    else
        dashboard->SetFanError(message);
}
