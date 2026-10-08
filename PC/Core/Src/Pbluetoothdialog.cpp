#include "Pbluetoothdialog.h"
#include "Pgamingtheme.h"
#include "ui_Pbluetoothdialog.h"

#include <QStyle>

BluetoothDialog::BluetoothDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::BluetoothDialog)
{
    ui->setupUi(this);
    ApplyGamingPalette(this);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    for (auto *input : {ui->bluetoothIdInput, ui->bluetoothPasswordInput})
    {
        QPalette palette = input->palette();
        palette.setColor(QPalette::PlaceholderText, QColor("#878d9a"));
        input->setPalette(palette);
        connect(input, &QLineEdit::textChanged, this,
                &BluetoothDialog::ClearFeedback);
        connect(input, &QLineEdit::returnPressed, this,
                &BluetoothDialog::ValidateRequest);
    }
    connect(ui->bluetoothPasswordToggle, &QToolButton::toggled, this,
            [this](bool visible)
            {
                ui->bluetoothPasswordInput->setEchoMode(
                    visible ? QLineEdit::Normal : QLineEdit::Password);
                ui->bluetoothPasswordToggle->setText(visible ? tr("숨김")
                                                             : tr("표시"));
                const QString DESCRIPTION =
                    visible ? tr("비밀번호 숨기기") : tr("비밀번호 표시");
                ui->bluetoothPasswordToggle->setToolTip(DESCRIPTION);
                ui->bluetoothPasswordToggle->setAccessibleName(DESCRIPTION);
            });
    connect(ui->bluetoothCancelButton, &QPushButton::clicked, this,
            &QDialog::reject);
    connect(ui->bluetoothRequestButton, &QPushButton::clicked, this,
            &BluetoothDialog::ValidateRequest);
    ui->bluetoothIdInput->setFocus();
}

BluetoothDialog::~BluetoothDialog() { delete ui; }

void BluetoothDialog::ClearFeedback()
{
    ui->bluetoothFeedback->clear();
    for (auto *input : {ui->bluetoothIdInput, ui->bluetoothPasswordInput})
    {
        input->setProperty("invalid", false);
        input->style()->unpolish(input);
        input->style()->polish(input);
        input->update();
    }
}

void BluetoothDialog::ValidateRequest()
{
    ClearFeedback();
    const QString ID = ui->bluetoothIdInput->text().trimmed();
    const QString PASSWORD = ui->bluetoothPasswordInput->text();
    QLineEdit *invalidInput = nullptr;
    QString message;
    if (ID.isEmpty())
    {
        invalidInput = ui->bluetoothIdInput;
        message = tr("장치 계정 아이디를 입력해 주세요.");
    }
    else if (PASSWORD.isEmpty())
    {
        invalidInput = ui->bluetoothPasswordInput;
        message = tr("장치 계정 비밀번호를 입력해 주세요.");
    }
    if (invalidInput)
    {
        invalidInput->setProperty("invalid", true);
        invalidInput->style()->unpolish(invalidInput);
        invalidInput->style()->polish(invalidInput);
        invalidInput->update();
        invalidInput->setFocus();
        ui->bluetoothFeedback->setText(message);
        return;
    }

    // UI preview only: do not retain credentials or issue device requests.
    ui->bluetoothPasswordInput->clear();
    emit PreviewRequested(ID, PASSWORD);
    accept();
}
