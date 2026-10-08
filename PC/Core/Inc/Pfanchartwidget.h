#pragma once

#include <QWidget>

class QVariantAnimation;

class FanChartWidget final : public QWidget
{
    Q_OBJECT

  public:
    explicit FanChartWidget(QWidget *parent = nullptr);
    void SetTarget(int percent);

  signals:
    void TargetPreviewChanged(int percent);

  protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

  private:
    QRectF PlotRect() const;
    void UpdateTargetFromPosition(const QPointF &position);

    int targetPercent = 65;
    qreal displayedTarget = 65;
    qreal revealProgress = 1;
    bool dragging = false;
    QVariantAnimation *targetAnimation;
    QVariantAnimation *revealAnimation;
};
