#include "Pdashboardwidget.h"

#include "Pbluetoothdialog.h"
#include "Pdashboardpanel.h"

#include <QList>
#include <QStringList>

namespace
{
constexpr int MIN_FAN_PERCENT = 25;
} // namespace

DashboardWidget::DashboardWidget(QWidget *parent)
    : QWidget(parent), panel(new DashboardPanel(this))
{
    connect(panel, &DashboardPanel::ReturnToLogin, this,
            &DashboardWidget::ReturnToLogin);
    connect(panel, &DashboardPanel::ServerChangeRequested, this,
            &DashboardWidget::ServerChangeRequested);
    connect(panel, &DashboardPanel::ServerDisconnectRequested, this,
            &DashboardWidget::ServerDisconnectRequested);
    connect(panel, &DashboardPanel::BluetoothRequested, this,
            &DashboardWidget::ShowBluetoothDialog);
    connect(panel, &DashboardPanel::TargetChanged, this,
            &DashboardWidget::SetTargetPreview);
    connect(panel, &DashboardPanel::ApplyFanRequested, this,
            &DashboardWidget::ApplyFanPreview);
    connect(panel, &DashboardPanel::ReloadDbRequested, this,
            &DashboardWidget::ReloadDbPreview);
    connect(panel, &DashboardPanel::UpdateAllRequested, this,
            &DashboardWidget::RequestAllPreview);
    connect(panel, &DashboardPanel::ClientUpdateRequested, this,
            &DashboardWidget::RequestClientPreview);
    PopulateSampleClients();
}

DashboardWidget::~DashboardWidget() { delete panel; }

void DashboardWidget::SetServerEndpoint(const QString &host, int port)
{
    panel->SetServerEndpoint(host, port);
}

void DashboardWidget::PopulateSampleClients()
{
    const QList<QStringList> ROWS = {
        {QStringLiteral("STM32-01"), QStringLiteral("STM32"),
         QStringLiteral("26.4 °C"), QStringLiteral("58.2 %"),
         QStringLiteral("09:10 · 샘플")},
        {QStringLiteral("ARDUINO-01"), QStringLiteral("Arduino"),
         QStringLiteral("25.8 °C"), QStringLiteral("61.0 %"),
         QStringLiteral("09:10 · 샘플")}};
    panel->SetClients(ROWS);
}

void DashboardWidget::SetTargetPreview(int percent)
{
    panel->DisplayTarget(percent, percent < MIN_FAN_PERCENT ? 0 : percent);
}

void DashboardWidget::ApplyFanPreview()
{
    const int PERCENT =
        panel->ReadTarget() < MIN_FAN_PERCENT ? 0 : panel->ReadTarget();
    panel->SetTarget(PERCENT);
    panel->SetFeedback(
        tr("미리보기 목표 팬 속도: %1%. DB 측정값은 샘플 65%로 유지되며 실제 "
           "장치에는 명령을 전송하지 않습니다.")
            .arg(PERCENT));
}

void DashboardWidget::ReloadDbPreview()
{
    panel->SetFeedback(
        tr("미리보기: DB 조회 버튼입니다. 현재 화면은 고정된 샘플 "
           "데이터를 표시합니다."));
}

void DashboardWidget::RequestAllPreview()
{
    panel->SetFeedback(
        tr("미리보기: 모든 현장 클라이언트에 즉시 데이터 갱신을 "
           "요청하는 버튼입니다. 실제 요청은 전송되지 않습니다."));
}

void DashboardWidget::RequestClientPreview(const QString &clientId)
{
    panel->SetFeedback(tr("미리보기: %1 현장 클라이언트에 즉시 데이터 갱신을 "
                          "요청하는 버튼입니다. 실제 요청은 전송되지 않습니다.")
                           .arg(clientId));
}

void DashboardWidget::ShowBluetoothDialog()
{
    BluetoothDialog dialog(this);
    connect(&dialog, &BluetoothDialog::PreviewRequested, this,
            [this](const QString &id, const QString & /*password*/)
            {
                panel->SetFeedback(
                    tr("미리보기: %1 계정으로 블루투스 연결 요청 입력을 "
                       "확인했습니다. 실제 요청은 전송되지 않았습니다.")
                        .arg(id));
            });
    dialog.exec();
}
