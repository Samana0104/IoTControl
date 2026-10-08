#include "Pmainwindow.h"
#include "ui_Pmainwindow.h"

#include <QMetaMethod>
#include <QSettings>

void MainWindow::InitializeLoginControls()
{
    for (auto *input : {ui->usernameInput, ui->passwordInput})
    {
        QPalette palette = input->palette();
        palette.setColor(QPalette::PlaceholderText, QColor("#878d9a"));
        input->setPalette(palette);
        connect(input, &QLineEdit::textChanged, this,
                &MainWindow::ClearFeedback);
        connect(input, &QLineEdit::returnPressed, this,
                &MainWindow::SubmitLogin);
    }
    ui->passwordInput->installEventFilter(this);

    connect(ui->passwordToggleButton, &QToolButton::toggled, this,
            [this](bool visible)
            {
                ui->passwordInput->setEchoMode(visible ? QLineEdit::Normal
                                                       : QLineEdit::Password);
                ui->passwordToggleButton->setText(visible ? tr("숨김")
                                                          : tr("표시"));
                const QString DESCRIPTION =
                    visible ? tr("비밀번호 숨기기") : tr("비밀번호 표시");
                ui->passwordToggleButton->setToolTip(DESCRIPTION);
                ui->passwordToggleButton->setAccessibleName(DESCRIPTION);
            });
    connect(ui->loginButton, &QPushButton::clicked, this,
            &MainWindow::SubmitLogin);

    QSettings settings;
    const bool REMEMBER =
        settings.value(QStringLiteral("login/rememberUsername"), false)
            .toBool();
    ui->rememberUsername->setChecked(REMEMBER);
    if (REMEMBER)
        ui->usernameInput->setText(
            settings.value(QStringLiteral("login/username")).toString());
    connect(ui->rememberUsername, &QCheckBox::toggled, this,
            [](bool checked)
            {
                if (!checked)
                {
                    QSettings settings;
                    settings.remove(QStringLiteral("login/username"));
                    settings.setValue(QStringLiteral("login/rememberUsername"),
                                      false);
                }
            });
}

void MainWindow::ClearFeedback()
{
    ui->feedbackLabel->clear();
    ui->usernameInput->setProperty("invalid", false);
    ui->passwordField->setProperty("invalid", false);
    RefreshStyle(ui->usernameInput);
    RefreshStyle(ui->passwordField);
}

void MainWindow::SubmitLogin()
{
    if (!serverPreviewReady)
    {
        ShowServerConnection();
        ui->serverFeedback->setText(tr("서버 연결 단계를 먼저 진행해 주세요."));
        return;
    }
    const QString USERNAME = ui->usernameInput->text().trimmed();
    const QString PASSWORD = ui->passwordInput->text();
    const bool MISSING_USERNAME = USERNAME.isEmpty();
    const bool MISSING_PASSWORD = PASSWORD.isEmpty();
    ClearFeedback();

    if (MISSING_USERNAME || MISSING_PASSWORD)
    {
        ui->usernameInput->setProperty("invalid", MISSING_USERNAME);
        ui->passwordField->setProperty("invalid", MISSING_PASSWORD);
        ui->feedbackLabel->setProperty("messageType", "error");
        ui->feedbackLabel->setText(
            MISSING_USERNAME && MISSING_PASSWORD
                ? tr("아이디와 비밀번호를 입력해 주세요.")
            : MISSING_USERNAME ? tr("아이디를 입력해 주세요.")
                               : tr("비밀번호를 입력해 주세요."));
        RefreshStyle(ui->usernameInput);
        RefreshStyle(ui->passwordField);
        RefreshStyle(ui->feedbackLabel);
        (MISSING_USERNAME ? ui->usernameInput : ui->passwordInput)->setFocus();
        return;
    }

    QSettings settings;
    settings.setValue(QStringLiteral("login/rememberUsername"),
                      ui->rememberUsername->isChecked());
    if (ui->rememberUsername->isChecked())
        settings.setValue(QStringLiteral("login/username"), USERNAME);
    else
        settings.remove(QStringLiteral("login/username"));

    // Authentication can be connected here; never persist passwords or simulate
    // a successful login.
    if (isSignalConnected(QMetaMethod::fromSignal(&MainWindow::LoginRequested)))
        emit LoginRequested(USERNAME, PASSWORD);
    else
    {
        ui->feedbackLabel->setProperty("messageType", "info");
        ui->feedbackLabel->setText(
            tr("로그인 서비스가 아직 연결되지 않았어요."));
        RefreshStyle(ui->feedbackLabel);
    }
}
