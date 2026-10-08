#include "PDashboardPanel.h"
#include "PFanChartWidget.h"
#include "PFanRotorWidget.h"
#include "PGamingTheme.h"
#include "ui_PDashboard.h"

#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QHeaderView>
#include <QParallelAnimationGroup>
#include <QPauseAnimation>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QSequentialAnimationGroup>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStyle>
#include <QVariantAnimation>

namespace
{
constexpr int CLIENT_TIMESTAMP_COLUMN = 4;
constexpr int CLIENT_STATUS_COLUMN = 5;
constexpr int CLIENT_UPDATE_COLUMN = 6;

QString FormatMetricValue(const QString &finalText, qreal progress)
{
    bool numeric = false;
    const double VALUE = finalText.toDouble(&numeric);
    if (!numeric)
        return finalText;
    const int DOT = finalText.indexOf(QLatin1Char('.'));
    const int DECIMALS = DOT < 0 ? 0 : finalText.size() - DOT - 1;
    QString text = QString::number(VALUE * progress, 'f', DECIMALS);
    if (DOT < 0 && finalText.startsWith(QLatin1Char('0')))
        text = text.rightJustified(finalText.size(), QLatin1Char('0'));
    return text;
}
} // namespace

DashboardPanel::DashboardPanel(QWidget *host) : QObject(host), host(host), ui(new Ui::DashboardWidget), fanChart(new FanChartWidget(host)), fanRotor(new FanRotorWidget(host))
{
    ui->setupUi(host);
    overviewTableMaximumHeight = ui->clientsTable->maximumHeight();
    overviewChartMaximumHeight = ui->fanChartHost->maximumHeight();
    ApplyGamingPalette(host);
    new GamingBackdropWidget(ui->dashboardContent);
    fanChart->setObjectName(QStringLiteral("fanChart"));
    ui->fanChartLayout->addWidget(fanChart);
    ui->chartPanelLayout->setStretch(1, 1);
    fanRotor->setObjectName(QStringLiteral("fanRotor"));
    ui->targetValue->ensurePolished();
    const QFontMetrics TARGET_METRICS(ui->targetValue->font());
    int readoutWidth = ui->targetValue->minimumWidth();
    for (int percent = 0; percent <= 100; ++percent)
        readoutWidth = qMax(readoutWidth, TARGET_METRICS.boundingRect(tr("%1%").arg(percent)).width() + 2);
    ui->targetValue->setFixedWidth(readoutWidth);
    ui->targetValueLayout->insertWidget(2, fanRotor, 0, Qt::AlignVCenter);
    ui->fanControlPanelLayout->setAlignment(Qt::AlignTop);
    entrancePanels = {ui->temperatureCard, ui->humidityCard, ui->fanCard, ui->clientCard, ui->chartPanel, ui->fanControlPanel, ui->clientsPanel};
    metricLabels = {ui->temperatureValue, ui->humidityValue, ui->fanValue, ui->clientValue};
    for (auto *label : metricLabels)
        metricFinalText.append(label->text());
    for (auto *button : {ui->overviewNav, ui->fanNav, ui->clientsNav})
        button->setCheckable(true);
    ui->overviewNav->setAccessibleDescription(tr("모든 디바이스 데이터를 한눈에 확인하기"));
    ui->fanNav->setAccessibleDescription(tr("팬 그래프와 목표 속도 조절 화면 열기"));
    ui->clientsNav->setAccessibleDescription(tr("현장 보드 목록과 데이터 갱신 화면 열기"));

    connect(ui->returnToLoginButton, &QPushButton::clicked, this, &DashboardPanel::ReturnToLogin);
    connect(ui->dashboardChangeServerButton, &QPushButton::clicked, this, &DashboardPanel::ServerChangeRequested);
    connect(ui->dashboardDisconnectServerButton, &QPushButton::clicked, this, &DashboardPanel::ServerDisconnectRequested);
    connect(ui->sidebarBluetoothButton, &QPushButton::clicked, this, &DashboardPanel::BluetoothRequested);
    connect(ui->fanSpeedSlider, &QSlider::valueChanged, this, &DashboardPanel::TargetChanged);
    connect(fanChart, &FanChartWidget::TargetPreviewChanged, ui->fanSpeedSlider, &QSlider::setValue);
    connect(ui->applyFanButton, &QPushButton::clicked, this, &DashboardPanel::ApplyFanRequested);
    connect(ui->fanTargetInput, &QComboBox::currentIndexChanged, this, [this]
            { SetFanUpdateBusy(fanUpdateBusy); });
    connect(ui->stopFanButton, &QPushButton::clicked, this, [this]
            { SetTarget(0); });
    connect(ui->fan25Button, &QPushButton::clicked, this, [this]
            { SetTarget(25); });
    connect(ui->fan50Button, &QPushButton::clicked, this, [this]
            { SetTarget(50); });
    connect(ui->fan100Button, &QPushButton::clicked, this, [this]
            { SetTarget(100); });
    connect(ui->reloadDbButton, &QPushButton::clicked, this, &DashboardPanel::ReloadDbRequested);
    connect(ui->requestAllButton, &QPushButton::clicked, this, &DashboardPanel::UpdateAllRequested);
    connect(ui->overviewNav, &QPushButton::clicked, this, [this]
            { SelectSection(ui->overviewNav); });
    connect(ui->fanNav, &QPushButton::clicked, this, [this]
            { SelectSection(ui->fanNav); });
    connect(ui->clientsNav, &QPushButton::clicked, this, [this]
            { SelectSection(ui->clientsNav); });
    SelectSection(ui->overviewNav);
    host->installEventFilter(this);
}

DashboardPanel::~DashboardPanel()
{
    host->removeEventFilter(this);
    if (entranceAnimation)
        entranceAnimation->stop();
    FinishEntrance();
    delete ui;
}

bool DashboardPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == host && event->type() == QEvent::Show && !event->spontaneous())
        PlayEntrance();
    else if (watched == host && event->type() == QEvent::Hide)
    {
        if (entranceAnimation)
            entranceAnimation->stop();
        FinishEntrance();
    }
    return QObject::eventFilter(watched, event);
}

void DashboardPanel::SetClients(const QList<QStringList> &rows)
{
    ui->clientsTable->clearContents();
    ui->clientsTable->setRowCount(static_cast<int>(rows.size()));
    SetMetricValue(ui->clientValue, QString::number(rows.size()).rightJustified(2, QLatin1Char('0')));
    ui->clientsTable->verticalHeader()->hide();
    ui->clientsTable->verticalHeader()->setDefaultSectionSize(39);
    ui->clientsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->clientsTable->horizontalHeader()->setMinimumSectionSize(70);
    ui->clientsTable->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui->clientsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->clientsTable->horizontalHeader()->setSectionResizeMode(CLIENT_TIMESTAMP_COLUMN, QHeaderView::ResizeToContents);
    ui->clientsTable->horizontalHeader()->setSectionResizeMode(CLIENT_STATUS_COLUMN, QHeaderView::ResizeToContents);
    ui->clientsTable->horizontalHeader()->setSectionResizeMode(CLIENT_UPDATE_COLUMN, QHeaderView::Fixed);
    ui->clientsTable->setColumnWidth(CLIENT_UPDATE_COLUMN, 100);
    ui->clientsTable->setFocusPolicy(Qt::StrongFocus);
    for (int row = 0; row < rows.size(); ++row)
    {
        for (int column = 0; column < rows[row].size(); ++column)
        {
            auto *item = new QTableWidgetItem(rows[row][column]);
            item->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
            ui->clientsTable->setItem(row, column, item);
        }
        auto *requestButton = new QPushButton(tr("준비 중"), ui->clientsTable);
        requestButton->setObjectName(QStringLiteral("requestClient%1Button").arg(row));
        requestButton->setProperty("role", "rowAction");
        requestButton->setCursor(Qt::PointingHandCursor);
        requestButton->setToolTip(tr("기기별 갱신은 준비 중입니다. 전체 기기 현장 갱신 요청을 사용해 주세요."));
        requestButton->setAccessibleName(tr("%1 현장 갱신 요청 준비 중").arg(rows[row][0]));
        ui->clientsTable->setCellWidget(row, CLIENT_UPDATE_COLUMN, requestButton);
        const QString CLIENT_ID = rows[row][0];
        connect(requestButton, &QPushButton::clicked, this, [this, CLIENT_ID]
                { emit ClientUpdateRequested(CLIENT_ID); });
    }
    RefreshClientButtons();
}

void DashboardPanel::SetMetricValue(QLabel *label, const QString &text)
{
    label->setText(text);
    for (int index = 0; index < metricLabels.size(); ++index)
    {
        if (metricLabels[index] == label)
            metricFinalText[index] = text;
    }
}

void DashboardPanel::DisplayDhtClients(const QList<QStringList> &rows)
{
    if (entranceAnimation)
        entranceAnimation->stop();
    FinishEntrance();
    SetClients(rows);
    ui->clientTag->setText(tr("DHT 데이터 보유 · %1대").arg(rows.size()));
    ui->clientsSubtitle->setText(tr("서버 DB 조회 결과 · dht %1건").arg(rows.size()));
}

void DashboardPanel::DisplayCurrentDht(const QString &temperature, const QString &humidity, const QString &source)
{
    SetMetricValue(ui->temperatureValue, temperature);
    SetMetricValue(ui->humidityValue, humidity);
    ui->temperatureTag->setText(source);
    ui->humidityTag->setText(source);
}

void DashboardPanel::DisplaySessionSummary(int onlineCount, int dbCount, const QString &error)
{
    SetMetricValue(ui->clientValue, onlineCount < 0 ? QStringLiteral("—") : QString::number(onlineCount).rightJustified(2, QLatin1Char('0')));
    ui->clientTag->setText(onlineCount >= 0 ? tr("서버 세션 · %1대 접속").arg(onlineCount) : error.isEmpty() ? tr("접속 상태 확인 중…")
                                                                                                             : tr("접속 상태 확인 실패"));
    ui->clientTag->setToolTip(error);
    ui->clientsSubtitle->setText(onlineCount >= 0 ? tr("서버 DB %1대 · 현장 접속 %2대").arg(dbCount).arg(onlineCount) : tr("서버 DB %1대 · 접속 상태 미확인").arg(dbCount));
}

void DashboardPanel::SetDhtLoading(bool loading, bool collecting)
{
    dhtLoading = loading;
    RefreshClientButtons();
    ui->reloadDbButton->setEnabled(!loading);
    ui->reloadDbButton->setText(loading && !collecting ? tr("DB 조회 중…") : tr("DB 새로고침"));
    ui->requestAllButton->setEnabled(!loading);
    ui->requestAllButton->setText(collecting ? tr("요청 전송 중…") : tr("현장 갱신 요청"));
}

void DashboardPanel::ResetDhtLabels()
{
    ui->clientTag->setToolTip(QString());
    ui->clientTag->setText(tr("STM32 / Arduino · 샘플"));
    ui->clientsSubtitle->setText(tr("DB 조회 결과 · 샘플 데이터"));
}

void DashboardPanel::DisplayFanSpeed(int percent, bool sample)
{
    SetMetricValue(ui->fanValue, QString::number(percent));
    ui->fanTag->setText(sample ? tr("단일 팬 · DB 측정값 샘플") : tr("단일 팬 · 서버 DB 조회"));
    ui->fanTag->setToolTip(QString());
}

void DashboardPanel::DisplayFanSaved(int percent)
{
    DisplayFanSpeed(percent);
    ui->fanTag->setText(tr("DB 저장 · 장치 적용 대기"));
}

void DashboardPanel::SetFanTargets(const QStringList &ids)
{
    const QString PREVIOUS = ReadFanClientId();
    const QSignalBlocker BLOCKER(ui->fanTargetInput);
    ui->fanTargetInput->clear();
    ui->fanTargetInput->addItem(ids.isEmpty() ? tr("접속한 STM32(BT) 없음 · DB 저장만") : tr("팬이 연결된 STM32(BT) 선택 · DB 저장만"), QString());
    for (const QString &ID : ids)
        ui->fanTargetInput->addItem(tr("%1 · BT 접속").arg(ID), ID);
    const int INDEX = ui->fanTargetInput->findData(PREVIOUS);
    const int DEFAULT_INDEX = ids.size() == 1 ? 1 : 0;
    ui->fanTargetInput->setCurrentIndex(!PREVIOUS.isEmpty() && INDEX > 0 ? INDEX : DEFAULT_INDEX);
    SetFanUpdateBusy(fanUpdateBusy);
}

QString DashboardPanel::ReadFanClientId() const { return ui->fanTargetInput->currentData().toString(); }

void DashboardPanel::DisplayFanApplied(const QString &clientId, int percent)
{
    DisplayFanSpeed(percent);
    ui->fanTag->setText(tr("DB 저장 · %1 장치 적용 완료").arg(clientId));
    ui->fanTag->setToolTip(tr("STM32가 PWM 적용 성공으로 응답했습니다. 표시한 값은 설정 비율이며 RPM 측정값이 아닙니다."));
}

void DashboardPanel::SetFanApplyError(const QString &message)
{
    ui->fanTag->setText(tr("DB 저장 완료 · 장치 적용 미확인"));
    ui->fanTag->setToolTip(message);
}

void DashboardPanel::SetFanUpdateMode(bool enabled)
{
    fanUpdateMode = enabled;
    ui->previewBadge->setText(enabled ? tr("서버 DB 연결") : tr("UI 미리보기 · 샘플 데이터"));
    ui->sidebarPreviewNote->setText(enabled ? tr("서버 DB 자동 조회.\n팬 저장 후 선택한\nSTM32(BT)에 적용합니다.") : tr("로그인 후 DB 자동 조회.\n팬·장치 제어는\n미리보기입니다."));
    ui->fanPreviewNote->setWordWrap(true);
    ui->fanPreviewNote->setText(enabled ? tr("대상을 선택하면 DB 저장 성공 후 STM32에 적용하고 장치 응답을 확인합니다. 대상이 없으면 DB만 저장합니다.") : tr("미리보기에서만 적용됩니다."));
    ui->fanRangeNote->setText(enabled ? tr("DB 저장 범위: 0–100%") : tr("0% 정지 · 25% 미만은 정지 구간"));
    SetFanUpdateBusy(false);
}

void DashboardPanel::SetFanUpdateBusy(bool busy, const QString &stage)
{
    fanUpdateBusy = busy;
    if (!busy)
        fanBusyLabel.clear();
    else if (!stage.isEmpty())
        fanBusyLabel = stage;
    ui->applyFanButton->setEnabled(!busy);
    ui->returnToLoginButton->setEnabled((!busy || fanBusyLabel.isEmpty()) && refreshingClientId.isEmpty());
    ui->fanTargetInput->setEnabled(fanUpdateMode && !busy && ui->fanTargetInput->count() > 1);
    const QString SERVER_LABEL = ReadFanClientId().isEmpty() ? tr("팬 속도 DB 저장") : tr("DB 저장 후 팬 적용");
    const QString LABEL = fanUpdateMode ? SERVER_LABEL : tr("목표 속도 적용");
    const QString BUSY_LABEL = fanBusyLabel.isEmpty() ? tr("팬 DB 응답 대기 중…") : fanBusyLabel;
    ui->applyFanButton->setText(busy ? BUSY_LABEL : LABEL);
}

void DashboardPanel::SetFanStatus(const QString &status, const QString &detail)
{
    SetMetricValue(ui->fanValue, QStringLiteral("—"));
    ui->fanTag->setText(status);
    ui->fanTag->setToolTip(detail);
}

void DashboardPanel::SetServerEndpoint(const QString &host, int port)
{
    const QString ENDPOINT = tr("%1:%2").arg(host).arg(port);
    ui->serverEndpoint->setText(ENDPOINT);
    ui->serverEndpoint->setToolTip(ENDPOINT);
}

void DashboardPanel::PlayEntrance()
{
    if (entranceAnimation)
        entranceAnimation->stop();
    FinishEntrance();
    auto *group = new QParallelAnimationGroup(this);
    entranceAnimation = group;
    for (int index = 0; index < metricLabels.size(); ++index)
        metricLabels[index]->setText(FormatMetricValue(metricFinalText[index], 0));
    for (int index = 0; index < entrancePanels.size(); ++index)
    {
        auto *panel = entrancePanels[index];
        auto *effect = new QGraphicsOpacityEffect(panel);
        effect->setOpacity(0);
        panel->setGraphicsEffect(effect);
        auto *sequence = new QSequentialAnimationGroup(group);
        sequence->addPause(index * 45);
        auto *fade = new QPropertyAnimation(effect, "opacity", sequence);
        fade->setDuration(420);
        fade->setEasingCurve(QEasingCurve::OutCubic);
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        sequence->addAnimation(fade);
        group->addAnimation(sequence);
    }
    auto *countSequence = new QSequentialAnimationGroup(group);
    countSequence->addPause(100);
    auto *counter = new QVariantAnimation(countSequence);
    counter->setDuration(650);
    counter->setStartValue(0.0);
    counter->setEndValue(1.0);
    counter->setEasingCurve(QEasingCurve::OutCubic);
    connect(counter, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value)
            {
                for (int index = 0; index < metricLabels.size(); ++index)
                {
                    metricLabels[index]->setText(FormatMetricValue(metricFinalText[index], value.toReal()));
                }
            });
    countSequence->addAnimation(counter);
    group->addAnimation(countSequence);
    connect(group, &QParallelAnimationGroup::finished, this, &DashboardPanel::FinishEntrance);
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void DashboardPanel::FinishEntrance()
{
    for (auto *panel : entrancePanels)
        panel->setGraphicsEffect(nullptr);
    for (int index = 0; index < metricLabels.size(); ++index)
        metricLabels[index]->setText(metricFinalText[index]);
}

void DashboardPanel::SelectSection(QPushButton *navigation)
{
    if (entranceAnimation)
    {
        entranceAnimation->stop();
        FinishEntrance();
    }

    const bool IS_FAN = navigation == ui->fanNav;
    const bool IS_CLIENTS = navigation == ui->clientsNav;
    for (auto *button : {ui->overviewNav, ui->fanNav, ui->clientsNav})
    {
        button->setChecked(button == navigation);
        button->setProperty("active", button == navigation);
        button->style()->unpolish(button);
        button->style()->polish(button);
        button->update();
    }
    ui->chartPanel->setVisible(!IS_CLIENTS);
    ui->fanControlPanel->setVisible(!IS_CLIENTS);
    ui->clientsPanel->setVisible(!IS_FAN);
    ui->clientsTable->setMaximumHeight(IS_CLIENTS ? QWIDGETSIZE_MAX : overviewTableMaximumHeight);
    ui->fanChartHost->setMaximumHeight(IS_FAN ? QWIDGETSIZE_MAX : overviewChartMaximumHeight);

    if (IS_FAN)
    {
        ui->dashboardTitle->setText(QStringLiteral("FAN CONTROL"));
        ui->dashboardSubtitle->setText(tr("그래프와 슬라이더로 목표 팬 속도를 조절하세요."));
    }
    else if (IS_CLIENTS)
    {
        ui->dashboardTitle->setText(QStringLiteral("FIELD CLIENTS"));
        ui->dashboardSubtitle->setText(tr("보드별 수집 데이터를 확인하고 즉시 갱신을 요청하세요."));
    }
    else
    {
        ui->dashboardTitle->setText(QStringLiteral("CONTROL CENTER"));
        ui->dashboardSubtitle->setText(tr("센서 모니터링 · 팬 제어 · 현장 연결"));
    }

    for (int index = 0; index < ui->dashboardContentLayout->count(); ++index)
    {
        auto *item = ui->dashboardContentLayout->itemAt(index);
        const bool EXPAND = (IS_FAN && item->layout() == ui->chartAndControlLayout) || (IS_CLIENTS && item->widget() == ui->clientsPanel);
        ui->dashboardContentLayout->setStretch(index, EXPAND ? 1 : 0);
    }
    ui->dashboardContentLayout->activate();
    ui->contentScroll->verticalScrollBar()->setValue(0);
}

int DashboardPanel::ReadTarget() const { return ui->fanSpeedSlider->value(); }

void DashboardPanel::SetTarget(int percent) { ui->fanSpeedSlider->setValue(percent); }

void DashboardPanel::DisplayTarget(int percent, int rotorSpeed)
{
    ui->targetValue->setText(tr("%1%").arg(percent));
    fanChart->SetTarget(percent);
    fanRotor->SetSpeed(rotorSpeed);
}

void DashboardPanel::SetFeedback(const QString &message) { ui->dashboardFeedback->setText(message); }

void DashboardPanel::SetClientRefreshing(const QString &clientId)
{
    refreshingClientId = clientId;
    RefreshClientButtons();
    SetFanUpdateBusy(fanUpdateBusy);
}

void DashboardPanel::RefreshClientButtons()
{
    for (int row = 0; row < ui->clientsTable->rowCount(); ++row)
    {
        auto *button = qobject_cast<QPushButton *>(ui->clientsTable->cellWidget(row, CLIENT_UPDATE_COLUMN));
        if (!button || !ui->clientsTable->item(row, 1) || !ui->clientsTable->item(row, CLIENT_STATUS_COLUMN))
            continue;
        const QString ID = ui->clientsTable->item(row, 0)->text();
        const QString TYPE = ui->clientsTable->item(row, 1)->text();
        const bool FIELD = TYPE == QStringLiteral("STM32") || TYPE == QStringLiteral("ARDUINO");
        const bool ONLINE = ui->clientsTable->item(row, CLIENT_STATUS_COLUMN)->text().startsWith(tr("접속 ·"));
        const bool PENDING = ID == refreshingClientId;
        const QString IDLE_LABEL = FIELD ? ONLINE ? tr("현장 갱신") : tr("미접속") : tr("대상 아님");
        button->setText(PENDING ? tr("갱신 중…") : IDLE_LABEL);
        button->setEnabled(FIELD && ONLINE && !dhtLoading && refreshingClientId.isEmpty());
        button->setToolTip(tr("%1의 새 온습도 측정을 요청하고 DB 저장 후 다시 조회합니다.").arg(ID));
        button->setAccessibleName(tr("%1 현장 갱신 요청").arg(ID));
    }
}
