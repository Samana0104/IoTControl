#include "PAccessPanel.h"

#include "ui_PMainWindow.h"

#include <QLayout>
#include <QMainWindow>
#include <QPainter>
#include <QPixmap>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVariantAnimation>

namespace
{
constexpr int ACCESS_TRANSITION_DURATION = 340;
constexpr int ACCESS_TRANSITION_OFFSET = 24;

class AccessTransitionOverlay final : public QWidget
{
  public:
    explicit AccessTransitionOverlay(QWidget *parent) : QWidget(parent)
    {
        setObjectName(QStringLiteral("accessTransition"));
        setAttribute(Qt::WA_OpaquePaintEvent);
        hide();
    }

    void SetFrames(const QPixmap &source, const QPixmap &destination,
                   int direction)
    {
        sourceFrame = source;
        destinationFrame = destination;
        slideDirection = direction;
        progress = 0;
    }

    void SetProgress(qreal value)
    {
        progress = value;
        update();
    }

    void ClearFrames()
    {
        sourceFrame = QPixmap();
        destinationFrame = QPixmap();
    }

  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#16181e"));
        painter.setClipRect(rect());
        const qreal OFFSET = ACCESS_TRANSITION_OFFSET * slideDirection;
        painter.setOpacity(1 - progress);
        painter.drawPixmap(QPointF(-OFFSET * progress, 0), sourceFrame);
        painter.setOpacity(progress);
        painter.drawPixmap(QPointF(OFFSET * (1 - progress), 0),
                           destinationFrame);
    }

  private:
    QPixmap sourceFrame;
    QPixmap destinationFrame;
    qreal progress = 0;
    int slideDirection = 1;
};
} // namespace

void AccessPanel::InitializeAccessTransition()
{
    accessTransition = new AccessTransitionOverlay(ui->accessStack);
    accessAnimation = new QVariantAnimation(accessTransition);
    accessAnimation->setObjectName(QStringLiteral("accessTransitionAnimation"));
    accessAnimation->setDuration(ACCESS_TRANSITION_DURATION);
    accessAnimation->setEasingCurve(QEasingCurve::InOutCubic);
    connect(accessAnimation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value)
            {
                static_cast<AccessTransitionOverlay *>(accessTransition)
                    ->SetProgress(value.toReal());
            });
    connect(accessAnimation, &QVariantAnimation::finished, this,
            &AccessPanel::FinishAccessTransition);
    ui->accessStack->installEventFilter(this);
    window->installEventFilter(this);
}

void AccessPanel::ShowAccessPage(QWidget *page, bool animate)
{
    FinishAccessTransition();
    auto *sourcePage = ui->accessStack->currentWidget();
    if (sourcePage == page || !animate)
    {
        ui->accessStack->setCurrentWidget(page);
        FocusAccessPage();
        return;
    }

    const QPixmap SOURCE_FRAME = sourcePage->grab();
    ui->accessStack->setCurrentWidget(page);
    if (page->layout())
        page->layout()->activate();
    const QPixmap DESTINATION_FRAME = page->grab();
    auto *overlay = static_cast<AccessTransitionOverlay *>(accessTransition);
    overlay->setGeometry(ui->accessStack->rect());
    overlay->SetFrames(SOURCE_FRAME, DESTINATION_FRAME,
                       page == ui->loginPage ? 1 : -1);
    {
        // Animation setters can emit a value from the previous timeline.
        const QSignalBlocker BLOCKER(accessAnimation);
        accessAnimation->setStartValue(0.0);
        accessAnimation->setEndValue(1.0);
        accessAnimation->setCurrentTime(0);
    }
    ui->accessStack->setEnabled(false);
    overlay->show();
    overlay->raise();
    accessAnimation->start();
}

void AccessPanel::FinishAccessTransition()
{
    const bool WAS_ACTIVE = accessTransition->isVisible();
    accessAnimation->stop();
    ui->accessStack->setEnabled(true);
    accessTransition->hide();
    static_cast<AccessTransitionOverlay *>(accessTransition)->ClearFrames();
    if (WAS_ACTIVE)
        FocusAccessPage();
}

void AccessPanel::FocusAccessPage()
{
    if (!accessRoot->isVisible() || window->isMinimized())
        return;
    if (ui->accessStack->currentWidget() == ui->serverPage)
        ui->serverHostInput->setFocus();
    else if (ui->usernameInput->text().isEmpty())
        ui->usernameInput->setFocus();
    else
        ui->passwordInput->setFocus();
}
