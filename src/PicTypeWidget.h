#pragma once

#include "TsScan.h"

#include <QPoint>
#include <QRect>
#include <QVector>
#include <QWidget>

// Per-frame picture-type timeline: draws each coded picture as a skyline bar
// (I tallest, P medium, B short) coloured by type, with open/closed-GOP markers
// at the RAPs. Shares the same viewport (zoom/pan) model as the other widgets,
// so you can zoom into a GOP and read its I/P/B cadence.
class PicTypeWidget : public QWidget {
    Q_OBJECT

public:
    explicit PicTypeWidget(QWidget* parent = nullptr);
    void setFrames(const QVector<FramePic>& frames, qint64 durationMs, const QString& codec);

    void fitAll();
    void setView(qint64 startMs, qint64 endMs);

signals:
    void seekRequested(qint64 ms);

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    QSize minimumSizeHint() const override;

private:
    QRect minimapRect() const;
    QRect rulerRect() const;
    QRect laneRect() const;
    int xForTime(qint64 ms) const;
    qint64 timeForX(int x) const;
    int miniXForTime(qint64 ms) const;
    void zoomAt(int anchorX, double factor);
    void clampView();

    QVector<FramePic> m_frames;
    qint64 m_durationMs = 0;
    QString m_codec;
    int m_nI = 0, m_nP = 0, m_nB = 0, m_nU = 0;

    qint64 m_viewStartMs = 0;
    qint64 m_viewEndMs = 0;

    bool m_dragging = false;
    bool m_dragMoved = false;
    QPoint m_pressPos;
    qint64 m_pressViewStart = 0;
    qint64 m_pressViewEnd = 0;
};
