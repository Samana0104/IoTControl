#pragma once

#include <QRect>
#include <QStringList>
#include <QWidget>
#include <array>

class QPushButton;
class QVariantAnimation;

class FeatureDetailsWidget final : public QWidget
{
    Q_OBJECT

  public:
    FeatureDetailsWidget(QWidget *panel,
                         const std::array<QPushButton *, 3> &buttons);
    int GetSelectedFeature() const;

  protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    void ToggleFeature(int index);
    void CloseDetails();
    void ResetDetails();
    void UpdateSelection();
    void UpdateDescription();
    void AnimateTo(const QRect &destination, int duration);
    QRect GetButtonRect() const;
    QRect GetExpandedRect() const;
    qreal GetExpansion() const;

    std::array<QPushButton *, 3> featureButtons;
    QStringList featureEyebrows;
    QStringList featureDescriptions;
    QStringList featureFooters;
    QPushButton *closeButton;
    QVariantAnimation *motionAnimation;
    QVariantAnimation *contentAnimation;
    int selectedFeature = -1;
    int displayedFeature = -1;
    qreal contentOpacity = 1;
    bool closing = false;
    bool fadingOut = false;
};
