#pragma once

#include <QRect>
#include <QVector>
#include <QWidget>
#include <cstdint>

// Read-only TS structure viewer with a viewport (zoom/pan) timeline, intended
// for verifying smart-rendering exports: it overlays the GOP/RAP structure with
// the exporter's plan (copy regions vs. lead-in/tail re-encode windows).
//
// Coordinates are relative ms (0 = first video frame), matching ts-edit-gui.
//
//   * wheel        : zoom in/out centered on the cursor
//   * left-drag    : pan the visible range
//   * left-click   : seek (emits seekRequested)
//   * double-click : fit the whole duration
//   * click minimap: recenter the view there
class StructureViewer : public QWidget {
    Q_OBJECT

public:
    explicit StructureViewer(QWidget* parent = nullptr);

    // One exporter plan segment. The edited output for this cut is [inMs, outMs);
    // [copyStartMs, copyEndMs) is passed through verbatim (GOP-snapped), while
    // [inMs, copyStartMs) and [copyEndMs, outMs) are the partial-GOP re-encode
    // windows when hasLeadIn / hasTail are set.
    struct PlanSeg {
        qint64 inMs = 0;
        qint64 outMs = 0;
        qint64 copyStartMs = 0;
        qint64 copyEndMs = 0;
        bool hasLeadIn = false;
        bool hasTail = false;
    };

    void setDuration(qint64 durationMs);
    void setRapTimes(const QVector<qint64>& relMs); // sorted keyframe times
    void setPlan(const QVector<PlanSeg>& plan);
    void setPlayhead(qint64 relMs);

    void fitAll();                                  // reset view to [0, duration]
    void setView(qint64 startMs, qint64 endMs);     // clamped to [0, duration]

signals:
    void seekRequested(qint64 relMs);

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

    int xForTime(qint64 timeMs) const;   // relative ms -> x pixel (lane viewport)
    qint64 timeForX(int x) const;        // x pixel -> relative ms (lane viewport)
    int miniXForTime(qint64 timeMs) const;

    void zoomAt(int anchorX, double factor);
    void panByPixels(int dx);
    void clampView();
    void drawRuler(QPainter& p);
    void drawGops(QPainter& p, const QRect& lane);
    void drawPlan(QPainter& p, const QRect& lane);
    void drawMinimap(QPainter& p);

    qint64 m_durationMs = 0;
    QVector<qint64> m_rapTimesMs;
    QVector<PlanSeg> m_plan;
    qint64 m_playheadMs = 0;

    qint64 m_viewStartMs = 0;
    qint64 m_viewEndMs = 0;

    bool m_dragging = false;
    bool m_dragMoved = false;
    QPoint m_pressPos;
    qint64 m_pressViewStart = 0;
    qint64 m_pressViewEnd = 0;
};
