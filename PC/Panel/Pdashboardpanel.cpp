#include "Pdashboardpanel.h"
#include "Pfanchartwidget.h"
#include "Pfanrotorwidget.h"
#include "Pgamingtheme.h"
#include "ui_Pdashboard.h"

#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QHeaderView>
#include <QParallelAnimationGroup>
#include <QPauseAnimation>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QSequentialAnimationGroup>
#include <QShowEvent>
#include <QStyle>
#include <QVariantAnimation>

namespace
{
constexpr int CLIENT_TIMESTAMP_COLUMN = 4;
constexpr int CLIENT_UPDATE_COLUMN = 5;

QString FormatMetricValue(const QString &finalText, qreal progress)
{
    const int DOT = finalText.indexOf(QLatin1Char('.'));
    const int DECIMALS = DOT < 0 ? 0 : finalText.size() - DOT - 1;
    QString text =
        QString::number(finalText.toDouble() * progress, 'f', DECIMALS);
    if (DOT < 0 && finalText.startsWith(QLatin1Char('0')))
        text = text.rightJustified(finalText.size(), QLatin1Char('0'));
    return text;
}
} // namespace

DashboardPanel::DashboardPanel(QWidget *host)
    : QObject(host), host(host), ui(new Ui::DashboardWidget),
      fanChart(new FanChartWidget(host)), fanRotor(new FanRotorWidget(host))
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
        readoutWidth = qMax(
            readoutWidth,
            TARGET_METRICS.boundingRect(tr("%1%").arg(percent)).width() + 2);
    ui->targetValue->setFixedWidth(readoutWidth);
    ui->targetValueLayout->insertWidget(2, fanRotor, 0, Qt::AlignVCenter);
    ui->fanControlPanelLayout->setAlignment(Qt::AlignTop);
    entrancePanels = {ui->temperatureCard, ui->humidityCard,
                      ui->fanCard,         ui->clientCard,
                      ui->chartPanel,      ui->fanControlPanel,
                      ui->clientsPanel};
    metricLabels = {ui->temperatureValue, ui->humidityValue, ui->fanValue,
                    ui->clientValue};
    for (auto *label : metricLabels)
        metricFinalText.append(label->text());
    for (auto *button : {ui->overviewNav, ui->fanNav, ui->clientsNav})
        button->setCheckable(true);
    ui->overviewNav->setAccessibleDescription(
        tr("모든 디바이스 데이터를 한눈에 확인하기"));
    ui->fanNav->setAccessibleDescription(
        tr("팬 그래프와 목표 속도 조절 화면 열기"));
    ui->clientsNav->setAccessibleDescription(
        tr("현장 보드 목록과 데이터 갱신 화면 열기"));

    connect(ui->returnToLoginButton, &QPushButton::clicked, this,
            &DashboardPanel::ReturnToLogin);
    connect(ui->dashboardChangeServerButton, &QPushButton::clicked, this,
            &DashboardPanel::ServerChangeRequested);
    connect(ui->sidebarBluetoothButton, &QPushButton::clicked, this,
            &DashboardPanel::BluetoothRequested);
    connect(ui->fanSpeedSlider, &QSlider::valueChanged, this,
            &DashboardPanel::TargetChanged);
    connect(fanChart, &FanChartWidget::TargetPreviewChanged, ui->fanSpeedSlider,
            &QSlider::setValue);
    connect(ui->applyFanButton, &QPushButton::clicked, this,
            &DashboardPanel::ApplyFanRequested);
    connect(ui->stopFanButton, &QPushButton::clicked, this,
            [this] { SetTarget(0); });
    connect(ui->fan25Button, &QPushButton::clicked, this,
            [this] { SetTarget(25); });
    connect(ui->fan50Button, &QPushButton::clicked, this,
            [this] { SetTarget(50); });
    connect(ui->fan100Button, &QPushButton::clicked, this,
            [this] { SetTarget(100); });
    connect(ui->reloadDbButton, &QPushButton::clicked, this,
            &DashboardPanel::ReloadDbRequested);
    connect(ui->requestAllButton, &QPushButton::clicked, this,
            &DashboardPanel::UpdateAllRequested);
    connect(ui->overviewNav, &QPushButton::clicked, this,
            [this] { SelectSection(ui->overviewNav); });
    connect(ui->fanNav, &QPushButton::clicked, this,
            [this] { SelectSection(ui->fanNav); });
    connect(ui->clientsNav, &QPushButton::clicked, this,
            [this] { SelectSection(ui->clientsNav); });
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
    if (watched == host && event->type() == QEvent::Show &&
        !event->spontaneous())
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
    ui->clientsTable->verticalHeader()->hide();
    ui->clientsTable->verticalHeader()->setDefaultSectionSize(39);
    ui->clientsTable->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Stretch);
    ui->clientsTable->horizontalHeader()->setMinimumSectionSize(70);
    ui->clientsTable->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft |
                                                              Qt::AlignVCenter);
    ui->clientsTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    ui->clientsTable->horizontalHeader()->setSectionResizeMode(
        CLIENT_TIMESTAMP_COLUMN, QHeaderView::ResizeToContents);
    ui->clientsTable->horizontalHeader()->setSectionResizeMode(
        CLIENT_UPDATE_COLUMN, QHeaderView::Fixed);
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
        auto *requestButton =
            new QPushButton(tr("즉시 갱신"), ui->clientsTable);
        requestButton->setObjectName(
            QStringLiteral("requestClient%1Button").arg(row));
        requestButton->setProperty("role", "rowAction");
        requestButton->setCursor(Qt::PointingHandCursor);
        requestButton->setAccessibleName(
            tr("%1 데이터 즉시 갱신 요청").arg(rows[row][0]));
        ui->clientsTable->setCellWidget(row, CLIENT_UPDATE_COLUMN,
                                        requestButton);
        const QString CLIENT_ID = rows[row][0];
        connect(requestButton, &QPushButton::clicked, this,
                [this, CLIENT_ID] { emit ClientUpdateRequested(CLIENT_ID); });
    }
}

void DashboardPanel::SetServerPreview(const QString &host, int port)
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
        metricLabels[index]->setText(
            FormatMetricValue(metricFinalText[index], 0));
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
                    metricLabels[index]->setText(FormatMetricValue(
                        metricFinalText[index], value.toReal()));
                }
            });
    countSequence->addAnimation(counter);
    group->addAnimation(countSequence);
    connect(group, &QParallelAnimationGroup::finished, this,
            &DashboardPanel::FinishEntrance);
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
    ui->clientsTable->setMaximumHeight(IS_CLIENTS ? QWIDGETSIZE_MAX
                                                  : overviewTableMaximumHeight);
    ui->fanChartHost->setMaximumHeight(IS_FAN ? QWIDGETSIZE_MAX
                                              : overviewChartMaximumHeight);

    if (IS_FAN)
    {
        ui->dashboardTitle->setText(QStringLiteral("FAN CONTROL"));
        ui->dashboardSubtitle->setText(
            tr("그래프와 슬라이더로 목표 팬 속도를 조절하세요."));
    }
    else if (IS_CLIENTS)
    {
        ui->dashboardTitle->setText(QStringLiteral("FIELD CLIENTS"));
        ui->dashboardSubtitle->setText(
            tr("보드별 수집 데이터를 확인하고 즉시 갱신을 요청하세요."));
    }
    else
    {
        ui->dashboardTitle->setText(QStringLiteral("CONTROL CENTER"));
        ui->dashboardSubtitle->setText(
            tr("센서 모니터링 · 팬 제어 · 현장 연결"));
    }

    for (int index = 0; index < ui->dashboardContentLayout->count(); ++index)
    {
        auto *item = ui->dashboardContentLayout->itemAt(index);
        const bool EXPAND =
            (IS_FAN && item->layout() == ui->chartAndControlLayout) ||
            (IS_CLIENTS && item->widget() == ui->clientsPanel);
        ui->dashboardContentLayout->setStretch(index, EXPAND ? 1 : 0);
    }
    ui->dashboardContentLayout->activate();
    ui->contentScroll->verticalScrollBar()->setValue(0);
}

int DashboardPanel::ReadTarget() const { return ui->fanSpeedSlider->value(); }

void DashboardPanel::SetTarget(int percent)
{
    ui->fanSpeedSlider->setValue(percent);
}

void DashboardPanel::DisplayTarget(int percent, int rotorSpeed)
{
    ui->targetValue->setText(tr("%1%").arg(percent));
    fanChart->SetTarget(percent);
    fanRotor->SetSpeed(rotorSpeed);
}

void DashboardPanel::SetFeedback(const QString &message)
{
    ui->dashboardFeedback->setText(message);
}
