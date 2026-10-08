#include "PConnectionCanvas.h"

#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <cmath>

namespace
{
constexpr qreal TWO_PI = 6.283185307179586;
} // namespace

ConnectionCanvas::ConnectionCanvas(QWidget *parent)
    : QWidget(parent), animationTimer(new QTimer(this))
{
    setObjectName(QStringLiteral("connectionCanvas"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    animationTimer->setObjectName(QStringLiteral("connectionAnimationTimer"));
    animationTimer->setInterval(33);
    animationTimer->setTimerType(Qt::PreciseTimer);
    connect(animationTimer, &QTimer::timeout, this,
            [this]
            {
                if (!isVisible() || window()->isMinimized())
                {
                    UpdateAnimationState();
                    return;
                }
                animationSeconds +=
                    qMin(animationClock.restart() / 1000.0, 0.1);
                update();
            });
}

void ConnectionCanvas::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    window()->installEventFilter(this);
    UpdateAnimationState();
}

void ConnectionCanvas::hideEvent(QHideEvent *event)
{
    animationTimer->stop();
    QWidget::hideEvent(event);
}

bool ConnectionCanvas::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == window() && event->type() == QEvent::WindowStateChange)
        UpdateAnimationState();
    return QWidget::eventFilter(watched, event);
}

void ConnectionCanvas::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal SCALE = qMin(width() / 360.0, height() / 228.0);
    painter.translate((width() - 360 * SCALE) / 2,
                      (height() - 228 * SCALE) / 2);
    painter.scale(SCALE, SCALE);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(125, 136, 158, 25));
    for (int x = 12; x < 360; x += 24)
        for (int y = 10; y < 228; y += 24)
            painter.drawEllipse(QPointF(x, y), 1, 1);

    const QPointF HUB(180, 146);
    const qreal BREATH = 0.5 + 0.5 * std::sin(animationSeconds * TWO_PI / 3.2);
    const qreal GLOW_RADIUS = 103 + 7 * BREATH;
    QRadialGradient glow(HUB, GLOW_RADIUS);
    glow.setColorAt(0, QColor(255, 51, 79, qRound(32 + 22 * BREATH)));
    glow.setColorAt(1, QColor(255, 51, 79, 0));
    painter.setBrush(glow);
    painter.drawEllipse(HUB, GLOW_RADIUS, GLOW_RADIUS);

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(255, 75, 103, 35), 1));
    painter.drawEllipse(HUB, 66, 66);
    QPen dashedRing(QColor(255, 75, 103, 70), 1, Qt::DashLine);
    dashedRing.setDashOffset(animationSeconds * 1.4);
    painter.setPen(dashedRing);
    painter.drawEllipse(HUB, 54, 54);
    for (int index = 0; index < 2; ++index)
    {
        const qreal PROGRESS =
            std::fmod(animationSeconds / 3.2 + index * 0.5, 1.0);
        const qreal RADIUS = 47 + 33 * PROGRESS;
        const int ALPHA = qRound(70 * (1 - PROGRESS) * (1 - PROGRESS));
        painter.setPen(QPen(QColor(255, 85, 112, ALPHA), 1));
        painter.drawEllipse(HUB, RADIUS, RADIUS);
    }
    painter.setPen(
        QPen(QColor(255, 92, 120, 105), 1.4, Qt::SolidLine, Qt::RoundCap));
    const int ORBIT_ANGLE =
        qRound(std::fmod(animationSeconds * 18, 360.0) * 16);
    painter.drawArc(QRectF(114, 80, 132, 132), ORBIT_ANGLE, 38 * 16);
    painter.drawArc(QRectF(114, 80, 132, 132), ORBIT_ANGLE + 180 * 16, 38 * 16);

    QPainterPath leftConnection;
    leftConnection.moveTo(79, 78);
    leftConnection.cubicTo(79, 122, 105, 146, 138, 146);
    QPainterPath rightConnection;
    rightConnection.moveTo(281, 78);
    rightConnection.cubicTo(281, 122, 255, 146, 222, 146);
    painter.setPen(QPen(QColor("#744454"), 1.5, Qt::DashLine));
    painter.drawPath(leftConnection);
    painter.drawPath(rightConnection);
    DrawDataFlow(painter, leftConnection, 0.0);
    DrawDataFlow(painter, rightConnection, 0.32);

    DrawNode(painter, QRectF(5, 14, 149, 64), QStringLiteral("SENSOR NODE"),
             tr("데이터 수집"), false);
    DrawNode(painter, QRectF(206, 14, 149, 64), QStringLiteral("CONTROLLER"),
             tr("디바이스 제어"), true);

    QLinearGradient hubFill(140, 105, 220, 185);
    hubFill.setColorAt(0, QColor("#4c2435"));
    hubFill.setColorAt(1, QColor("#24202c"));
    painter.setBrush(hubFill);
    painter.setPen(QPen(QColor(255, 80, 110, qRound(190 + 65 * BREATH)), 1.5));
    painter.drawEllipse(HUB, 42 + 1.2 * BREATH, 42 + 1.2 * BREATH);
    painter.setPen(Qt::NoPen);
    for (int x = 0; x < 2; ++x)
    {
        for (int y = 0; y < 2; ++y)
        {
            const qreal CELL_GLOW =
                0.5 + 0.5 * std::sin(animationSeconds * 2.4 - (x + y) * 0.7);
            painter.setBrush(
                QColor(255, 115, 143, qRound(185 + 70 * CELL_GLOW)));
            painter.drawRoundedRect(QRectF(169 + x * 13, 129 + y * 13, 9, 9), 2,
                                    2);
        }
    }
    SetTextFont(painter, 10, QFont::DemiBold);
    painter.setPen(QColor("#ffe7ed"));
    painter.drawText(QRectF(140, 160, 80, 16), Qt::AlignCenter,
                     QStringLiteral("IoT HUB"));
    SetTextFont(painter, 10, QFont::Medium);
    painter.setPen(QColor("#a4adbe"));
    painter.drawText(QRectF(80, 214, 200, 14), Qt::AlignCenter,
                     QStringLiteral("ONE WORKSPACE. ALL CONNECTED."));
}

void ConnectionCanvas::UpdateAnimationState()
{
    const bool ACTIVE = isVisible() && !window()->isMinimized();
    if (ACTIVE && !animationTimer->isActive())
    {
        animationClock.start();
        animationTimer->start();
    }
    else if (!ACTIVE)
        animationTimer->stop();
}

void ConnectionCanvas::DrawDataFlow(QPainter &painter, const QPainterPath &path,
                                    qreal offset) const
{
    const qreal PATH_LENGTH = path.length();
    painter.setPen(Qt::NoPen);
    for (int packet = 0; packet < 2; ++packet)
    {
        const qreal PROGRESS =
            std::fmod(animationSeconds / 3.0 + offset + packet * 0.5, 1.0);
        for (int trail = 3; trail > 0; --trail)
        {
            const qreal TAIL_PROGRESS = PROGRESS - trail * 0.042;
            if (TAIL_PROGRESS < 0)
                continue;
            const QPointF TAIL = path.pointAtPercent(
                path.percentAtLength(PATH_LENGTH * TAIL_PROGRESS));
            painter.setBrush(QColor(255, 95, 123, 100 - trail * 22));
            painter.drawEllipse(TAIL, 2.6 - trail * 0.4, 2.6 - trail * 0.4);
        }
        const QPointF POINT =
            path.pointAtPercent(path.percentAtLength(PATH_LENGTH * PROGRESS));
        QRadialGradient halo(POINT, 8);
        halo.setColorAt(0, QColor(255, 95, 123, 85));
        halo.setColorAt(1, QColor(255, 95, 123, 0));
        painter.setBrush(halo);
        painter.drawEllipse(POINT, 8, 8);
        painter.setBrush(QColor("#ff7a92"));
        painter.drawEllipse(POINT, 2.7, 2.7);
    }
}

void ConnectionCanvas::SetTextFont(QPainter &painter, int pixelSize,
                                   QFont::Weight weight) const
{
    QFont textFont = font();
    textFont.setPixelSize(pixelSize);
    textFont.setWeight(weight);
    painter.setFont(textFont);
}

void ConnectionCanvas::DrawNode(QPainter &painter, const QRectF &rect,
                                const QString &title, const QString &subtitle,
                                bool controller) const
{
    painter.setPen(QPen(QColor("#444958"), 1));
    painter.setBrush(QColor("#1a1e27"));
    painter.drawRoundedRect(rect, 3, 3);

    const QRectF ICON(rect.x() + 12, rect.y() + 17, 28, 28);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#332734"));
    painter.drawRoundedRect(ICON, 3, 3);
    painter.setPen(QPen(QColor("#ff7088"), 1.5, Qt::SolidLine, Qt::RoundCap,
                        Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    if (controller)
    {
        painter.drawRoundedRect(ICON.adjusted(5, 6, -5, -14), 2, 2);
        painter.drawRoundedRect(ICON.adjusted(5, 15, -5, -5), 2, 2);
        painter.setBrush(QColor("#ff7088"));
        painter.drawEllipse(QPointF(ICON.x() + 19, ICON.y() + 10), 2, 2);
        painter.drawEllipse(QPointF(ICON.x() + 9, ICON.y() + 19), 2, 2);
    }
    else
    {
        QPainterPath wave;
        wave.moveTo(ICON.x() + 5, ICON.y() + 15);
        wave.lineTo(ICON.x() + 10, ICON.y() + 15);
        wave.lineTo(ICON.x() + 13, ICON.y() + 8);
        wave.lineTo(ICON.x() + 17, ICON.y() + 20);
        wave.lineTo(ICON.x() + 20, ICON.y() + 13);
        wave.lineTo(ICON.x() + 23, ICON.y() + 13);
        painter.drawPath(wave);
    }

    SetTextFont(painter, 10, QFont::DemiBold);
    painter.setPen(QColor("#e2e7f0"));
    painter.drawText(QRectF(rect.x() + 49, rect.y() + 16, 95, 16),
                     Qt::AlignVCenter, title);
    SetTextFont(painter, 10, QFont::Normal);
    painter.setPen(QColor("#a3aebe"));
    painter.drawText(QRectF(rect.x() + 49, rect.y() + 34, 95, 16),
                     Qt::AlignVCenter, subtitle);
}
