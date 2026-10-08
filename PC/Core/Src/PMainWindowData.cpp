#include "PMainWindow.h"

#include "PAccessPanel.h"
#include "PDashboardWidget.h"
#include "PServerConnection.h"
#include "PServerDhtQuery.h"

#include <QTimer>

namespace
{
constexpr int DHT_POLL_INTERVAL_MS = 5000;
} // namespace

void MainWindow::InitializeDhtControls()
{
    dhtPollTimer->setObjectName(QStringLiteral("dhtPollTimer"));
    dhtPollTimer->setInterval(DHT_POLL_INTERVAL_MS);
    connect(dhtPollTimer, &QTimer::timeout, this, [this]
            { LoadAllDht(true); });
    connect(dashboard, &DashboardWidget::ReloadDhtRequested, this, [this]
            { LoadAllDht(); });
    connect(dashboard, &DashboardWidget::FieldDataUpdateRequested, this, &MainWindow::RequestFieldDataUpdate);
    connect(serverDhtQuery, &ServerDhtQuery::DhtLoaded, this, &MainWindow::HandleDhtLoaded);
    connect(serverDhtQuery, &ServerDhtQuery::QueryFailed, this, &MainWindow::HandleDhtQueryFailed);
    connect(serverDhtQuery, &ServerDhtQuery::RefreshFinished, this,
            [this](const QString &clientId)
            {
                dashboard->SetClientRefreshing(QString());
                dashboard->SetDhtLoading(false);
                dashboard->SetDataFeedback(tr("%1 측정·DB 저장 완료. 최신 데이터를 조회합니다.").arg(clientId));
                QTimer::singleShot(0, this, [this]
                                   { LoadAllDht(true); });
            });
    connect(serverDhtQuery, &ServerDhtQuery::RefreshFailed, this,
            [this](const QString &clientId, const QString &message)
            {
                dashboard->SetClientRefreshing(QString());
                dashboard->SetDhtLoading(false);
                if (serverConnection->IsConnected())
                    dashboard->SetDataFeedback(tr("%1 갱신 실패: %2").arg(clientId, message));
                else
                    ShowServerFailure(message);
            });
    connect(serverDhtQuery, &ServerDhtQuery::CollectFinished, this, &MainWindow::HandleFieldDataUpdateResult);
}

void MainWindow::RequestFieldDataUpdate(const QString &clientId)
{
    if (!authenticated || !serverConnection->IsConnected())
    {
        dashboard->SetDataFeedback(tr("현장 갱신 요청은 서버에 로그인한 후 사용할 수 있습니다."));
        return;
    }
    if (serverDhtQuery->IsBusy())
        return;
    if (!clientId.isEmpty())
    {
        dashboard->SetClientRefreshing(clientId);
        dashboard->SetDhtLoading(true, true);
        dashboard->SetDataFeedback(tr("%1 새 측정 및 DB 저장 응답을 기다립니다…").arg(clientId));
        serverDhtQuery->RequestClientRefresh(clientId);
        return;
    }
    dashboard->SetDhtLoading(true, true);
    dashboard->SetDataFeedback(tr("서버에 전체 기기 DHT 갱신을 요청하고 있습니다…"));
    serverDhtQuery->RequestDhtCollect();
}

void MainWindow::HandleFieldDataUpdateResult(bool requested)
{
    dashboard->SetDhtLoading(false);
    dashboard->SetDataFeedback(requested ? tr("서버가 기기들에 측정 요청을 보냈습니다. 측정·저장 결과는 5초 주기의 DB 조회로 확인합니다.") : tr("측정 요청을 보낼 기기가 없거나 요청 전송에 실패했습니다. 기기의 접속 상태를 확인해 주세요."));
}

void MainWindow::LoadAllDht(bool background)
{
    if (!authenticated || !serverConnection->IsConnected())
    {
        if (!background)
            dashboard->SetDataFeedback(tr("미리보기: 실제 DB 조회는 서버에 로그인한 후 사용할 수 있습니다."));
        return;
    }
    if (serverDhtQuery->IsBusy() && (background || !backgroundDhtQuery || !serverDhtQuery->IsLoading()))
        return;
    // 자동 조회 중 수동 새로고침을 누르면 요청을 중복 전송하지 않고 같은 응답을 기다립니다.
    const bool REUSE_QUERY = serverDhtQuery->IsLoading();
    backgroundDhtQuery = background;
    if (!background)
    {
        dashboard->SetDhtLoading(true);
        dashboard->SetDataFeedback(tr("서버에서 dht 전체 데이터를 조회하고 있습니다…"));
    }
    if (!REUSE_QUERY)
        serverDhtQuery->LoadAllDht();
}

void MainWindow::HandleDhtLoaded(const DhtRecords &records)
{
    if (!backgroundDhtQuery)
        dashboard->SetDhtLoading(false);
    dashboard->DisplayDhtRecords(records, !backgroundDhtQuery);
    backgroundDhtQuery = false;
}

void MainWindow::HandleDhtQueryFailed(const QString &message)
{
    if (!backgroundDhtQuery)
        dashboard->SetDhtLoading(false);
    backgroundDhtQuery = false;
    if (!serverConnection->IsConnected())
    {
        ShowServerFailure(message);
    }
    else
        dashboard->SetDataFeedback(message);
}
