#include "PBluetoothManagerDialog.h"
#include "PDashboardWidget.h"
#include "PMainWindow.h"
#include "PServerBluetooth.h"
#include "PServerConnection.h"
void MainWindow::InitializeBluetoothControls()
{
    connect(serverBluetooth, &ServerBluetooth::Completed, this, &MainWindow::LoadSessionStatus);
    connect(dashboard, &DashboardWidget::BluetoothManageRequested, this, &MainWindow::ShowBluetoothManager);
}
void MainWindow::ShowBluetoothManager()
{
    if (!authenticated || !serverConnection->IsConnected())
    {
        dashboard->SetDataFeedback(tr("블루투스 관리는 서버에 로그인한 후 사용할 수 있습니다."));
        return;
    }
    BluetoothManagerDialog dialog(serverBluetooth, this);
    dialog.exec();
}
