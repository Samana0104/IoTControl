#include "IoTPacketCodec.h"
#include "Paccesspanel.h"
#include "Pmainwindow.h"

#include <QMetaMethod>
#include <QSettings>
#include <cstring>

void MainWindow::InitializeLoginControls()
{
    connect(accessPanel, &AccessPanel::LoginSubmitted, this,
            &MainWindow::SubmitLogin);
    QSettings settings;
    const bool REMEMBER =
        settings.value(QStringLiteral("login/rememberUsername"), false)
            .toBool();
    accessPanel->RestoreUsername(
        REMEMBER, settings.value(QStringLiteral("login/username")).toString());
    connect(accessPanel, &AccessPanel::RememberUsernameChanged, this,
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

void MainWindow::SubmitLogin()
{
    if (!serverPreviewReady)
    {
        ShowServerConnection();
        accessPanel->SetServerFeedback(
            tr("서버 연결 단계를 먼저 진행해 주세요."));
        return;
    }
    const QByteArray ID = accessPanel->ReadUsername().trimmed().toUtf8();
    const QByteArray PASSWORD = accessPanel->ReadPassword().toUtf8();
    const bool INVALID_ID =
        ID.isEmpty() || ID.size() > MEM_ID_SIZE || ID.contains('\0');
    const bool INVALID_PASSWORD = PASSWORD.isEmpty() ||
                                  PASSWORD.size() > MEM_PW_SIZE ||
                                  PASSWORD.contains('\0');
    accessPanel->ClearLoginFeedback();

    if (INVALID_ID || INVALID_PASSWORD)
    {
        accessPanel->ShowLoginError(
            ID.isEmpty() && PASSWORD.isEmpty()
                ? tr("아이디와 비밀번호를 입력해 주세요.")
            : ID.isEmpty()       ? tr("아이디를 입력해 주세요.")
            : PASSWORD.isEmpty() ? tr("비밀번호를 입력해 주세요.")
            : INVALID_ID ? tr("아이디는 UTF-8 기준 1–%1바이트이며 널 문자를 "
                              "포함할 수 없습니다.")
                               .arg(MEM_ID_SIZE)
                         : tr("비밀번호는 UTF-8 기준 1–%1바이트이며 널 문자를 "
                              "포함할 수 없습니다.")
                               .arg(MEM_PW_SIZE),
            INVALID_ID, INVALID_PASSWORD);
        return;
    }

    MemData memData{};
    std::memcpy(memData.id, ID.constData(), static_cast<size_t>(ID.size()));
    std::memcpy(memData.pw, PASSWORD.constData(),
                static_cast<size_t>(PASSWORD.size()));
    uint8_t frame[HEADER_SIZE + MEM_DATA_SIZE]{};
    const size_t FRAME_LENGTH = MakeLoginPacket(frame, sizeof(frame), &memData);
    if (FRAME_LENGTH == 0)
    {
        accessPanel->SetLoginFeedback(
            tr("로그인 요청 패킷을 만들지 못했습니다."), true);
        return;
    }
    const QByteArray PACKET(reinterpret_cast<const char *>(frame),
                            static_cast<qsizetype>(FRAME_LENGTH));

    QSettings settings;
    settings.setValue(QStringLiteral("login/rememberUsername"),
                      accessPanel->ReadRememberUsername());
    if (accessPanel->ReadRememberUsername())
        settings.setValue(QStringLiteral("login/username"),
                          QString::fromUtf8(ID));
    else
        settings.remove(QStringLiteral("login/username"));

    // REQ_LOGIN 프레임을 송신부에 전달합니다. 인증 성공 처리는 ACK_LOGIN 수신
    // 후 수행합니다.
    if (isSignalConnected(QMetaMethod::fromSignal(&MainWindow::LoginRequested)))
        emit LoginRequested(PACKET);
    else
    {
        accessPanel->SetLoginFeedback(
            tr("로그인 서비스가 아직 연결되지 않았어요."), false);
    }
}
