#include "Pbluetoothdialog.h"

#include "Pbluetoothpanel.h"

BluetoothDialog::BluetoothDialog(QWidget *parent)
    : QDialog(parent), panel(new BluetoothPanel(this))
{
    connect(panel, &BluetoothPanel::CancelRequested, this, &QDialog::reject);
    connect(panel, &BluetoothPanel::RequestSubmitted, this,
            &BluetoothDialog::ValidateRequest);
}

BluetoothDialog::~BluetoothDialog() { delete panel; }

void BluetoothDialog::ValidateRequest()
{
    panel->ClearFeedback();
    const QString ID = panel->ReadId().trimmed();
    const QString PASSWORD = panel->ReadPassword();
    if (ID.isEmpty())
    {
        panel->ShowInputError(tr("장치 계정 아이디를 입력해 주세요."), true);
        return;
    }
    if (PASSWORD.isEmpty())
    {
        panel->ShowInputError(tr("장치 계정 비밀번호를 입력해 주세요."), false);
        return;
    }
    // UI preview only: do not retain credentials or issue device requests.
    panel->ClearPassword();
    emit PreviewRequested(ID, PASSWORD);
    accept();
}
