#include "Pmainwindow.h"

#include "Paccesspanel.h"
#include "Pdashboardwidget.h"
#include "Pserverconnection.h"
#include "Pserverdhtquery.h"

#include <QTimer>

namespace
{
constexpr int DHT_POLL_INTERVAL_MS = 5000;
} // namespace

void MainWindow::InitializeDhtControls()
{
    dhtPollTimer->setObjectName(QStringLiteral("dhtPollTimer"));
    dhtPollTimer->setInterval(DHT_POLL_INTERVAL_MS);
    connect(dhtPollTimer, &QTimer::timeout, this, &MainWindow::LoadAllDht);
    connect(dashboard, &DashboardWidget::ReloadDhtRequested, this, &MainWindow::LoadAllDht);
    connect(dashboard, &DashboardWidget::FieldDataUpdateRequested, this, &MainWindow::RequestFieldDataUpdate);
    connect(serverDhtQuery, &ServerDhtQuery::DhtLoaded, this, &MainWindow::HandleDhtLoaded);
    connect(serverDhtQuery, &ServerDhtQuery::QueryFailed, this, &MainWindow::HandleDhtQueryFailed);
    connect(serverDhtQuery, &ServerDhtQuery::CollectFinished, this, &MainWindow::HandleFieldDataUpdateResult);
}

void MainWindow::RequestFieldDataUpdate(const QString &clientId)
{
    if (!clientId.isEmpty())
    {
        // TODO: 현재 REQ_DHT_COLLECT에는 대상 ID가 없습니다. 개별 갱신은 별도 규격 필요.
        dashboard->SetDataFeedback(tr("%1 개별 갱신은 준비 중입니다. 현재 서버는 전체 기기 갱신 요청을 지원합니다.").arg(clientId));
        return;
    }
    if (!authenticated || !serverConnection->IsConnected())
    {
        dashboard->SetDataFeedback(tr("현장 갱신 요청은 서버에 로그인한 후 사용할 수 있습니다."));
        return;
    }
    if (serverDhtQuery->IsBusy())
        return;
    dashboard->SetDhtLoading(true, true);
    dashboard->SetDataFeedback(tr("서버에 전체 기기 DHT 갱신을 요청하고 있습니다…"));
    serverDhtQuery->RequestDhtCollect();
}

void MainWindow::HandleFieldDataUpdateResult(bool requested)
{
    dashboard->SetDhtLoading(false);
    dashboard->SetDataFeedback(requested ? tr("서버가 기기들에 측정 요청을 보냈습니다. 측정·저장 결과는 5초 주기의 DB 조회로 확인합니다.") : tr("측정 요청을 보낼 기기가 없거나 요청 전송에 실패했습니다. 기기의 접속 상태를 확인해 주세요."));
}

void MainWindow::LoadAllDht()
{
    if (!authenticated || !serverConnection->IsConnected())
    {
        dashboard->SetDataFeedback(tr("미리보기: 실제 DB 조회는 서버에 로그인한 후 사용할 수 있습니다."));
        return;
    }
    if (serverDhtQuery->IsBusy())
        return;
    dashboard->SetDhtLoading(true);
    dashboard->SetDataFeedback(tr("서버에서 dht 전체 데이터를 조회하고 있습니다…"));
    serverDhtQuery->LoadAllDht();
}

void MainWindow::HandleDhtLoaded(const DhtRecords &records)
{
    dashboard->SetDhtLoading(false);
    dashboard->DisplayDhtRecords(records);
}

void MainWindow::HandleDhtQueryFailed(const QString &message)
{
    dashboard->SetDhtLoading(false);
    if (!serverConnection->IsConnected())
    {
        ShowServerConnection();
        accessPanel->SetServerFeedback(message);
    }
    else
        dashboard->SetDataFeedback(message);
}
