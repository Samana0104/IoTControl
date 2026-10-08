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
    connect(serverFanQuery, &ServerFanQuery::FanApplied, this, &MainWindow::HandleFanApplied);
    connect(serverFanQuery, &ServerFanQuery::ApplyFailed, this, &MainWindow::HandleFanApplyFailed);
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
    pendingFanClientId = dashboard->ReadFanClientId();
    dashboard->SetDataFeedback(tr("목표 팬 속도 %1%를 서버 DB에 저장 중입니다…").arg(percent));
    serverFanQuery->UpdateFanSpeed(percent);
}

void MainWindow::HandleFanUpdated(int percent)
{
    dashboard->DisplayFanSaved(percent);
    if (pendingFanClientId.isEmpty())
    {
        dashboard->SetFanUpdateBusy(false);
        dashboard->SetDataFeedback(tr("팬 DB 저장 완료: %1%. 접속한 STM32(BT)를 선택하면 저장 후 장치에도 적용합니다.").arg(percent));
        return;
    }
    dashboard->SetFanUpdateBusy(true, tr("STM32 적용 응답 대기 중…"));
    dashboard->SetDataFeedback(tr("DB 저장 완료: %1%. %2의 팬 적용 응답을 기다립니다…").arg(percent).arg(pendingFanClientId));
    serverFanQuery->ApplySavedSpeed(pendingFanClientId, percent);
}

void MainWindow::HandleFanApplied(const QString &clientId, int percent)
{
    dashboard->SetFanUpdateBusy(false);
    dashboard->DisplayFanApplied(clientId, percent);
    dashboard->SetDataFeedback(tr("팬 DB 저장 및 장치 적용 완료: %1 · %2%. STM32 응답을 확인했습니다.").arg(clientId).arg(percent));
}

void MainWindow::HandleFanApplyFailed(const QString &clientId, const QString &message)
{
    dashboard->SetFanUpdateBusy(false);
    const QString DETAIL = tr("DB 저장은 완료됐지만 %1의 장치 적용을 확인하지 못했습니다. %2").arg(clientId, message);
    dashboard->SetFanApplyError(DETAIL);
    dashboard->SetDataFeedback(DETAIL);
    if (!serverConnection->IsConnected())
        ShowServerFailure(DETAIL);
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
