#pragma once

#include <QWidget>

void ApplyGamingPalette(QWidget *widget);
void ApplyGamingDialogTheme(QWidget *widget);

class GamingBackdropWidget final : public QWidget
{
  public:
    explicit GamingBackdropWidget(QWidget *parent, bool prominent = false);

  protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    bool prominent;
};
