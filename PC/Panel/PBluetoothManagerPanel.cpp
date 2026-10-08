#include "PBluetoothManagerPanel.h"
#include "PGamingTheme.h"
#include "ui_PBluetoothManagerDialog.h"
#include <QDialog>
#include <QHeaderView>
BluetoothManagerPanel::BluetoothManagerPanel(QDialog *dialog) : QObject(dialog), ui(new Ui::BluetoothManagerDialog)
{
    ui->setupUi(dialog);
    ApplyGamingPalette(dialog);
    dialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    ui->bluetoothResultsTable->verticalHeader()->hide();
    ui->bluetoothResultsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    connect(ui->scanBluetoothButton, &QPushButton::clicked, this, &BluetoothManagerPanel::ScanRequested);
    connect(ui->connectAllBluetoothButton, &QPushButton::clicked, this, &BluetoothManagerPanel::ConnectAllRequested);
    connect(ui->closeBluetoothManagerButton, &QPushButton::clicked, this, &BluetoothManagerPanel::CloseRequested);
}
BluetoothManagerPanel::~BluetoothManagerPanel() { delete ui; }
void BluetoothManagerPanel::Display(bool scanning, bool busy, const QList<QStringList> &rows, const QString &feedback)
{
    ui->scanBluetoothButton->setEnabled(!busy);
    ui->connectAllBluetoothButton->setEnabled(!busy);
    ui->closeBluetoothManagerButton->setEnabled(true);
    ui->scanBluetoothButton->setText(busy && scanning ? tr("스캔 중…") : tr("주변 장치 스캔 · 10초"));
    ui->connectAllBluetoothButton->setText(busy && !scanning ? tr("등록 기기 연결 중…") : tr("DB 등록 기기 전체 연결"));
    ui->managerFeedback->setText(feedback.isEmpty() ? tr("작업을 선택해 주세요.") : feedback);
    const QStringList HEADERS = scanning ? QStringList{tr("MAC 주소"), tr("장치 이름"), tr("RSSI (dBm)"), tr("페어링 상태")} : QStringList{tr("등록 ID"), tr("연결 결과")};
    auto *table = ui->bluetoothResultsTable;
    table->clear();
    table->setColumnCount(HEADERS.size());
    table->setHorizontalHeaderLabels(HEADERS);
    table->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row)
        for (int column = 0; column < rows[row].size(); ++column)
            table->setItem(row, column, new QTableWidgetItem(rows[row][column]));
}
