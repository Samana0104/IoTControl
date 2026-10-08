#include "PGamingTheme.h"

#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <qt_windows.h>
#endif

void ApplyGamingPalette(QWidget *widget)
{
    QPalette palette = widget->palette();
    palette.setColor(QPalette::Window, QColor("#0d0f13"));
    palette.setColor(QPalette::WindowText, QColor("#f1f2f5"));
    palette.setColor(QPalette::Base, QColor("#101217"));
    palette.setColor(QPalette::AlternateBase, QColor("#181b22"));
    palette.setColor(QPalette::Text, QColor("#e6e8ee"));
    palette.setColor(QPalette::Button, QColor("#20232a"));
    palette.setColor(QPalette::ButtonText, QColor("#e6e8ee"));
    palette.setColor(QPalette::Highlight, QColor("#b9253f"));
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    palette.setColor(QPalette::PlaceholderText, QColor("#878d9a"));
    palette.setColor(QPalette::ToolTipBase, QColor("#22252d"));
    palette.setColor(QPalette::ToolTipText, QColor("#e6e8ee"));
    palette.setColor(QPalette::Light, QColor("#454a55"));
    palette.setColor(QPalette::Mid, QColor("#30343f"));
    palette.setColor(QPalette::Dark, QColor("#08090c"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#727784"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#727784"));
    widget->setPalette(palette);
}

GamingBackdropWidget::GamingBackdropWidget(QWidget *parent, bool prominent) : QWidget(parent), prominent(prominent)
{
    setObjectName(QStringLiteral("gamingBackdrop"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setGeometry(parent->rect());
    parent->installEventFilter(this);
    lower();
}

void GamingBackdropWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal PANEL_WIDTH = width();
    const qreal PANEL_HEIGHT = height();

    QRadialGradient redGlow(QPointF(PANEL_WIDTH * 0.03, PANEL_HEIGHT * 0.05), PANEL_WIDTH * 0.85);
    redGlow.setColorAt(0, QColor(255, 51, 79, prominent ? 25 : 9));
    redGlow.setColorAt(1, QColor(255, 51, 79, 0));
    painter.fillRect(rect(), redGlow);

    painter.setPen(QPen(QColor(115, 129, 150, prominent ? 12 : 7), 1));
    for (int x = 0; x < width(); x += 44)
        painter.drawLine(x, 0, x, height());
    for (int y = 0; y < height(); y += 44)
        painter.drawLine(0, y, width(), y);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(81, 109, 137, prominent ? 13 : 8));
    for (int index = 0; index < 4; ++index)
    {
        const qreal OFFSET = index * 42;
        QPainterPath slash;
        slash.moveTo(PANEL_WIDTH - 144 + OFFSET, PANEL_HEIGHT * 0.55);
        slash.lineTo(PANEL_WIDTH - 122 + OFFSET, PANEL_HEIGHT * 0.55);
        slash.lineTo(PANEL_WIDTH - 306 + OFFSET, PANEL_HEIGHT * 0.55 + 184);
        slash.lineTo(PANEL_WIDTH - 328 + OFFSET, PANEL_HEIGHT * 0.55 + 184);
        slash.closeSubpath();
        painter.drawPath(slash);
    }

    if (prominent)
    {
        painter.setPen(QPen(QColor(255, 51, 79, 160), 2));
        painter.drawLine(QPointF(28, 19), QPointF(88, 19));
        painter.setPen(QPen(QColor(125, 135, 151, 42), 1));
        painter.drawLine(QPointF(PANEL_WIDTH - 20, PANEL_HEIGHT - 42), QPointF(PANEL_WIDTH - 20, PANEL_HEIGHT - 20));
        painter.drawLine(QPointF(PANEL_WIDTH - 20, PANEL_HEIGHT - 20), QPointF(PANEL_WIDTH - 66, PANEL_HEIGHT - 20));
    }
}

bool GamingBackdropWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize)
        setGeometry(parentWidget()->rect());
    return QWidget::eventFilter(watched, event);
}

void ApplyGamingDialogTheme(QWidget *widget)
{
    auto *style = QStyleFactory::create(QStringLiteral("Fusion"));
    if (style)
    {
        style->setParent(widget);
        widget->setStyle(style);
    }
    ApplyGamingPalette(widget);
    widget->setAttribute(Qt::WA_StyledBackground, true);
    widget->setAutoFillBackground(true);
    new GamingBackdropWidget(widget, true);
#ifdef Q_OS_WIN
    const HWND HANDLE = reinterpret_cast<HWND>(widget->winId());
    const BOOL DARK = TRUE;
    const DWORD DARK_MODE_ATTRIBUTE = 20;
    const DWORD LEGACY_DARK_MODE_ATTRIBUTE = 19;
    const DWORD BORDER_COLOR_ATTRIBUTE = 34;
    const DWORD CAPTION_COLOR_ATTRIBUTE = 35;
    const DWORD TEXT_COLOR_ATTRIBUTE = 36;
    const COLORREF BORDER_COLOR = RGB(48, 59, 78);
    const COLORREF CAPTION_COLOR = RGB(16, 18, 25);
    const COLORREF TEXT_COLOR = RGB(228, 234, 245);
    if (FAILED(DwmSetWindowAttribute(HANDLE, DARK_MODE_ATTRIBUTE, &DARK, sizeof(DARK))))
        DwmSetWindowAttribute(HANDLE, LEGACY_DARK_MODE_ATTRIBUTE, &DARK, sizeof(DARK));
    DwmSetWindowAttribute(HANDLE, BORDER_COLOR_ATTRIBUTE, &BORDER_COLOR, sizeof(BORDER_COLOR));
    DwmSetWindowAttribute(HANDLE, CAPTION_COLOR_ATTRIBUTE, &CAPTION_COLOR, sizeof(CAPTION_COLOR));
    DwmSetWindowAttribute(HANDLE, TEXT_COLOR_ATTRIBUTE, &TEXT_COLOR, sizeof(TEXT_COLOR));
#endif
}
