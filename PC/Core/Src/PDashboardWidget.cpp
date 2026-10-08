#include "PDashboardWidget.h"

#include "PBluetoothDialog.h"
#include "PDashboardPanel.h"

#include <QList>
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
    const QList<QStringList> ROWS = {{QStringLiteral("STM32-01"), QStringLiteral("STM32"), QStringLiteral("26.4 °C"), QStringLiteral("58.2 %"), QStringLiteral("09:10 · 샘플")}, {QStringLiteral("ARDUINO-01"), QStringLiteral("Arduino"), QStringLiteral("25.8 °C"), QStringLiteral("61.0 %"), QStringLiteral("09:10 · 샘플")}};
    panel->SetClients(ROWS);
    panel->DisplayCurrentDht(QStringLiteral("26.4"), QStringLiteral("58.2"), tr("STM32-01 · 샘플"));
}

void DashboardWidget::DisplayDhtRecords(const DhtRecords &records, bool showFeedback)
{
    QList<QStringList> rows;
    rows.reserve(records.size());
    for (const DhtRecord &RECORD : records)
    {
        rows.append({RECORD.id, RECORD.memberType.isEmpty() ? QStringLiteral("—") : RECORD.memberType, tr("%1 °C").arg(RECORD.temp, 0, 'f', 1), tr("%1 %").arg(RECORD.humi, 0, 'f', 1), RECORD.updatedAt.isValid() ? RECORD.updatedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QStringLiteral("—")});
    }
    panel->DisplayDhtClients(rows);
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

void DashboardWidget::SetDataFeedback(const QString &message) { panel->SetFeedback(message); }

void DashboardWidget::SetDhtLoading(bool loading, bool collecting) { panel->SetDhtLoading(loading, collecting); }

void DashboardWidget::DisplayFanSpeed(int percent) { panel->DisplayFanSpeed(percent); }

void DashboardWidget::DisplayFanSaved(int percent) { panel->DisplayFanSaved(percent); }

void DashboardWidget::SetFanLoading() { panel->SetFanStatus(tr("팬 속도 조회 중…")); }

void DashboardWidget::SetFanError(const QString &message) { panel->SetFanStatus(tr("팬 조회 실패"), message); }

void DashboardWidget::SetFanUpdateMode(bool enabled)
{
    fanUpdateMode = enabled;
    panel->SetFanUpdateMode(enabled);
}

void DashboardWidget::SetFanUpdateBusy(bool busy) { panel->SetFanUpdateBusy(busy); }

void DashboardWidget::ResetDhtView()
{
    SetFanUpdateMode(false);
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

void DashboardWidget::ShowBluetoothDialog()
{
    BluetoothDialog dialog(this);
    connect(&dialog, &BluetoothDialog::PreviewRequested, this,
            [this](const QString &id, const QString & /*password*/)
            {
                panel->SetFeedback(tr("미리보기: %1 계정으로 블루투스 연결 요청 입력을 "
                                      "확인했습니다. 실제 요청은 전송되지 않았습니다.")
                                       .arg(id));
            });
    dialog.exec();
}
