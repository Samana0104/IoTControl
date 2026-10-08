#include "PMainWindow.h"

#include "IoTPacket.h"
#include "PAccessPanel.h"
#include "PDashboardWidget.h"
#include "PServerConnection.h"
#include "PServerLogin.h"

#include <QSettings>
#include <QTimer>
#include <cstring>

namespace
{
void ClearLoginMemory(void *memory, size_t length)
{
    volatile uint8_t *bytes = reinterpret_cast<volatile uint8_t *>(memory);
    for (size_t index = 0; index < length; ++index)
        bytes[index] = 0;
}
} // namespace

void MainWindow::InitializeLoginControls()
{
    connect(accessPanel, &AccessPanel::LoginSubmitted, this, &MainWindow::SubmitLogin);
    connect(serverLogin, &ServerLogin::LoginFinished, this, &MainWindow::HandleLoginResult);
    connect(serverLogin, &ServerLogin::LoginRequested, this, &MainWindow::LoginRequested);
    QSettings settings;
    const bool REMEMBER = settings.value(QStringLiteral("login/rememberUsername"), false).toBool();
    accessPanel->RestoreUsername(REMEMBER, settings.value(QStringLiteral("login/username")).toString());
    connect(accessPanel, &AccessPanel::RememberUsernameChanged, this,
            [](bool checked)
            {
                if (!checked)
                {
                    QSettings settings;
                    settings.remove(QStringLiteral("login/username"));
                    settings.setValue(QStringLiteral("login/rememberUsername"), false);
                }
            });
}

void MainWindow::SubmitLogin()
{
    if (serverLogin->IsLoggingIn())
        return;
    if (!serverConnection->IsConnected())
    {
        ShowServerConnection();
        accessPanel->SetServerFeedback(tr("서버에 먼저 접속해 주세요."));
        return;
    }
    MemData member{};
    if (!ReadLoginMember(member))
        return;
    SaveLoginUsername();
    accessPanel->SetLoginBusy(true);
    accessPanel->SetLoginFeedback(tr("로그인 요청을 전송합니다. 서버 응답을 기다리고 있습니다."), false);
    serverLogin->LoginToServer(member);
    // 로그인 통신 객체에는 비밀번호를 저장하지 않습니다.
    ClearLoginMemory(&member, sizeof(member));
    accessPanel->ResetPassword();
}

bool MainWindow::ReadLoginMember(MemData &member)
{
    const QByteArray ID = accessPanel->ReadUsername().trimmed().toUtf8();
    QByteArray password = accessPanel->ReadPassword().toUtf8();
    const bool INVALID_ID = ID.isEmpty() || ID.size() > MEM_ID_SIZE || ID.contains('\0');
    const bool INVALID_PASSWORD = password.isEmpty() || password.size() > MEM_PW_SIZE || password.contains('\0');
    accessPanel->ClearLoginFeedback();

    if (INVALID_ID || INVALID_PASSWORD)
    {
        QString message;
        if (ID.isEmpty() && password.isEmpty())
            message = tr("아이디와 비밀번호를 입력해 주세요.");
        else if (ID.isEmpty())
            message = tr("아이디를 입력해 주세요.");
        else if (password.isEmpty())
            message = tr("비밀번호를 입력해 주세요.");
        else if (INVALID_ID)
            message = tr("아이디는 UTF-8 기준 1–%1바이트이며 널 문자를 포함할 수 없습니다.").arg(MEM_ID_SIZE);
        else
            message = tr("비밀번호는 UTF-8 기준 1–%1바이트이며 널 문자를 포함할 수 없습니다.").arg(MEM_PW_SIZE);
        accessPanel->ShowLoginError(message, INVALID_ID, INVALID_PASSWORD);
        ClearLoginMemory(password.data(), static_cast<size_t>(password.size()));
        return false;
    }

    member = {};
    std::memcpy(member.id, ID.constData(), static_cast<size_t>(ID.size()));
    std::memcpy(member.pw, password.constData(), static_cast<size_t>(password.size()));
    ClearLoginMemory(password.data(), static_cast<size_t>(password.size()));
    return true;
}

void MainWindow::SaveLoginUsername()
{
    QSettings settings;
    settings.setValue(QStringLiteral("login/rememberUsername"), accessPanel->ReadRememberUsername());
    if (accessPanel->ReadRememberUsername())
        settings.setValue(QStringLiteral("login/username"), accessPanel->ReadUsername().trimmed());
    else
        settings.remove(QStringLiteral("login/username"));
}

void MainWindow::HandleLoginResult(LoginResult result)
{
    accessPanel->SetLoginBusy(false);
    accessPanel->ResetPassword();
    switch (result)
    {
    case LOGIN_SUCCESS:
        HandleLoginSuccess();
        break;
    case LOGIN_REJECTED:
        accessPanel->ShowLoginError(tr("로그인이 거절되었습니다. 아이디와 비밀번호를 확인해 주세요."), false, true);
        break;
    case LOGIN_NO_SERVER:
        ShowServerFailure(tr("로그인 중 서버 연결이 종료되었거나 요청 전송에 실패했습니다. 다시 접속해 주세요."));
        break;
    case LOGIN_TIMEOUT:
        ShowServerFailure(tr("로그인 응답 시간이 초과되었습니다. 다시 접속해 주세요. (5초)"));
        break;
    default:
        ShowServerFailure(tr("로그인 응답 패킷이 올바르지 않습니다. 다시 접속해 주세요."));
        break;
    }
}

void MainWindow::HandleLoginSuccess()
{
    accessPanel->ClearLoginFeedback();
    authenticated = true;
    dashboard->SetFanUpdateMode(true);
    dashboard->ResetSessionStatus();
    ShowDashboard();
    dashboard->DisplayDhtRecords({});
    LoadFanSpeed();
    LoadAllDht();
    // 먼저 로그인 ACK가 포함된 수신 바이트를 각 파서에서 모두 소비합니다.
    QTimer::singleShot(0, this, &MainWindow::LoadSessionStatus);
    if (authenticated && serverConnection->IsConnected())
    {
        dhtPollTimer->start();
        sessionPollTimer->start();
    }
}
