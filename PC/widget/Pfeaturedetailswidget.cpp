#include "Pfeaturedetailswidget.h"

#include <QEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QVariantAnimation>

namespace
{
constexpr int SHADOW_MARGIN = 8;
constexpr int CARD_HEIGHT = 160;
constexpr int CARD_GAP = 10;
} // namespace

FeatureDetailsWidget::FeatureDetailsWidget(
    QWidget *panel, const std::array<QPushButton *, 3> &buttons)
    : QWidget(panel), featureButtons(buttons),
      featureEyebrows({QStringLiteral("01 / SENSOR MONITORING"),
                       QStringLiteral("02 / DEVICE CONTROL"),
                       QStringLiteral("03 / LIVE CONNECTION")}),
      featureDescriptions(
          {tr("온도·습도·팬 속도를 한눈에 확인하고,\n시간에 따른 변화를 "
              "그래프로 살펴보세요."),
           tr("팬 속도를 그래프와 슬라이더로 조절하고,\n현장 보드에 최신 "
              "데이터 요청을 보내세요."),
           tr("서버와 보드를 연결하고 상태를 확인하세요.\n블루투스는 ID·PW로 "
              "연결을 요청합니다.")}),
      featureFooters({tr("온도·습도  /  DB 데이터"),
                      tr("팬 속도  /  즉시 업데이트"),
                      tr("소켓 서버  /  블루투스")}),
      closeButton(new QPushButton(QStringLiteral("×"), this)),
      motionAnimation(new QVariantAnimation(this)),
      contentAnimation(new QVariantAnimation(this))
{
    setObjectName(QStringLiteral("featureDetails"));
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("기능 설명"));
    panel->installEventFilter(this);

    closeButton->setObjectName(QStringLiteral("closeFeatureDetails"));
    closeButton->setAutoDefault(false);
    closeButton->installEventFilter(this);
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setAccessibleName(tr("기능 설명 닫기"));
    closeButton->setToolTip(tr("닫기 (Esc)"));
    closeButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; color: #a3adbf; "
        "border: 1px solid transparent; border-radius: 3px; font-size: 21px; }"
        "QPushButton:hover { background: #39232e; color: #fff0f4; }"
        "QPushButton:focus { border-color: #ff526b; }"));
    connect(closeButton, &QPushButton::clicked, this,
            &FeatureDetailsWidget::CloseDetails);

    for (int index = 0; index < int(featureButtons.size()); ++index)
    {
        auto *button = featureButtons[index];
        button->setCheckable(true);
        button->installEventFilter(this);
        button->setAutoDefault(false);
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(tr("클릭하여 설명 펼치기"));
        button->setAccessibleDescription(featureDescriptions[index]);
        connect(button, &QPushButton::clicked, this,
                [this, index] { ToggleFeature(index); });
    }

    motionAnimation->setObjectName(QStringLiteral("featureMotionAnimation"));
    motionAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(motionAnimation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value)
            {
                setGeometry(value.toRect());
                closeButton->setVisible(!closing && GetExpansion() > 0.98);
                update();
            });
    connect(motionAnimation, &QVariantAnimation::finished, this,
            [this]
            {
                if (closing)
                    ResetDetails();
            });

    contentAnimation->setDuration(110);
    contentAnimation->setEasingCurve(QEasingCurve::InOutQuad);
    connect(contentAnimation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value)
            {
                contentOpacity = value.toReal();
                update();
            });
    connect(contentAnimation, &QVariantAnimation::finished, this,
            [this]
            {
                if (!fadingOut)
                    return;
                displayedFeature = selectedFeature;
                UpdateDescription();
                fadingOut = false;
                contentAnimation->setStartValue(qreal(0));
                contentAnimation->setEndValue(qreal(1));
                contentAnimation->start();
            });
    hide();
}

int FeatureDetailsWidget::GetSelectedFeature() const
{
    return closing ? -1 : selectedFeature;
}

void FeatureDetailsWidget::ToggleFeature(int index)
{
    if (isVisible() && selectedFeature == index && !closing)
    {
        CloseDetails();
        return;
    }

    if (closing && selectedFeature != index)
        ResetDetails();

    const bool WAS_VISIBLE = isVisible();
    selectedFeature = index;
    closing = false;
    UpdateSelection();
    if (!WAS_VISIBLE)
    {
        displayedFeature = index;
        contentOpacity = 1;
        UpdateDescription();
        setGeometry(GetButtonRect());
        closeButton->hide();
        show();
        raise();
    }
    else if (displayedFeature != index || fadingOut)
    {
        contentAnimation->stop();
        fadingOut = true;
        contentAnimation->setStartValue(contentOpacity);
        contentAnimation->setEndValue(qreal(0));
        contentAnimation->start();
    }
    setFocus(Qt::OtherFocusReason);
    AnimateTo(GetExpandedRect(), 360);
}

void FeatureDetailsWidget::CloseDetails()
{
    if (!isVisible() || closing)
        return;
    closing = true;
    contentAnimation->stop();
    fadingOut = false;
    displayedFeature = selectedFeature;
    contentOpacity = 1;
    UpdateDescription();
    UpdateSelection();
    closeButton->hide();
    featureButtons[selectedFeature]->setFocus(Qt::OtherFocusReason);
    AnimateTo(GetButtonRect(), 260);
}

void FeatureDetailsWidget::ResetDetails()
{
    motionAnimation->stop();
    contentAnimation->stop();
    selectedFeature = -1;
    displayedFeature = -1;
    closing = false;
    fadingOut = false;
    contentOpacity = 1;
    hide();
    UpdateSelection();
    setAccessibleDescription(QString());
}

void FeatureDetailsWidget::UpdateSelection()
{
    for (int index = 0; index < int(featureButtons.size()); ++index)
        featureButtons[index]->setChecked(!closing && selectedFeature == index);
}

void FeatureDetailsWidget::UpdateDescription()
{
    setAccessibleName(featureButtons[displayedFeature]->text());
    setAccessibleDescription(featureDescriptions[displayedFeature]);
}

void FeatureDetailsWidget::AnimateTo(const QRect &destination, int duration)
{
    const QRect START_RECT = geometry();
    {
        // Reconfiguring a stopped animation can emit values from its previous
        // timeline. Keep those stale values from moving the newly placed card.
        const QSignalBlocker BLOCKER(motionAnimation);
        motionAnimation->stop();
        motionAnimation->setDuration(duration);
        motionAnimation->setStartValue(START_RECT);
        motionAnimation->setEndValue(destination);
        motionAnimation->setCurrentTime(0);
    }
    motionAnimation->start();
}

QRect FeatureDetailsWidget::GetButtonRect() const
{
    const auto *button = featureButtons[selectedFeature];
    const QRect BUTTON_RECT(button->mapTo(parentWidget(), QPoint()),
                            button->size());
    return BUTTON_RECT.adjusted(-SHADOW_MARGIN, -SHADOW_MARGIN, SHADOW_MARGIN,
                                SHADOW_MARGIN);
}

QRect FeatureDetailsWidget::GetExpandedRect() const
{
    const QPoint FIRST_POSITION =
        featureButtons.front()->mapTo(parentWidget(), QPoint());
    const QPoint LAST_POSITION =
        featureButtons.back()->mapTo(parentWidget(), QPoint());
    return QRect(FIRST_POSITION.x() - SHADOW_MARGIN,
                 FIRST_POSITION.y() - CARD_GAP - CARD_HEIGHT - SHADOW_MARGIN,
                 LAST_POSITION.x() + featureButtons.back()->width() -
                     FIRST_POSITION.x() + 2 * SHADOW_MARGIN,
                 CARD_HEIGHT + 2 * SHADOW_MARGIN);
}

qreal FeatureDetailsWidget::GetExpansion() const
{
    if (selectedFeature < 0)
        return 0;
    const int BUTTON_HEIGHT = featureButtons[selectedFeature]->height();
    return qBound(qreal(0),
                  qreal(height() - 2 * SHADOW_MARGIN - BUTTON_HEIGHT) /
                      (CARD_HEIGHT - BUTTON_HEIGHT),
                  qreal(1));
}

void FeatureDetailsWidget::paintEvent(QPaintEvent *)
{
    if (displayedFeature < 0)
        return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal EXPANSION = GetExpansion();
    const qreal RADIUS = 3 + EXPANSION;
    const QRectF CARD_RECT = QRectF(rect()).adjusted(
        SHADOW_MARGIN, SHADOW_MARGIN, -SHADOW_MARGIN, -SHADOW_MARGIN);

    painter.setPen(Qt::NoPen);
    for (int inset = SHADOW_MARGIN; inset > 0; --inset)
    {
        painter.setBrush(QColor(0, 0, 0, int(5 * EXPANSION)));
        painter.drawRoundedRect(
            CARD_RECT.adjusted(-inset, -inset / 2.0, inset, inset),
            RADIUS + inset, RADIUS + inset);
    }
    QLinearGradient background(CARD_RECT.topLeft(), CARD_RECT.bottomRight());
    background.setColorAt(0, QColor("#25222c"));
    background.setColorAt(1, QColor("#171b23"));
    painter.setBrush(background);
    painter.setPen(QPen(QColor(255, 68, 100, int(70 + 65 * EXPANSION)), 1));
    painter.drawRoundedRect(CARD_RECT.adjusted(0.5, 0.5, -0.5, -0.5), RADIUS,
                            RADIUS);

    // Keep the label visible as its pill rises and grows into the detail card.
    painter.setOpacity(contentOpacity);
    QFont titleFont = font();
    titleFont.setPixelSize(qRound(11 + 8 * EXPANSION));
    titleFont.setWeight(EXPANSION > 0.4 ? QFont::DemiBold : QFont::Normal);
    painter.setFont(titleFont);
    painter.setPen(QColor("#f5f5f8"));
    const qreal TITLE_X = CARD_RECT.x() + 20;
    const qreal TITLE_Y = CARD_RECT.y() + 35;
    const qreal CENTERED_X =
        CARD_RECT.center().x() - painter.fontMetrics().horizontalAdvance(
                                     featureButtons[displayedFeature]->text()) /
                                     2.0;
    const qreal CENTERED_Y = CARD_RECT.center().y() - 12;
    painter.drawText(QRectF(CENTERED_X + (TITLE_X - CENTERED_X) * EXPANSION,
                            CENTERED_Y + (TITLE_Y - CENTERED_Y) * EXPANSION,
                            CARD_RECT.width() - 40, 26),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     featureButtons[displayedFeature]->text());

    const qreal REVEAL = qBound(qreal(0), (EXPANSION - 0.55) / 0.45, qreal(1));
    painter.setOpacity(REVEAL * contentOpacity);
    QFont detailFont = font();
    detailFont.setPixelSize(9);
    detailFont.setWeight(QFont::DemiBold);
    detailFont.setLetterSpacing(QFont::AbsoluteSpacing, 1);
    painter.setFont(detailFont);
    painter.setPen(QColor("#ff8095"));
    painter.drawText(CARD_RECT.adjusted(20, 15, -50, -120),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     featureEyebrows[displayedFeature]);

    detailFont.setPixelSize(12);
    detailFont.setWeight(QFont::Normal);
    detailFont.setLetterSpacing(QFont::AbsoluteSpacing, 0);
    painter.setFont(detailFont);
    painter.setPen(QColor("#bbc4d4"));
    painter.drawText(
        QRectF(TITLE_X, CARD_RECT.y() + 70, CARD_RECT.width() - 40, 42),
        Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
        featureDescriptions[displayedFeature]);

    painter.setPen(QColor("#3d414f"));
    painter.drawLine(QPointF(TITLE_X, CARD_RECT.y() + 123),
                     QPointF(CARD_RECT.right() - 20, CARD_RECT.y() + 123));
    detailFont.setPixelSize(10);
    painter.setFont(detailFont);
    painter.setPen(QColor("#a3adbf"));
    painter.drawText(
        QRectF(TITLE_X, CARD_RECT.y() + 132, CARD_RECT.width() - 130, 16),
        Qt::AlignLeft | Qt::AlignVCenter, featureFooters[displayedFeature]);
    painter.setPen(QColor("#ff7b92"));
    painter.drawText(
        QRectF(CARD_RECT.right() - 94, CARD_RECT.y() + 132, 74, 16),
        Qt::AlignRight | Qt::AlignVCenter, tr("기능 미리보기"));
}

void FeatureDetailsWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    closeButton->setGeometry(width() - SHADOW_MARGIN - 42, SHADOW_MARGIN + 9,
                             30, 30);
}

void FeatureDetailsWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
    {
        CloseDetails();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool FeatureDetailsWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != parentWidget() && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape && isVisible())
    {
        CloseDetails();
        return true;
    }
    if (watched == parentWidget())
    {
        if (event->type() == QEvent::Hide)
            ResetDetails();
        else if (event->type() == QEvent::Resize)
        {
            // Layout positions are updated after the parent's resize event.
            QTimer::singleShot(0, this,
                               [this]
                               {
                                   if (!isVisible())
                                       return;
                                   if (closing)
                                       ResetDetails();
                                   else
                                   {
                                       motionAnimation->stop();
                                       setGeometry(GetExpandedRect());
                                       closeButton->show();
                                   }
                               });
        }
    }
    return QWidget::eventFilter(watched, event);
}
