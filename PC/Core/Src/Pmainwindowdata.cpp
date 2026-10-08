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
}

void MainWindow::RequestFieldDataUpdate(const QString &clientId)
{
    const QString TARGET = clientId.isEmpty() ? tr("모든 기기") : clientId;
    // TODO: 서버 측 현장 DHT 갱신 요청 및 완료 응답 규격 확정 후 실제 송수신 연결.
    dashboard->SetDataFeedback(tr("%1 현장 갱신: 준비 중입니다. 서버 통신 규격 확정 후 연결합니다. DB 새로고침은 서버에 저장된 값을 조회합니다.").arg(TARGET));
}

void MainWindow::LoadAllDht()
{
    if (!authenticated || !serverConnection->IsConnected())
    {
        dashboard->SetDataFeedback(tr("미리보기: 실제 DB 조회는 서버에 로그인한 후 사용할 수 있습니다."));
        return;
    }
    if (serverDhtQuery->IsLoading())
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
