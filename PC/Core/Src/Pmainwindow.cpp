#include "Pmainwindow.h"
#include "Pconnectioncanvas.h"
#include "Pdashboardwidget.h"
#include "Pfeaturedetailswidget.h"
#include "Pgamingtheme.h"
#include "ui_Pmainwindow.h"

#include <QEvent>
#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QStackedWidget>
#include <QStyle>

namespace
{
QPixmap CreateBrandPixmap()
{
    QPixmap pixmap(72, 72);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#ff334f"));
    painter.drawRoundedRect(QRectF(0, 0, 72, 72), 6, 6);
    painter.setPen(QPen(QColor("#ffffff"), 3, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(25, 25), QPointF(47, 47));
    painter.drawLine(QPointF(25, 47), QPointF(47, 25));
    painter.setBrush(QColor("#ffffff"));
    painter.setPen(Qt::NoPen);
    for (const auto &POINT :
         {QPointF(24, 24), QPointF(48, 24), QPointF(24, 48), QPointF(48, 48)})
        painter.drawRoundedRect(QRectF(POINT.x() - 6, POINT.y() - 6, 12, 12), 3,
                                3);
    return pixmap;
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ApplyGamingPalette(this);
    new GamingBackdropWidget(ui->brandPanel, true);

    pages = new QStackedWidget(this);
    pages->setObjectName(QStringLiteral("pageStack"));
    pages->addWidget(takeCentralWidget());
    dashboard = new DashboardWidget(pages);
    pages->addWidget(dashboard);
    setCentralWidget(pages);
    InitializeAccessTransition();

    connect(ui->previewDashboardButton, &QPushButton::clicked, this,
            &MainWindow::ShowDashboard);
    connect(dashboard, &DashboardWidget::ReturnToLogin, this,
            &MainWindow::ShowLogin);
    InitializeServerControls();

    const QPixmap LOGO = CreateBrandPixmap();
    setWindowIcon(QIcon(LOGO));
    ui->brandMark->setPixmap(
        LOGO.scaled(36, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->networkVisualLayout->addWidget(new ConnectionCanvas(ui->networkVisual));
    new FeatureDetailsWidget(
        ui->brandPanel,
        {ui->featureMonitor, ui->featureControl, ui->featureConnect});

    auto *shadow = new QGraphicsDropShadowEffect(ui->loginCard);
    shadow->setBlurRadius(38);
    shadow->setOffset(0, 8);
    shadow->setColor(QColor(0, 0, 0, 100));
    ui->loginCard->setGraphicsEffect(shadow);

    InitializeLoginControls();

    loginWindowSize = size();
    ShowServerConnection();
}

MainWindow::~MainWindow()
{
    ui->accessStack->removeEventFilter(this);
    removeEventFilter(this);
    FinishAccessTransition();
    delete ui;
}

void MainWindow::ShowDashboard()
{
    if (!serverPreviewReady)
    {
        ShowServerConnection();
        ui->serverFeedback->setText(
            tr("먼저 서버 주소와 포트를 확인해 주세요."));
        return;
    }
    if (pages->currentWidget() == dashboard)
        return;
    FinishAccessTransition();
    loginWindowSize = size();
    pages->setCurrentWidget(dashboard);
    setWindowTitle(tr("IoT Control — UI 목업"));
    resize(1280, 850);
}

void MainWindow::ShowServerConnection()
{
    const bool FROM_DASHBOARD = pages->currentWidget() == dashboard;
    const bool ANIMATE =
        !FROM_DASHBOARD && ui->accessStack->isVisible() && !isMinimized();
    serverPreviewReady = false;
    pages->setCurrentIndex(0);
    if (FROM_DASHBOARD)
        resize(loginWindowSize);
    setWindowTitle(tr("IoT Control — 서버 연결"));
    ui->passwordInput->clear();
    ui->passwordToggleButton->setChecked(false);
    ui->loginButton->setEnabled(false);
    ui->previewDashboardButton->setEnabled(false);
    ClearFeedback();
    ClearServerFeedback();
    ShowAccessPage(ui->serverPage, ANIMATE);
}

void MainWindow::ShowLogin()
{
    if (!serverPreviewReady)
    {
        ShowServerConnection();
        return;
    }
    const bool FROM_DASHBOARD = pages->currentWidget() == dashboard;
    const bool ANIMATE =
        !FROM_DASHBOARD && ui->accessStack->isVisible() && !isMinimized();
    pages->setCurrentIndex(0);
    if (FROM_DASHBOARD)
        resize(loginWindowSize);
    setWindowTitle(tr("IoT Control — 로그인"));
    ui->passwordInput->clear();
    ui->passwordToggleButton->setChecked(false);
    ClearFeedback();
    ShowAccessPage(ui->loginPage, ANIMATE);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == ui->accessStack &&
         (event->type() == QEvent::Resize || event->type() == QEvent::Hide)) ||
        (watched == this && event->type() == QEvent::WindowStateChange &&
         isMinimized()))
        FinishAccessTransition();
    if (watched == ui->passwordInput &&
        (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut))
    {
        ui->passwordField->setProperty("focused",
                                       event->type() == QEvent::FocusIn);
        RefreshStyle(ui->passwordField);
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::RefreshStyle(QWidget *widget)
{
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}
