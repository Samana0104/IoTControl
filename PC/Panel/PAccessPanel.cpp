#include "PAccessPanel.h"
#include "PConnectionCanvas.h"
#include "PFeatureDetailsWidget.h"
#include "PGamingTheme.h"
#include "ui_PMainWindow.h"

#include <QEvent>
#include <QGraphicsDropShadowEffect>
#include <QIntValidator>
#include <QMainWindow>
#include <QPainter>
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
    for (const auto &POINT : {QPointF(24, 24), QPointF(48, 24), QPointF(24, 48), QPointF(48, 48)})
        painter.drawRoundedRect(QRectF(POINT.x() - 6, POINT.y() - 6, 12, 12), 3, 3);
    return pixmap;
}

void RefreshStyle(QWidget *widget)
{
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}
} // namespace

AccessPanel::AccessPanel(QMainWindow *window) : QObject(window), window(window), ui(new Ui::MainWindow)
{
    ui->setupUi(window);
    accessRoot = window->centralWidget();
    InitializeDesign();
    InitializeInputEvents();
    InitializeAccessTransition();
}

AccessPanel::~AccessPanel()
{
    ui->accessStack->removeEventFilter(this);
    ui->passwordInput->removeEventFilter(this);
    window->removeEventFilter(this);
    FinishAccessTransition();
    delete ui;
}

void AccessPanel::InitializeDesign()
{
    ApplyGamingPalette(window);
    new GamingBackdropWidget(ui->brandPanel, true);
    const QPixmap LOGO = CreateBrandPixmap();
    window->setWindowIcon(QIcon(LOGO));
    ui->brandMark->setPixmap(LOGO.scaled(36, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->networkVisualLayout->addWidget(new ConnectionCanvas(ui->networkVisual));
    new FeatureDetailsWidget(ui->brandPanel, {ui->featureMonitor, ui->featureControl, ui->featureConnect});
    auto *shadow = new QGraphicsDropShadowEffect(ui->loginCard);
    shadow->setBlurRadius(38);
    shadow->setOffset(0, 8);
    shadow->setColor(QColor(0, 0, 0, 100));
    ui->loginCard->setGraphicsEffect(shadow);
    ui->serverPortInput->setValidator(new QIntValidator(1, 65535, ui->serverPortInput));
    for (auto *input : {ui->serverHostInput, ui->serverPortInput, ui->usernameInput, ui->passwordInput})
    {
        QPalette palette = input->palette();
        palette.setColor(QPalette::PlaceholderText, QColor("#878d9a"));
        input->setPalette(palette);
    }
}

void AccessPanel::InitializeInputEvents()
{
    // 화면은 클릭을 신호로 전달하고 Core에서 실행 함수를 연결합니다.
    connect(ui->connectServerButton, &QPushButton::clicked, this, &AccessPanel::ServerConnectRequested);
    connect(ui->changeServerButton, &QPushButton::clicked, this, &AccessPanel::ServerChangeRequested);
    connect(ui->disconnectServerButton, &QPushButton::clicked, this, &AccessPanel::ServerDisconnectRequested);
    connect(ui->loginButton, &QPushButton::clicked, this, &AccessPanel::LoginSubmitted);
    connect(ui->previewDashboardButton, &QPushButton::clicked, this, &AccessPanel::DashboardPreviewRequested);
    connect(ui->rememberUsername, &QCheckBox::toggled, this, &AccessPanel::RememberUsernameChanged);
    for (auto *input : {ui->serverHostInput, ui->serverPortInput})
    {
        connect(input, &QLineEdit::textChanged, this, &AccessPanel::ClearServerFeedback);
        connect(input, &QLineEdit::returnPressed, this, &AccessPanel::ServerConnectRequested);
    }
    for (auto *input : {ui->usernameInput, ui->passwordInput})
    {
        connect(input, &QLineEdit::textChanged, this, &AccessPanel::ClearLoginFeedback);
        connect(input, &QLineEdit::returnPressed, this, &AccessPanel::LoginSubmitted);
    }
    ui->passwordInput->installEventFilter(this);
    connect(ui->passwordToggleButton, &QToolButton::toggled, this,
            [this](bool visible)
            {
                ui->passwordInput->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
                ui->passwordToggleButton->setText(visible ? tr("숨김") : tr("표시"));
                const QString DESCRIPTION = visible ? tr("비밀번호 숨기기") : tr("비밀번호 표시");
                ui->passwordToggleButton->setToolTip(DESCRIPTION);
                ui->passwordToggleButton->setAccessibleName(DESCRIPTION);
            });
}

QString AccessPanel::ReadServerHost() const { return ui->serverHostInput->text(); }
QString AccessPanel::ReadServerPort() const { return ui->serverPortInput->text(); }
QString AccessPanel::ReadUsername() const { return ui->usernameInput->text(); }
QString AccessPanel::ReadPassword() const { return ui->passwordInput->text(); }
bool AccessPanel::ReadRememberUsername() const { return ui->rememberUsername->isChecked(); }
bool AccessPanel::IsAccessVisible() const { return ui->accessStack->isVisible(); }

void AccessPanel::RestoreUsername(bool remember, const QString &username)
{
    ui->rememberUsername->setChecked(remember);
    if (remember)
        ui->usernameInput->setText(username);
}

void AccessPanel::SetServerSummary(const QString &host, int port)
{
    ui->serverHostInput->setText(host);
    ui->loginServerSummary->setText(tr("SERVER / %1:%2").arg(host).arg(port));
    ui->loginServerSummary->setToolTip(ui->loginServerSummary->text());
}

void AccessPanel::SetLoginEnabled(bool enabled)
{
    loginEnabled = enabled;
    UpdateLoginControls();
}

void AccessPanel::SetLoginBusy(bool busy)
{
    loginBusy = busy;
    UpdateLoginControls();
}

void AccessPanel::UpdateLoginControls()
{
    const bool CAN_SUBMIT = loginEnabled && !loginBusy;
    ui->loginButton->setEnabled(CAN_SUBMIT);
    ui->previewDashboardButton->setEnabled(CAN_SUBMIT);
    ui->usernameInput->setEnabled(CAN_SUBMIT);
    ui->passwordInput->setEnabled(CAN_SUBMIT);
    ui->passwordToggleButton->setEnabled(CAN_SUBMIT);
    ui->rememberUsername->setEnabled(CAN_SUBMIT);
    ui->disconnectServerButton->setEnabled(loginEnabled);
    ui->loginButton->setText(loginBusy ? tr("로그인 중…") : tr("로그인  →"));
}

void AccessPanel::SetServerConnecting(bool connecting)
{
    ui->serverHostInput->setEnabled(!connecting);
    ui->serverPortInput->setEnabled(!connecting);
    ui->connectServerButton->setEnabled(!connecting);
    ui->connectServerButton->setText(connecting ? tr("서버에 연결 중…") : tr("서버 연결  →"));
}

void AccessPanel::ResetPassword()
{
    ui->passwordInput->clear();
    ui->passwordToggleButton->setChecked(false);
}

void AccessPanel::ClearServerFeedback()
{
    ui->serverFeedback->clear();
    for (auto *input : {ui->serverHostInput, ui->serverPortInput})
    {
        input->setProperty("invalid", false);
        RefreshStyle(input);
    }
}

void AccessPanel::ShowServerError(const QString &message, bool invalidHost)
{
    auto *input = invalidHost ? ui->serverHostInput : ui->serverPortInput;
    input->setProperty("invalid", true);
    RefreshStyle(input);
    input->setFocus();
    SetServerFeedback(message);
}

void AccessPanel::SetServerFeedback(const QString &message) { ui->serverFeedback->setText(message); }

void AccessPanel::ClearLoginFeedback()
{
    ui->feedbackLabel->clear();
    ui->usernameInput->setProperty("invalid", false);
    ui->passwordField->setProperty("invalid", false);
    RefreshStyle(ui->usernameInput);
    RefreshStyle(ui->passwordField);
}

void AccessPanel::ShowLoginError(const QString &message, bool invalidId, bool invalidPassword)
{
    ui->usernameInput->setProperty("invalid", invalidId);
    ui->passwordField->setProperty("invalid", invalidPassword);
    RefreshStyle(ui->usernameInput);
    RefreshStyle(ui->passwordField);
    SetLoginFeedback(message, true);
    (invalidId ? ui->usernameInput : ui->passwordInput)->setFocus();
}

void AccessPanel::SetLoginFeedback(const QString &message, bool error)
{
    ui->feedbackLabel->setProperty("messageType", error ? "error" : "info");
    ui->feedbackLabel->setText(message);
    RefreshStyle(ui->feedbackLabel);
}

void AccessPanel::ShowServerPage(bool animate) { ShowAccessPage(ui->serverPage, animate); }

void AccessPanel::ShowLoginPage(bool animate) { ShowAccessPage(ui->loginPage, animate); }

bool AccessPanel::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == ui->accessStack && (event->type() == QEvent::Resize || event->type() == QEvent::Hide)) || (watched == window && event->type() == QEvent::WindowStateChange && window->isMinimized()))
        FinishAccessTransition();
    if (watched == ui->passwordInput && (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut))
    {
        ui->passwordField->setProperty("focused", event->type() == QEvent::FocusIn);
        RefreshStyle(ui->passwordField);
    }
    return QObject::eventFilter(watched, event);
}
