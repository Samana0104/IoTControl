#pragma once

#include <QElapsedTimer>
#include <QFont>
#include <QWidget>

class QPainter;
class QPainterPath;
class QTimer;

// 서버 연결/로그인 화면의 허브 그림과 데이터 흐름 애니메이션.
class ConnectionCanvas final : public QWidget
{
  public:
    explicit ConnectionCanvas(QWidget *parent = nullptr);

  protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *) override;

  private:
    void UpdateAnimationState();
    void DrawDataFlow(QPainter &painter, const QPainterPath &path,
                      qreal offset) const;
    void SetTextFont(QPainter &painter, int pixelSize,
                     QFont::Weight weight) const;
    void DrawNode(QPainter &painter, const QRectF &rect, const QString &title,
                  const QString &subtitle, bool controller) const;

    QTimer *animationTimer;
    QElapsedTimer animationClock;
    qreal animationSeconds = 0;
};
