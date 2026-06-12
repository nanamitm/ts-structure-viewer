#pragma once

#include <QRect>
#include <QString>
#include <QVector>
#include <QWidget>
#include <cstdint>

// Two-file structure comparison viewer for smart-render round-trip verification.
//
// Master axis is SOURCE time. The top lane shows the whole source GOP/RAP
// structure with kept ranges lit and dropped ranges dimmed. The bottom lane
// shows the export: each output piece is drawn directly beneath the source time
// it came from, so a verbatim copy lines up vertically with its origin (and
// keeps the same GOP boundaries), a re-encoded boundary window shows as a short
// orange block, and anything cut out leaves a gap. A secondary ruler under the
// bottom lane reads the rebased OUTPUT time, and seam markers flag the splice
// points where non-adjacent source ranges are joined in the output.
//
// One shared viewport (source ms) drives zoom/pan:
//   wheel zoom (cursor-anchored) | left-drag pan | click seek (source ms)
//   double-click fit | click minimap to recenter
class CompareViewer : public QWidget {
    Q_OBJECT

public:
    explicit CompareViewer(QWidget* parent = nullptr);

    // Source structure.
    struct Source {
        QString label;
        qint64 durationMs = 0;
        QVector<qint64> rapMs; // keyframe times (sorted, relative ms)
    };

    enum class PieceKind { Copy, Reencode };

    // One contiguous run of the export, mapped back to its source range.
    // srcStart/srcEnd is the source span it represents; outStart/outEnd is where
    // it lands on the rebased output timeline. For a Copy piece rapMs may carry
    // the output-time keyframes (else the source RAPs in range are used); a
    // Reencode piece is drawn as a single IDR-started block.
    struct OutPiece {
        qint64 srcStartMs = 0;
        qint64 srcEndMs = 0;
        qint64 outStartMs = 0;
        qint64 outEndMs = 0;
        PieceKind kind = PieceKind::Copy;
        bool seamBefore = false; // joins a non-adjacent source range (splice point)
    };

    void setSource(const Source& src);
    void setOutput(const QVector<OutPiece>& pieces, qint64 outDurationMs);
    void setPlayhead(qint64 srcMs);

    void fitAll();
    void setView(qint64 startMs, qint64 endMs);

signals:
    void seekRequested(qint64 srcMs);

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
    QRect srcRulerRect() const;
    QRect srcLaneRect() const;
    QRect outLaneRect() const;
    QRect outRulerRect() const;

    int xForTime(qint64 srcMs) const;
    qint64 timeForX(int x) const;
    int miniXForTime(qint64 srcMs) const;

    void zoomAt(int anchorX, double factor);
    void panByPixels(int dx);
    void clampView();

    void drawMinimap(QPainter& p);
    void drawSrcRuler(QPainter& p);
    void drawSrcLane(QPainter& p, const QRect& lane);
    void drawOutLane(QPainter& p, const QRect& lane);
    void drawOutRuler(QPainter& p);

    // Map a source ms inside a kept piece to its rebased output ms (-1 if dropped).
    qint64 srcToOut(qint64 srcMs) const;

    Source m_src;
    QVector<OutPiece> m_out;
    qint64 m_outDurationMs = 0;
    qint64 m_playheadMs = -1;

    qint64 m_viewStartMs = 0;
    qint64 m_viewEndMs = 0;

    bool m_dragging = false;
    bool m_dragMoved = false;
    QPoint m_pressPos;
    qint64 m_pressViewStart = 0;
    qint64 m_pressViewEnd = 0;
};
