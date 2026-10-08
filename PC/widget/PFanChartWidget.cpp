#include "PFanChartWidget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QShowEvent>
#include <QVariantAnimation>
#include <array>

namespace
{
const std::array<double, 31> SAMPLE_SPEEDS = {
    42, 43, 42, 46, 48, 47, 50, 52, 51, 49, 53, 55, 54, 57, 58, 57,
    60, 62, 61, 58, 61, 64, 63, 65, 67, 66, 64, 67, 68, 66, 65};
}

FanChartWidget::FanChartWidget(QWidget *parent)
    : QWidget(parent), targetAnimation(new QVariantAnimation(this)),
      revealAnimation(new QVariantAnimation(this))
{
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);
    setAccessibleName(tr("팬 속도 샘플 그래프"));
    setAccessibleDescription(tr("클릭 또는 드래그로 목표 속도를 조절합니다. "
                                "위아래 방향키로도 조절할 수 있습니다."));
    setToolTip(tr("목표선을 드래그하세요 · 방향키로 5%씩 조절"));
    targetAnimation->setDuration(220);
    targetAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(targetAnimation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value)
            {
                displayedTarget = value.toReal();
                update();
            });
    revealAnimation->setDuration(950);
    revealAnimation->setEasingCurve(QEasingCurve::InOutCubic);
    revealAnimation->setStartValue(0.0);
    revealAnimation->setEndValue(1.0);
    connect(revealAnimation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value)
            {
                revealProgress = value.toReal();
                update();
            });
}

void FanChartWidget::SetTarget(int percent)
{
    const int NEXT_TARGET = qBound(0, percent, 100);
    if (NEXT_TARGET == targetPercent && !dragging)
        return;
    targetPercent = NEXT_TARGET;
    targetAnimation->stop();
    if (dragging || !isVisible())
    {
        displayedTarget = targetPercent;
        update();
        return;
    }
    targetAnimation->setStartValue(displayedTarget);
    targetAnimation->setEndValue(qreal(targetPercent));
    targetAnimation->start();
}

void FanChartWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!event->spontaneous())
    {
        revealProgress = 0;
        revealAnimation->start();
    }
}

void FanChartWidget::hideEvent(QHideEvent *event)
{
    targetAnimation->stop();
    revealAnimation->stop();
    displayedTarget = targetPercent;
    revealProgress = 1;
    dragging = false;
    QWidget::hideEvent(event);
}

QRectF FanChartWidget::PlotRect() const
{
    return QRectF(40, 12, qMax(1, width() - 64), qMax(1, height() - 42));
}

void FanChartWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QFont chartFont = font();
    chartFont.setPixelSize(10);
    painter.setFont(chartFont);
    const QRectF PLOT = PlotRect();

    painter.fillRect(PLOT, QColor("#0d1016"));
    painter.setPen(QPen(QColor("#222934"), 1));
    for (int tick = 0; tick <= 10; ++tick)
    {
        const qreal X = PLOT.left() + PLOT.width() * tick / 10;
        const qreal Y = PLOT.bottom() - PLOT.height() * tick / 10;
        painter.drawLine(QPointF(X, PLOT.top()), QPointF(X, PLOT.bottom()));
        painter.drawLine(QPointF(PLOT.left(), Y), QPointF(PLOT.right(), Y));
    }

    for (int tick = 0; tick <= 100; tick += 25)
    {
        const qreal Y = PLOT.bottom() - PLOT.height() * tick / 100.0;
        painter.setPen(QPen(QColor("#2e3542"), 1));
        painter.drawLine(QPointF(PLOT.left(), Y), QPointF(PLOT.right(), Y));
        painter.setPen(QColor("#9daabd"));
        painter.drawText(QRectF(0, Y - 8, 31, 16),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(tick) + QLatin1Char('%'));
    }
    for (int tick = 0; tick <= 5; ++tick)
    {
        const qreal X = PLOT.left() + PLOT.width() * tick / 5;
        const QString TEXT =
            tick == 5 ? tr("현재") : tr("-%1분").arg(10 - tick * 2);
        painter.setPen(QColor("#9daabd"));
        painter.drawText(QRectF(X - 25, PLOT.bottom() + 8, 50, 18),
                         Qt::AlignCenter, TEXT);
    }

    QPainterPath line;
    for (std::size_t index = 0; index < SAMPLE_SPEEDS.size(); ++index)
    {
        const QPointF POINT(
            PLOT.left() + PLOT.width() * index / (SAMPLE_SPEEDS.size() - 1),
            PLOT.bottom() - PLOT.height() * SAMPLE_SPEEDS[index] / 100);
        if (index == 0)
            line.moveTo(POINT);
        else
            line.lineTo(POINT);
    }
    QPainterPath fill = line;
    fill.lineTo(PLOT.bottomRight());
    fill.lineTo(PLOT.bottomLeft());
    fill.closeSubpath();
    QLinearGradient gradient(PLOT.topLeft(), PLOT.bottomLeft());
    gradient.setColorAt(0, QColor(255, 51, 79, 70));
    gradient.setColorAt(1, QColor(255, 51, 79, 3));
    painter.save();
    painter.setClipRect(QRectF(PLOT.left() - 2, PLOT.top() - 2,
                               (PLOT.width() + 4) * revealProgress,
                               PLOT.height() + 4));
    painter.fillPath(fill, gradient);
    painter.setPen(QPen(QColor(255, 66, 95, 40), 7, Qt::SolidLine, Qt::RoundCap,
                        Qt::RoundJoin));
    painter.drawPath(line);
    painter.setPen(QPen(QColor("#ff425f"), 2.5, Qt::SolidLine, Qt::RoundCap,
                        Qt::RoundJoin));
    painter.drawPath(line);
    painter.setBrush(QColor("#ff425f"));
    painter.setPen(QPen(QColor("#ffd4dc"), 1));
    for (std::size_t index = 0; index < SAMPLE_SPEEDS.size(); index += 5)
    {
        const QPointF POINT(
            PLOT.left() + PLOT.width() * index / (SAMPLE_SPEEDS.size() - 1),
            PLOT.bottom() - PLOT.height() * SAMPLE_SPEEDS[index] / 100);
        painter.drawEllipse(POINT, 2.8, 2.8);
    }
    painter.restore();

    const qreal TARGET_Y =
        PLOT.bottom() - PLOT.height() * displayedTarget / 100.0;
    painter.setPen(QPen(QColor("#59c7f8"), 1, Qt::DashLine));
    painter.drawLine(QPointF(PLOT.left(), TARGET_Y),
                     QPointF(PLOT.right(), TARGET_Y));
    painter.setBrush(QColor("#ffffff"));
    painter.setPen(QPen(QColor("#59c7f8"), 2));
    painter.drawEllipse(QPointF(PLOT.right(), TARGET_Y), 5, 5);

    painter.setPen(QColor("#92dfff"));
    const qreal LABEL_Y = qBound(PLOT.top(), TARGET_Y - 19, PLOT.bottom() - 16);
    painter.drawText(QRectF(PLOT.right() - 94, LABEL_Y, 86, 16), Qt::AlignRight,
                     tr("목표 %1%").arg(targetPercent));
    if (hasFocus())
    {
        painter.setPen(QPen(QColor("#687b96"), 1, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(rect().adjusted(1, 1, -2, -2), 3, 3);
    }
}

void FanChartWidget::UpdateTargetFromPosition(const QPointF &position)
{
    const QRectF PLOT = PlotRect();
    const int PERCENT =
        qRound((PLOT.bottom() - position.y()) / PLOT.height() * 100);
    SetTarget(PERCENT);
    emit TargetPreviewChanged(targetPercent);
}

void FanChartWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton &&
        PlotRect().adjusted(-6, -6, 6, 6).contains(event->position()))
    {
        dragging = true;
        setFocus(Qt::MouseFocusReason);
        UpdateTargetFromPosition(event->position());
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void FanChartWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (dragging)
    {
        UpdateTargetFromPosition(event->position());
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void FanChartWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && dragging)
    {
        dragging = false;
        UpdateTargetFromPosition(event->position());
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void FanChartWidget::keyPressEvent(QKeyEvent *event)
{
    int percent = targetPercent;
    switch (event->key())
    {
    case Qt::Key_Up:
    case Qt::Key_Right:
        percent += 5;
        break;
    case Qt::Key_Down:
    case Qt::Key_Left:
        percent -= 5;
        break;
    case Qt::Key_Home:
        percent = 0;
        break;
    case Qt::Key_End:
        percent = 100;
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    SetTarget(percent);
    emit TargetPreviewChanged(targetPercent);
    event->accept();
}
