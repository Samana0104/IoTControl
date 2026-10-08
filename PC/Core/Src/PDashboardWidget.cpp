#include "PDashboardWidget.h"

#include "IoTPacket.h"
#include "PDashboardPanel.h"

#include <QList>
#include <QMap>
#include <QStringList>

namespace
{
constexpr int MIN_FAN_PERCENT = 25;
} // namespace

DashboardWidget::DashboardWidget(QWidget *parent) : QWidget(parent), panel(new DashboardPanel(this))
{
    connect(panel, &DashboardPanel::ReturnToLogin, this, &DashboardWidget::ReturnToLogin);
    connect(panel, &DashboardPanel::ServerChangeRequested, this, &DashboardWidget::ServerChangeRequested);
    connect(panel, &DashboardPanel::ServerDisconnectRequested, this, &DashboardWidget::ServerDisconnectRequested);
    connect(panel, &DashboardPanel::BluetoothRequested, this, &DashboardWidget::ShowBluetoothDialog);
    connect(panel, &DashboardPanel::TargetChanged, this, &DashboardWidget::SetTargetPreview);
    connect(panel, &DashboardPanel::ApplyFanRequested, this, &DashboardWidget::ApplyFanPreview);
    connect(panel, &DashboardPanel::ReloadDbRequested, this, &DashboardWidget::RequestDhtReload);
    connect(panel, &DashboardPanel::UpdateAllRequested, this, &DashboardWidget::RequestAllUpdate);
    connect(panel, &DashboardPanel::ClientUpdateRequested, this, &DashboardWidget::RequestClientUpdate);
    PopulateSampleClients();
}

DashboardWidget::~DashboardWidget() { delete panel; }

void DashboardWidget::SetServerEndpoint(const QString &host, int port) { panel->SetServerEndpoint(host, port); }

void DashboardWidget::PopulateSampleClients()
{
    const QList<QStringList> ROWS = {{QStringLiteral("STM32-01"), QStringLiteral("STM32"), QStringLiteral("26.4 °C"), QStringLiteral("58.2 %"), QStringLiteral("09:10 · 샘플"), tr("접속 · BT · 샘플")}, {QStringLiteral("ARDUINO-01"), QStringLiteral("Arduino"), QStringLiteral("25.8 °C"), QStringLiteral("61.0 %"), QStringLiteral("09:10 · 샘플"), tr("접속 · TCP · 샘플")}};
    panel->SetClients(ROWS);
    panel->DisplayCurrentDht(QStringLiteral("26.4"), QStringLiteral("58.2"), tr("STM32-01 · 샘플"));
}

void DashboardWidget::DisplayDhtRecords(const DhtRecords &records, bool showFeedback)
{
    dhtRecords = records;
    DisplayClientRecords();
    if (records.isEmpty())
        panel->DisplayCurrentDht(QStringLiteral("—"), QStringLiteral("—"), tr("DHT 데이터 없음"));
    else
    {
        // 상단 현재 온도·습도는 클라이언트 목록 첫 번째 기기의 최신 측정값입니다.
        const DhtRecord &RECORD = records.first();
        panel->DisplayCurrentDht(QString::number(RECORD.temp, 'f', 1), QString::number(RECORD.humi, 'f', 1), tr("1번 · %1").arg(RECORD.id));
    }
    if (showFeedback)
        panel->SetFeedback(tr("dht 전체 조회 완료: %1건").arg(records.size()));
}

void DashboardWidget::DisplaySessionRecords(const SessionRecords &records)
{
    sessionRecords = records;
    sessionStatusKnown = true;
    sessionError.clear();
    DisplayClientRecords();
}

void DashboardWidget::SetSessionError(const QString &message)
{
    sessionStatusKnown = false;
    sessionError = message;
    DisplayClientRecords();
}

void DashboardWidget::ResetSessionStatus()
{
    sessionRecords.clear();
    sessionStatusKnown = false;
    sessionError.clear();
}

void DashboardWidget::DisplayClientRecords()
{
    QMap<QString, QStringList> clients;
    QStringList clientOrder;
    const QString UNKNOWN_STATUS = sessionError.isEmpty() ? tr("확인 중") : tr("확인 실패");
    for (const DhtRecord &RECORD : dhtRecords)
    {
        if (!clients.contains(RECORD.id))
            clientOrder.append(RECORD.id);
        const QString STATUS = RECORD.memberType == QStringLiteral("PC") ? QStringLiteral("—") : sessionStatusKnown ? tr("미접속")
                                                                                                                    : UNKNOWN_STATUS;
        clients.insert(RECORD.id, {RECORD.id, RECORD.memberType.isEmpty() ? QStringLiteral("—") : RECORD.memberType, tr("%1 °C").arg(RECORD.temp, 0, 'f', 1), tr("%1 %").arg(RECORD.humi, 0, 'f', 1), RECORD.updatedAt.isValid() ? RECORD.updatedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QStringLiteral("—"), STATUS});
    }
    for (const SessionRecord &RECORD : sessionRecords)
    {
        if (!clients.contains(RECORD.id))
        {
            clientOrder.append(RECORD.id);
            clients.insert(RECORD.id, {RECORD.id, RECORD.memberType, QStringLiteral("—"), QStringLiteral("—"), QStringLiteral("—"), UNKNOWN_STATUS});
        }
        QStringList &row = clients[RECORD.id];
        row[1] = RECORD.memberType;
        if (sessionStatusKnown)
        {
            const QString LINK = RECORD.links == (SESSION_LINK_TCP | SESSION_LINK_BT) ? QStringLiteral("TCP/BT") : (RECORD.links & SESSION_LINK_BT) != 0 ? QStringLiteral("BT")
                                                                                                                                                         : QStringLiteral("TCP");
            row[5] = tr("접속 · %1").arg(LINK);
        }
        else
            row[5] = UNKNOWN_STATUS;
    }
    QList<QStringList> rows;
    for (const QString &ID : clientOrder)
        rows.append(clients.value(ID));
    // 측정값이 있는 기존 1번 클라이언트를 유지하고, 기록 없는 접속 기기는 뒤에 추가합니다.
    panel->DisplayDhtClients(rows);
    panel->DisplaySessionSummary(sessionStatusKnown ? sessionRecords.size() : -1, dhtRecords.size(), sessionError);
    QStringList fanIds;
    if (sessionStatusKnown)
        for (const SessionRecord &RECORD : sessionRecords)
            if (RECORD.memberType == QStringLiteral("STM32") && (RECORD.links & SESSION_LINK_BT) != 0)
                fanIds.append(RECORD.id);
    panel->SetFanTargets(fanIds);
}

void DashboardWidget::SetDataFeedback(const QString &message) { panel->SetFeedback(message); }

void DashboardWidget::SetClientRefreshing(const QString &clientId) { panel->SetClientRefreshing(clientId); }

void DashboardWidget::SetDhtLoading(bool loading, bool collecting) { panel->SetDhtLoading(loading, collecting); }

void DashboardWidget::DisplayFanSpeed(int percent) { panel->DisplayFanSpeed(percent); }

void DashboardWidget::DisplayFanSaved(int percent) { panel->DisplayFanSaved(percent); }

QString DashboardWidget::ReadFanClientId() const { return panel->ReadFanClientId(); }

void DashboardWidget::DisplayFanApplied(const QString &clientId, int percent) { panel->DisplayFanApplied(clientId, percent); }

void DashboardWidget::SetFanApplyError(const QString &message) { panel->SetFanApplyError(message); }

void DashboardWidget::SetFanLoading() { panel->SetFanStatus(tr("팬 속도 조회 중…")); }

void DashboardWidget::SetFanError(const QString &message) { panel->SetFanStatus(tr("팬 조회 실패"), message); }

void DashboardWidget::SetFanUpdateMode(bool enabled)
{
    fanUpdateMode = enabled;
    panel->SetFanUpdateMode(enabled);
}

void DashboardWidget::SetFanUpdateBusy(bool busy, const QString &stage) { panel->SetFanUpdateBusy(busy, stage); }

void DashboardWidget::ResetDhtView()
{
    panel->SetClientRefreshing(QString());
    dhtRecords.clear();
    sessionRecords.clear();
    sessionStatusKnown = false;
    sessionError.clear();
    SetFanUpdateMode(false);
    panel->SetFanTargets({});
    PopulateSampleClients();
    panel->ResetDhtLabels();
    panel->DisplayFanSpeed(65, true);
    panel->SetFeedback(QString());
}

void DashboardWidget::SetTargetPreview(int percent) { panel->DisplayTarget(percent, percent < MIN_FAN_PERCENT ? 0 : percent); }

void DashboardWidget::ApplyFanPreview()
{
    if (fanUpdateMode)
    {
        emit FanUpdateRequested(panel->ReadTarget());
        return;
    }
    const int PERCENT = panel->ReadTarget() < MIN_FAN_PERCENT ? 0 : panel->ReadTarget();
    panel->SetTarget(PERCENT);
    panel->SetFeedback(tr("미리보기 목표 팬 속도: %1%. 실제 장치에는 명령을 전송하지 않습니다.").arg(PERCENT));
}

void DashboardWidget::RequestDhtReload() { emit ReloadDhtRequested(); }

void DashboardWidget::RequestAllUpdate() { emit FieldDataUpdateRequested(QString()); }

void DashboardWidget::RequestClientUpdate(const QString &clientId) { emit FieldDataUpdateRequested(clientId); }

void DashboardWidget::ShowBluetoothDialog() { emit BluetoothManageRequested(); }
