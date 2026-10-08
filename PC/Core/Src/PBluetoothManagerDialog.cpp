#include "PBluetoothManagerDialog.h"
#include "PBluetoothManagerPanel.h"
#include "PServerBluetooth.h"
BluetoothManagerDialog::BluetoothManagerDialog(ServerBluetooth *query, QWidget *parent) : QDialog(parent), query(query), panel(new BluetoothManagerPanel(this))
{
    connect(panel, &BluetoothManagerPanel::ScanRequested, this, [this]
            { this->query->StartScan(); });
    connect(panel, &BluetoothManagerPanel::ConnectAllRequested, this, [this]
            { this->query->ConnectAll(); });
    connect(panel, &BluetoothManagerPanel::CloseRequested, this, &BluetoothManagerDialog::reject);
    connect(query, &ServerBluetooth::Changed, this, &BluetoothManagerDialog::Refresh);
    Refresh();
}
BluetoothManagerDialog::~BluetoothManagerDialog() { delete panel; }
void BluetoothManagerDialog::Refresh() { panel->Display(query->IsScanning(), query->IsBusy(), query->ReadRows(), query->ReadFeedback()); }
