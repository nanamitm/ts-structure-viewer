#include "StructureViewer.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>

namespace {
constexpr int kMinimapH = 30;
constexpr int kRulerH = 22;
constexpr int kMargin = 12;
constexpr qint64 kMinSpanMs = 40; // tightest zoom (~1 frame at 25fps)
constexpr int kClickSlopPx = 4;

// Pleasant round tick step >= target ms.
qint64 niceStep(qint64 target)
{
    static const qint64 steps[] = {
        10, 20, 50, 100, 200, 500,
        1000, 2000, 5000, 10000, 20000, 30000,
        60000, 120000, 300000, 600000, 1800000, 3600000 };
    for (qint64 s : steps)
        if (s >= target)
            return s;
    return 3600000;
}

QString fmtTime(qint64 ms)
{
    const qint64 totalSec = ms / 1000;
    const int h = int(totalSec / 3600);
    const int m = int((totalSec % 3600) / 60);
    const int s = int(totalSec % 60);
    if (h > 0)
        return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}
} // namespace

StructureViewer::StructureViewer(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void StructureViewer::setDuration(qint64 durationMs)
{
    m_durationMs = std::max<qint64>(0, durationMs);
    fitAll();
}

void StructureViewer::setRapTimes(const QVector<qint64>& relMs)
{
    m_rapTimesMs = relMs;
    std::sort(m_rapTimesMs.begin(), m_rapTimesMs.end());
    update();
}

void StructureViewer::setPlan(const QVector<PlanSeg>& plan)
{
    m_plan = plan;
    update();
}

void StructureViewer::setPlayhead(qint64 relMs)
{
    m_playheadMs = relMs;
    update();
}

void StructureViewer::fitAll()
{
    m_viewStartMs = 0;
    m_viewEndMs = std::max<qint64>(kMinSpanMs, m_durationMs);
    update();
}

void StructureViewer::setView(qint64 startMs, qint64 endMs)
{
    m_viewStartMs = startMs;
    m_viewEndMs = endMs;
    clampView();
    update();
}

QRect StructureViewer::minimapRect() const
{
    return QRect(kMargin, kMargin, width() - 2 * kMargin, kMinimapH);
}

QRect StructureViewer::rulerRect() const
{
    return QRect(kMargin, minimapRect().bottom() + 6, width() - 2 * kMargin, kRulerH);
}

QRect StructureViewer::laneRect() const
{
    const int top = rulerRect().bottom() + 2;
    return QRect(kMargin, top, width() - 2 * kMargin, height() - top - kMargin - 18);
}

int StructureViewer::xForTime(qint64 timeMs) const
{
    const QRect lane = laneRect();
    const qint64 span = std::max<qint64>(1, m_viewEndMs - m_viewStartMs);
    return lane.left() + int(double(timeMs - m_viewStartMs) / double(span) * lane.width());
}

qint64 StructureViewer::timeForX(int x) const
{
    const QRect lane = laneRect();
    if (lane.width() <= 0)
        return m_viewStartMs;
    const qint64 span = m_viewEndMs - m_viewStartMs;
    return m_viewStartMs + qint64(double(x - lane.left()) / double(lane.width()) * double(span));
}

int StructureViewer::miniXForTime(qint64 timeMs) const
{
    const QRect mm = minimapRect();
    const qint64 dur = std::max<qint64>(1, m_durationMs);
    return mm.left() + int(double(timeMs) / double(dur) * mm.width());
}

void StructureViewer::clampView()
{
    const qint64 dur = std::max<qint64>(kMinSpanMs, m_durationMs);
    qint64 span = std::clamp<qint64>(m_viewEndMs - m_viewStartMs, kMinSpanMs, dur);
    if (m_viewStartMs < 0)
        m_viewStartMs = 0;
    if (m_viewStartMs + span > dur)
        m_viewStartMs = dur - span;
    if (m_viewStartMs < 0)
        m_viewStartMs = 0;
    m_viewEndMs = m_viewStartMs + span;
}

void StructureViewer::zoomAt(int anchorX, double factor)
{
    const qint64 anchorMs = timeForX(anchorX);
    qint64 span = qint64((m_viewEndMs - m_viewStartMs) * factor);
    span = std::clamp<qint64>(span, kMinSpanMs, std::max<qint64>(kMinSpanMs, m_durationMs));
    const QRect lane = laneRect();
    const double frac = lane.width() > 0
        ? double(anchorX - lane.left()) / double(lane.width())
        : 0.5;
    m_viewStartMs = anchorMs - qint64(frac * span);
    m_viewEndMs = m_viewStartMs + span;
    clampView();
    update();
}

void StructureViewer::panByPixels(int dx)
{
    const QRect lane = laneRect();
    if (lane.width() <= 0)
        return;
    const qint64 span = m_viewEndMs - m_viewStartMs;
    const qint64 deltaMs = qint64(double(-dx) / double(lane.width()) * double(span));
    m_viewStartMs += deltaMs;
    m_viewEndMs += deltaMs;
    clampView();
    update();
}

void StructureViewer::wheelEvent(QWheelEvent* event)
{
    const double notches = event->angleDelta().y() / 120.0;
    if (notches == 0.0)
        return;
    // Zoom out for negative notches, in for positive.
    const double factor = std::pow(0.8, notches);
    zoomAt(int(event->position().x()), factor);
    event->accept();
}

void StructureViewer::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    if ((event->modifiers() & Qt::ShiftModifier) && laneRect().contains(event->pos())) {
        m_selecting = true;
        m_hasSelection = true;
        m_selectionStartMs = std::clamp<qint64>(timeForX(event->pos().x()), 0, m_durationMs);
        m_selectionEndMs = m_selectionStartMs;
        update();
        return;
    }
    if (minimapRect().contains(event->pos())) {
        // Recenter the view on the clicked minimap position.
        const qint64 dur = std::max<qint64>(1, m_durationMs);
        const QRect mm = minimapRect();
        const qint64 centerMs = qint64(double(event->pos().x() - mm.left()) / double(mm.width()) * double(dur));
        const qint64 span = m_viewEndMs - m_viewStartMs;
        m_viewStartMs = centerMs - span / 2;
        m_viewEndMs = m_viewStartMs + span;
        clampView();
        update();
        return;
    }
    m_dragging = true;
    m_dragMoved = false;
    m_pressPos = event->pos();
    m_pressViewStart = m_viewStartMs;
    m_pressViewEnd = m_viewEndMs;
}

void StructureViewer::mouseMoveEvent(QMouseEvent* event)
{
    if (m_selecting) {
        m_selectionEndMs = std::clamp<qint64>(timeForX(event->pos().x()), 0, m_durationMs);
        update();
        return;
    }
    if (!m_dragging)
        return;
    const int dx = event->pos().x() - m_pressPos.x();
    if (std::abs(dx) > kClickSlopPx)
        m_dragMoved = true;
    if (m_dragMoved) {
        // Pan relative to the press anchor (avoids accumulating rounding drift).
        m_viewStartMs = m_pressViewStart;
        m_viewEndMs = m_pressViewEnd;
        panByPixels(dx);
    }
}

void StructureViewer::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_selecting) {
        m_selecting = false;
        m_selectionEndMs = std::clamp<qint64>(timeForX(event->pos().x()), 0, m_durationMs);
        qint64 a = std::min(m_selectionStartMs, m_selectionEndMs);
        qint64 b = std::max(m_selectionStartMs, m_selectionEndMs);
        if (b - a < 1)
            b = std::min<qint64>(m_durationMs, a + 1);
        m_selectionStartMs = a;
        m_selectionEndMs = b;
        emit rangeSelected(a, b);
        update();
        return;
    }
    if (!m_dragging)
        return;
    m_dragging = false;
    if (!m_dragMoved && laneRect().contains(event->pos())) {
        const qint64 t = std::clamp<qint64>(timeForX(event->pos().x()), 0, m_durationMs);
        m_playheadMs = t;
        emit seekRequested(t);
        update();
    }
}

void StructureViewer::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        fitAll();
}

QSize StructureViewer::minimumSizeHint() const
{
    return QSize(480, 200);
}

void StructureViewer::drawRuler(QPainter& p)
{
    const QRect ruler = rulerRect();
    const QRect lane = laneRect();
    const qint64 span = std::max<qint64>(1, m_viewEndMs - m_viewStartMs);
    // Aim for a tick roughly every 90px.
    const qint64 targetMs = span * 90 / std::max(1, lane.width());
    const qint64 step = niceStep(std::max<qint64>(10, targetMs));

    p.setPen(QColor(150, 158, 170));
    qint64 t = (m_viewStartMs / step) * step;
    for (; t <= m_viewEndMs; t += step) {
        if (t < m_viewStartMs)
            continue;
        const int x = xForTime(t);
        p.drawLine(x, ruler.bottom() - 6, x, ruler.bottom());
        p.drawText(QRect(x - 45, ruler.top(), 90, ruler.height() - 6),
                   Qt::AlignHCenter | Qt::AlignVCenter, fmtTime(t));
    }
}

void StructureViewer::drawGops(QPainter& p, const QRect& lane)
{
    if (m_rapTimesMs.isEmpty())
        return;
    // Visible RAP index window via binary search.
    auto lo = std::lower_bound(m_rapTimesMs.begin(), m_rapTimesMs.end(), m_viewStartMs);
    if (lo != m_rapTimesMs.begin())
        --lo; // include the GOP straddling the left edge
    int idx = int(lo - m_rapTimesMs.begin());

    for (int i = idx; i + 1 < m_rapTimesMs.size(); ++i) {
        const qint64 a = m_rapTimesMs[i];
        const qint64 b = m_rapTimesMs[i + 1];
        if (a > m_viewEndMs)
            break;
        int xa = std::max(lane.left(), xForTime(a));
        int xb = std::min(lane.right(), xForTime(b));
        if (xb <= xa)
            continue;
        // Alternate band shading so individual GOPs are countable.
        p.setPen(Qt::NoPen);
        p.setBrush((i & 1) ? QColor(60, 66, 76) : QColor(52, 57, 65));
        p.drawRect(QRect(xa, lane.top(), xb - xa, lane.height()));
    }

    // Keyframe boundary lines, only when spaced enough to be legible.
    p.setPen(QColor(84, 155, 255, 130));
    int lastX = INT_MIN;
    for (int i = idx; i < m_rapTimesMs.size(); ++i) {
        const qint64 t = m_rapTimesMs[i];
        if (t < m_viewStartMs)
            continue;
        if (t > m_viewEndMs)
            break;
        const int x = xForTime(t);
        if (x - lastX < 3)
            continue;
        p.drawLine(x, lane.top(), x, lane.bottom());
        lastX = x;
    }
}

void StructureViewer::drawPlan(QPainter& p, const QRect& lane)
{
    auto fillRange = [&](qint64 s, qint64 e, const QColor& c) {
        if (e <= m_viewStartMs || s >= m_viewEndMs || e <= s)
            return;
        const int xa = std::max(lane.left(), xForTime(s));
        const int xb = std::min(lane.right(), xForTime(e));
        if (xb <= xa)
            return;
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRect(QRect(xa, lane.top(), xb - xa, lane.height()));
    };

    for (const PlanSeg& seg : m_plan) {
        // Copy region: passed through verbatim (green).
        fillRange(seg.copyStartMs, seg.copyEndMs, QColor(64, 190, 120, 90));
        // Re-encode windows: partial GOPs (orange).
        if (seg.hasLeadIn)
            fillRange(seg.inMs, seg.copyStartMs, QColor(240, 150, 60, 130));
        if (seg.hasTail)
            fillRange(seg.copyEndMs, seg.outMs, QColor(240, 150, 60, 130));

        // IN / OUT grips.
        auto grip = [&](qint64 t, const QColor& c) {
            if (t < m_viewStartMs || t > m_viewEndMs)
                return;
            const int x = xForTime(t);
            p.setPen(QPen(c, 2));
            p.drawLine(x, lane.top() - 6, x, lane.bottom() + 6);
        };
        grip(seg.inMs, QColor(255, 92, 92));
        grip(seg.outMs, QColor(255, 126, 58));
    }
}

void StructureViewer::drawMinimap(QPainter& p)
{
    const QRect mm = minimapRect();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(40, 44, 50));
    p.drawRoundedRect(mm, 3, 3);

    // Plan segments as a coarse overview.
    for (const PlanSeg& seg : m_plan) {
        const int xa = miniXForTime(seg.inMs);
        const int xb = miniXForTime(seg.outMs);
        p.setBrush(QColor(64, 190, 120, 120));
        p.drawRect(QRect(xa, mm.top() + 4, std::max(1, xb - xa), mm.height() - 8));
    }

    // Current view window.
    const int vx0 = miniXForTime(m_viewStartMs);
    const int vx1 = miniXForTime(m_viewEndMs);
    p.setPen(QPen(QColor(235, 238, 245), 1));
    p.setBrush(QColor(120, 160, 255, 50));
    p.drawRect(QRect(vx0, mm.top() + 1, std::max(2, vx1 - vx0), mm.height() - 2));
}

void StructureViewer::drawSelection(QPainter& p, const QRect& lane)
{
    if (!m_hasSelection)
        return;
    const qint64 a = std::min(m_selectionStartMs, m_selectionEndMs);
    const qint64 b = std::max(m_selectionStartMs, m_selectionEndMs);
    if (b <= m_viewStartMs || a >= m_viewEndMs)
        return;
    const int xa = std::max(lane.left(), xForTime(a));
    const int xb = std::min(lane.right(), xForTime(b));
    if (xb <= xa)
        return;
    p.setPen(QPen(QColor(150, 190, 255), 1));
    p.setBrush(QColor(90, 145, 255, 65));
    p.drawRect(QRect(xa, lane.top(), xb - xa, lane.height()));
}

void StructureViewer::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(rect(), QColor(22, 24, 28));

    const QRect lane = laneRect();
    if (m_durationMs <= 0 || lane.width() <= 0) {
        p.setPen(QColor(225, 229, 235));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Open a media file"));
        return;
    }

    drawMinimap(p);
    drawRuler(p);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(52, 57, 65));
    p.drawRoundedRect(lane, 4, 4);
    drawGops(p, lane);
    drawPlan(p, lane);
    drawSelection(p, lane);

    // Playhead.
    if (m_playheadMs >= m_viewStartMs && m_playheadMs <= m_viewEndMs) {
        const int x = xForTime(m_playheadMs);
        p.setPen(QPen(QColor(255, 235, 120), 1));
        p.drawLine(x, lane.top() - 8, x, lane.bottom() + 8);
    }

    // Footer status.
    p.setPen(QColor(170, 176, 186));
    const qint64 span = m_viewEndMs - m_viewStartMs;
    QString status = QString("view %1 - %2  (span %3 ms)   rap %4")
                         .arg(fmtTime(m_viewStartMs))
                         .arg(fmtTime(m_viewEndMs))
                         .arg(span)
                         .arg(m_rapTimesMs.size());
    p.drawText(kMargin, height() - 4, status);
}
