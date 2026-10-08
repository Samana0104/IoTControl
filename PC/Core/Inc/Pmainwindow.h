#pragma once

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui
{
class MainWindow;
}
QT_END_NAMESPACE

class DashboardWidget;
class QStackedWidget;
class QVariantAnimation;

class MainWindow : public QMainWindow
{
    Q_OBJECT

  public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    void ShowDashboard();

  signals:
    void LoginRequested(const QString &username, const QString &password);

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    void InitializeServerControls();
    void InitializeLoginControls();
    void InitializeAccessTransition();
    void ShowServerConnection();
    void ShowLogin();
    void ShowAccessPage(QWidget *page, bool animate);
    void FinishAccessTransition();
    void FocusAccessPage();
    // 버튼 함수 연결 예제: connectServerButton 클릭 시 호출할 함수를
    // 선언합니다.
    void ConfirmServerPreview();
    void ClearServerFeedback();
    void SubmitLogin();
    void ClearFeedback();
    void RefreshStyle(QWidget *widget);

    Ui::MainWindow *ui;
    QStackedWidget *pages;
    DashboardWidget *dashboard;
    QSize loginWindowSize;
    QWidget *accessTransition;
    QVariantAnimation *accessAnimation;
    bool serverPreviewReady = false;
};
