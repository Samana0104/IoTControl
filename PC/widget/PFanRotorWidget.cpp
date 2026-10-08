#include "PFanRotorWidget.h"

#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QVariantAnimation>
#include <cmath>

FanRotorWidget::FanRotorWidget(QWidget *parent)
    : QWidget(parent), rotationTimer(new QTimer(this)),
      speedAnimation(new QVariantAnimation(this))
{
    setFixedSize(30, 30);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAccessibleName(tr("목표 팬 속도 회전 미리보기"));
    setToolTip(tr("목표 속도에 따른 팬 회전 미리보기"));
    rotationTimer->setInterval(16);
    rotationTimer->setTimerType(Qt::PreciseTimer);
    connect(rotationTimer, &QTimer::timeout, this,
            [this]
            {
                const qreal ELAPSED = rotationClock.restart();
                angle = std::fmod(angle + ELAPSED * previewSpeed * 0.01, 360.0);
                update();
            });
    speedAnimation->setDuration(320);
    speedAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(speedAnimation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value)
            {
                previewSpeed = value.toReal();
                UpdateTimerState();
                update();
            });
}

void FanRotorWidget::SetSpeed(int percent)
{
    const int NEXT_SPEED = qBound(0, percent, 100);
    if (NEXT_SPEED == requestedSpeed)
        return;
    requestedSpeed = NEXT_SPEED;
    speedAnimation->stop();
    if (!isVisible())
    {
        previewSpeed = requestedSpeed;
        UpdateTimerState();
        update();
        return;
    }
    speedAnimation->setStartValue(previewSpeed);
    speedAnimation->setEndValue(qreal(requestedSpeed));
    speedAnimation->start();
}

void FanRotorWidget::UpdateTimerState()
{
    const bool ACTIVE =
        isVisible() && !window()->isMinimized() && previewSpeed > 0;
    if (ACTIVE && !rotationTimer->isActive())
    {
        rotationClock.start();
        rotationTimer->start();
    }
    else if (!ACTIVE)
        rotationTimer->stop();
}

void FanRotorWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    window()->installEventFilter(this);
    UpdateTimerState();
}

void FanRotorWidget::hideEvent(QHideEvent *event)
{
    rotationTimer->stop();
    speedAnimation->stop();
    previewSpeed = requestedSpeed;
    QWidget::hideEvent(event);
}

bool FanRotorWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == window() && event->type() == QEvent::WindowStateChange)
        UpdateTimerState();
    return QWidget::eventFilter(watched, event);
}

void FanRotorWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#35222d"));
    painter.drawRoundedRect(QRectF(0, 0, width(), height()), 3, 3);
    painter.translate(width() / 2.0, height() / 2.0);
    painter.rotate(angle);
    painter.setBrush(previewSpeed > 0 ? QColor("#ff5874") : QColor("#88909e"));
    QPainterPath blade;
    blade.moveTo(0, -2);
    blade.cubicTo(-7, -3, -8, -10, -2, -10);
    blade.cubicTo(3, -10, 5, -6, 2, -2);
    blade.closeSubpath();
    for (int index = 0; index < 3; ++index)
    {
        painter.drawPath(blade);
        painter.rotate(120);
    }
    painter.setBrush(QColor("#35222d"));
    painter.drawEllipse(QPointF(0, 0), 3.2, 3.2);
    painter.setBrush(previewSpeed > 0 ? QColor("#ff5874") : QColor("#88909e"));
    painter.drawEllipse(QPointF(0, 0), 1.7, 1.7);
}
