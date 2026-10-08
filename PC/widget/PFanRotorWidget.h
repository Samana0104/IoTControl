#pragma once

#include <QElapsedTimer>
#include <QWidget>
#include <QtTypes>

class QTimer;
class QVariantAnimation;

class FanRotorWidget final : public QWidget
{
    Q_OBJECT

  public:
    explicit FanRotorWidget(QWidget *parent = nullptr);
    void SetSpeed(int percent);

  protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    void UpdateTimerState();

    QTimer *rotationTimer;
    QVariantAnimation *speedAnimation;
    QElapsedTimer rotationClock;
    qreal angle = 0;
    qreal previewSpeed = 65;
    int requestedSpeed = 65;
};
