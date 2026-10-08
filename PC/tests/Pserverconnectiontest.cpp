#include "Pserverconnection.h"
#include "IoTPacketCodec.h"
#include "Pmainwindow.h"

#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QVariantAnimation>

namespace
{
template <typename T> T *FindChild(QObject &parent, const char *name)
{
    return parent.findChild<T *>(QString::fromLatin1(name));
}

void PrepareWindow(MainWindow &window, quint16 port)
{
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show();
    FindChild<QLineEdit>(window, "serverHostInput")
        ->setText(QStringLiteral("127.0.0.1"));
    FindChild<QLineEdit>(window, "serverPortInput")
        ->setText(QString::number(port));
}
} // namespace

class ServerConnectionTests final : public QObject
{
    Q_OBJECT

  private slots:
    void TestConnectSendReceiveAndClose()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        ServerConnection connection;
        QSignalSpy connected(&connection, &ServerConnection::Connected);
        QSignalSpy failed(&connection, &ServerConnection::ConnectionFailed);
        QSignalSpy received(&connection, &ServerConnection::DataReceived);
        QSignalSpy disconnected(&connection, &ServerConnection::Disconnected);
        QVERIFY(!connection.SendPacket(QByteArray("before-connect")));
        connection.ConnectToServer(QStringLiteral("127.0.0.1"),
                                   server.serverPort());
        QTRY_COMPARE(connected.count(), 1);
        QTRY_VERIFY(server.hasPendingConnections());
        auto *peer = server.nextPendingConnection();
        QVERIFY(connection.IsConnected());
        QVERIFY(!connection.IsConnecting());
        QVERIFY(!FindChild<QTimer>(connection, "serverConnectionTimer")
                     ->isActive());
        const QByteArray FRAME("a\0b\x80", 4);
        QVERIFY(connection.SendPacket(FRAME));
        QTRY_COMPARE(peer->bytesAvailable(), FRAME.size());
        QCOMPARE(peer->readAll(), FRAME);
        QCOMPARE(peer->write(QByteArray("reply\0", 6)), qint64(6));
        QTRY_VERIFY(!received.isEmpty());
        QCOMPARE(received.first().first().toByteArray(),
                 QByteArray("reply\0", 6));
        peer->disconnectFromHost();
        QTRY_VERIFY(!connection.IsConnected());
        QVERIFY(!connection.SendPacket(FRAME));
        QCOMPARE(failed.count(), 0);
        QCOMPARE(disconnected.count(), 1);
    }

    void TestRefusedConnectionAndRetry()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const quint16 PORT = server.serverPort();
        server.close();
        ServerConnection connection;
        QSignalSpy failed(&connection, &ServerConnection::ConnectionFailed);
        QSignalSpy connected(&connection, &ServerConnection::Connected);
        connection.ConnectToServer(QStringLiteral("127.0.0.1"), PORT);
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(!connection.IsConnecting());
        QVERIFY(!connection.IsConnected());
        QVERIFY(server.listen(QHostAddress::LocalHost, PORT));
        connection.ConnectToServer(QStringLiteral("127.0.0.1"), PORT);
        QTRY_COMPARE(connected.count(), 1);
        QCOMPARE(failed.count(), 1);
    }

    void TestTimeoutAndExplicitCancellation()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        ServerConnection connection;
        QSignalSpy failed(&connection, &ServerConnection::ConnectionFailed);
        QSignalSpy connected(&connection, &ServerConnection::Connected);
        auto *timer = FindChild<QTimer>(connection, "serverConnectionTimer");
        QVERIFY(timer);
        QCOMPARE(timer->interval(), 10000);
        connection.ConnectToServer(QStringLiteral("127.0.0.1"),
                                   server.serverPort());
        // 실제 외부 서버 없이 타임아웃 이벤트와 늦은 완료 이벤트를 검증합니다.
        QVERIFY(
            QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
        QCOMPARE(failed.count(), 1);
        QVERIFY(
            failed.first().first().toString().contains(QStringLiteral("10초")));
        QVERIFY(!connection.IsConnecting());
        QVERIFY(!timer->isActive());
        QTest::qWait(100);
        QCOMPARE(connected.count(), 0);
        connection.ConnectToServer(QStringLiteral("127.0.0.1"),
                                   server.serverPort());
        connection.DisconnectFromServer();
        QTest::qWait(100);
        QCOMPARE(connected.count(), 0);
        QCOMPARE(failed.count(), 1);
    }

    void TestWindowConnectLoginSendAndDisconnect()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        MainWindow window;
        PrepareWindow(window, server.serverPort());
        auto *access = FindChild<QStackedWidget>(window, "accessStack");
        auto *button = FindChild<QPushButton>(window, "connectServerButton");
        auto *login = FindChild<QPushButton>(window, "loginButton");
        auto *username = FindChild<QLineEdit>(window, "usernameInput");
        auto *password = FindChild<QLineEdit>(window, "passwordInput");
        auto *host = FindChild<QLineEdit>(window, "serverHostInput");
        button->click();
        QCOMPARE(access->currentWidget()->objectName(),
                 QStringLiteral("serverPage"));
        QVERIFY(!button->isEnabled());
        QVERIFY(!host->isEnabled());
        QVERIFY(!login->isEnabled());
        QTest::keyClick(FindChild<QLineEdit>(window, "serverPortInput"),
                        Qt::Key_Return);
        QTRY_COMPARE(access->currentWidget()->objectName(),
                     QStringLiteral("loginPage"));
        QTRY_VERIFY(access->isEnabled());
        QVERIFY(login->isEnabled());
        QTRY_VERIFY(server.hasPendingConnections());
        auto *peer = server.nextPendingConnection();
        QVERIFY(!server.hasPendingConnections());
        QVERIFY(FindChild<QLabel>(window, "loginServerSummary")
                    ->text()
                    .contains(QString::number(server.serverPort())));
        username->setText(QStringLiteral("demo01"));
        password->setText(QStringLiteral("dummy-password"));
        login->click();
        QTRY_COMPARE(peer->bytesAvailable(),
                     qint64(HEADER_SIZE + MEM_DATA_SIZE));
        const QByteArray FRAME = peer->readAll();
        const auto *FRAME_BYTES =
            reinterpret_cast<const uint8_t *>(FRAME.constData());
        HeaderData header{};
        DecodePacketHeader(FRAME_BYTES, &header);
        QCOMPARE(header.cmd, uint16_t(REQ_LOGIN));
        QCOMPARE(header.length, uint16_t(MEM_DATA_SIZE));
        QCOMPARE(
            CheckPacketCrc(FRAME_BYTES, &header, FRAME_BYTES + HEADER_SIZE), 0);
        QCOMPARE(FRAME.mid(HEADER_SIZE, MEM_ID_SIZE),
                 QByteArray("demo01").leftJustified(MEM_ID_SIZE, '\0'));
        QCOMPARE(FRAME.mid(HEADER_SIZE + MEM_ID_SIZE),
                 QByteArray("dummy-password").leftJustified(MEM_PW_SIZE, '\0'));
        QCOMPARE(FindChild<QStackedWidget>(window, "pageStack")->currentIndex(),
                 0);
        QSignalSpy received(&window, &MainWindow::ServerDataReceived);
        peer->write(QByteArray("raw-response"));
        QTRY_COMPARE(received.count(), 1);
        QCOMPARE(received.first().first().toByteArray(),
                 QByteArray("raw-response"));
        peer->disconnectFromHost();
        QTRY_COMPARE(access->currentWidget()->objectName(),
                     QStringLiteral("serverPage"));
        QTRY_VERIFY(access->isEnabled());
        QVERIFY(!login->isEnabled());
        QVERIFY(password->text().isEmpty());
        QVERIFY(button->isEnabled());
        QVERIFY(host->isEnabled());
        QVERIFY(!FindChild<QLabel>(window, "serverFeedback")->text().isEmpty());
        window.ShowDashboard();
        QCOMPARE(FindChild<QStackedWidget>(window, "pageStack")->currentIndex(),
                 0);
    }

    void TestWindowDisconnectButtons()
    {
        const QStringList SECTIONS = {
            QStringLiteral("login"), QStringLiteral("overviewNav"),
            QStringLiteral("fanNav"), QStringLiteral("clientsNav")};
        for (const QString &SECTION : SECTIONS)
        {
            QTcpServer server;
            QVERIFY(server.listen(QHostAddress::LocalHost, 0));
            MainWindow window;
            window.resize(1000, 700);
            PrepareWindow(window, server.serverPort());
            auto *access = FindChild<QStackedWidget>(window, "accessStack");
            auto *pages = FindChild<QStackedWidget>(window, "pageStack");
            auto *connection = window.findChild<ServerConnection *>();
            auto *loginDisconnect =
                FindChild<QPushButton>(window, "disconnectServerButton");
            QVERIFY(loginDisconnect);
            QVERIFY(!loginDisconnect->isEnabled());
            FindChild<QPushButton>(window, "connectServerButton")->click();
            QTRY_VERIFY(connection->IsConnected());
            QTRY_VERIFY(access->isEnabled());
            QTRY_VERIFY(server.hasPendingConnections());
            auto *peer = server.nextPendingConnection();
            QVERIFY(loginDisconnect->isEnabled());
            FindChild<QLineEdit>(window, "passwordInput")
                ->setText(QStringLiteral("temporary-password"));
            auto *disconnectButton = loginDisconnect;
            if (SECTION != QStringLiteral("login"))
            {
                window.ShowDashboard();
                auto *navigation = window.findChild<QPushButton *>(SECTION);
                QVERIFY(navigation);
                QTest::mouseClick(navigation, Qt::LeftButton);
                window.resize(1000, 700);
                QApplication::processEvents();
                QCOMPARE(pages->currentIndex(), 1);
                disconnectButton = FindChild<QPushButton>(
                    window, "dashboardDisconnectServerButton");
            }
            QVERIFY(disconnectButton);
            QVERIFY(disconnectButton->isVisible());
            QVERIFY(disconnectButton->isEnabled());
            QCOMPARE(window.size(), QSize(1000, 700));
            QVERIFY(window.rect().contains(
                QRect(disconnectButton->mapTo(&window, QPoint()),
                      disconnectButton->size())));
            QVERIFY(disconnectButton->width() >=
                    disconnectButton->minimumSizeHint().width());
            if (SECTION == QStringLiteral("login"))
                QVERIFY(window.grab().save("disconnect-login-preview.png"));
            else if (SECTION == QStringLiteral("overviewNav"))
            {
                QTest::qWait(850);
                QVERIFY(window.grab().save("disconnect-dashboard-preview.png"));
            }
            QSignalSpy failed(connection, &ServerConnection::ConnectionFailed);
            QTest::mouseClick(disconnectButton, Qt::LeftButton);
            QTRY_COMPARE(peer->state(), QAbstractSocket::UnconnectedState);
            QTRY_VERIFY(access->isEnabled());
            QVERIFY(!connection->IsConnected());
            QVERIFY(!connection->IsConnecting());
            QVERIFY(!FindChild<QTimer>(*connection, "serverConnectionTimer")
                         ->isActive());
            QCOMPARE(pages->currentIndex(), 0);
            QCOMPARE(access->currentWidget()->objectName(),
                     QStringLiteral("serverPage"));
            QCOMPARE(window.size(), QSize(1000, 700));
            QVERIFY(FindChild<QLineEdit>(window, "passwordInput")
                        ->text()
                        .isEmpty());
            QVERIFY(
                !FindChild<QPushButton>(window, "loginButton")->isEnabled());
            QVERIFY(!FindChild<QPushButton>(window, "previewDashboardButton")
                         ->isEnabled());
            QVERIFY(!loginDisconnect->isEnabled());
            QCOMPARE(failed.count(), 0);
            QCOMPARE(FindChild<QLabel>(window, "serverFeedback")->text(),
                     QStringLiteral("서버 연결을 해제했습니다."));
            window.ShowDashboard();
            QCOMPARE(pages->currentIndex(), 0);
            FindChild<QPushButton>(window, "connectServerButton")->click();
            QTRY_VERIFY(connection->IsConnected());
            QTRY_VERIFY(access->isEnabled());
            QVERIFY(loginDisconnect->isEnabled());
        }
    }

    void TestWindowFailureRetryServerChangeAndClose()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const quint16 PORT = server.serverPort();
        server.close();
        MainWindow window;
        PrepareWindow(window, PORT);
        auto *button = FindChild<QPushButton>(window, "connectServerButton");
        auto *host = FindChild<QLineEdit>(window, "serverHostInput");
        auto *port = FindChild<QLineEdit>(window, "serverPortInput");
        auto *notice = FindChild<QLabel>(window, "serverFeedback");
        auto *access = FindChild<QStackedWidget>(window, "accessStack");
        host->clear();
        button->click();
        QVERIFY(host->property("invalid").toBool());
        host->setText(QStringLiteral("127.0.0.1"));
        port->setText(QStringLiteral("0"));
        button->click();
        QVERIFY(port->property("invalid").toBool());
        port->setText(QString::number(PORT));
        button->click();
        QTRY_VERIFY(button->isEnabled());
        QVERIFY(notice->text().contains(QStringLiteral("접속 실패")));
        QCOMPARE(access->currentWidget()->objectName(),
                 QStringLiteral("serverPage"));
        QVERIFY(server.listen(QHostAddress::LocalHost, PORT));
        QTest::keyClick(port, Qt::Key_Return);
        QTRY_COMPARE(access->currentWidget()->objectName(),
                     QStringLiteral("loginPage"));
        QTRY_VERIFY(access->isEnabled());
        QTRY_VERIFY(server.hasPendingConnections());
        auto *peer = server.nextPendingConnection();
        FindChild<QPushButton>(window, "changeServerButton")->click();
        QTRY_VERIFY(access->isEnabled());
        QTRY_COMPARE(peer->state(), QAbstractSocket::UnconnectedState);
        QCOMPARE(access->currentWidget()->objectName(),
                 QStringLiteral("serverPage"));
        QVERIFY(notice->text().isEmpty());
        button->click();
        // 접속 중 창을 파괴해도 늦은 콜백이 삭제된 Panel에 접근하지 않습니다.
    }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setStyle(QStringLiteral("Fusion"));
    app.setOrganizationName(QStringLiteral("IoTControlTests"));
    app.setApplicationName(QStringLiteral("ServerConnectionTests"));
    QTemporaryDir settingsDirectory;
    if (!settingsDirectory.isValid())
        return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settingsDirectory.path());
    ServerConnectionTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "Pserverconnectiontest.moc"
