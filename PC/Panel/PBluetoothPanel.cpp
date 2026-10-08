#include "PBluetoothPanel.h"
#include "PGamingTheme.h"
#include "ui_PBluetoothDialog.h"

#include <QDialog>
#include <QStyle>

BluetoothPanel::BluetoothPanel(QDialog *dialog)
    : QObject(dialog), ui(new Ui::BluetoothDialog)
{
    ui->setupUi(dialog);
    ApplyGamingPalette(dialog);
    dialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    for (auto *input : {ui->bluetoothIdInput, ui->bluetoothPasswordInput})
    {
        QPalette palette = input->palette();
        palette.setColor(QPalette::PlaceholderText, QColor("#878d9a"));
        input->setPalette(palette);
        connect(input, &QLineEdit::textChanged, this,
                &BluetoothPanel::ClearFeedback);
        connect(input, &QLineEdit::returnPressed, this,
                &BluetoothPanel::RequestSubmitted);
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
            &BluetoothPanel::CancelRequested);
    connect(ui->bluetoothRequestButton, &QPushButton::clicked, this,
            &BluetoothPanel::RequestSubmitted);
    ui->bluetoothIdInput->setFocus();
}

BluetoothPanel::~BluetoothPanel() { delete ui; }

QString BluetoothPanel::ReadId() const { return ui->bluetoothIdInput->text(); }
QString BluetoothPanel::ReadPassword() const
{
    return ui->bluetoothPasswordInput->text();
}
void BluetoothPanel::ClearPassword() { ui->bluetoothPasswordInput->clear(); }

void BluetoothPanel::ClearFeedback()
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

void BluetoothPanel::ShowInputError(const QString &message, bool invalidId)
{
    auto *input = invalidId ? ui->bluetoothIdInput : ui->bluetoothPasswordInput;
    input->setProperty("invalid", true);
    input->style()->unpolish(input);
    input->style()->polish(input);
    input->update();
    input->setFocus();
    ui->bluetoothFeedback->setText(message);
}
