#include "CompareViewer.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kMinimapH = 26;
constexpr int kRulerH = 20;
constexpr int kLaneH = 64;
constexpr int kGapH = 22;   // band between the two lanes (seam markers)
constexpr int kMargin = 12;
constexpr qint64 kMinSpanMs = 40;
constexpr int kClickSlopPx = 4;

const QColor kCopy(64, 190, 120);
const QColor kReenc(240, 150, 60);

qint64 niceStep(qint64 target)
{
    static const qint64 steps[] = {
        10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 30000,
        60000, 120000, 300000, 600000, 1800000, 3600000 };
    for (qint64 s : steps)
        if (s >= target)
            return s;
    return 3600000;
}

QString fmtTime(qint64 ms)
{
    const qint64 t = ms / 1000;
    const int h = int(t / 3600), m = int((t % 3600) / 60), s = int(t % 60);
    if (h > 0)
        return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}
} // namespace

CompareViewer::CompareViewer(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void CompareViewer::setSource(const Source& src)
{
    m_src = src;
    std::sort(m_src.rapMs.begin(), m_src.rapMs.end());
    fitAll();
}

void CompareViewer::setOutput(const QVector<OutPiece>& pieces, qint64 outDurationMs)
{
    m_out = pieces;
    m_outDurationMs = outDurationMs;
    update();
}

void CompareViewer::setPlayhead(qint64 srcMs)
{
    m_playheadMs = srcMs;
    update();
}

void CompareViewer::fitAll()
{
    m_viewStartMs = 0;
    m_viewEndMs = std::max<qint64>(kMinSpanMs, m_src.durationMs);
    update();
}

void CompareViewer::setView(qint64 startMs, qint64 endMs)
{
    m_viewStartMs = startMs;
    m_viewEndMs = endMs;
    clampView();
    update();
}

QRect CompareViewer::minimapRect() const
{
    return QRect(kMargin, kMargin, width() - 2 * kMargin, kMinimapH);
}
QRect CompareViewer::srcRulerRect() const
{
    return QRect(kMargin, minimapRect().bottom() + 6, width() - 2 * kMargin, kRulerH);
}
QRect CompareViewer::srcLaneRect() const
{
    return QRect(kMargin, srcRulerRect().bottom() + 2, width() - 2 * kMargin, kLaneH);
}
QRect CompareViewer::outLaneRect() const
{
    const int top = srcLaneRect().bottom() + kGapH;
    return QRect(kMargin, top, width() - 2 * kMargin, kLaneH);
}
QRect CompareViewer::outRulerRect() const
{
    return QRect(kMargin, outLaneRect().bottom() + 2, width() - 2 * kMargin, kRulerH);
}

int CompareViewer::xForTime(qint64 srcMs) const
{
    const QRect lane = srcLaneRect();
    const qint64 span = std::max<qint64>(1, m_viewEndMs - m_viewStartMs);
    return lane.left() + int(double(srcMs - m_viewStartMs) / double(span) * lane.width());
}
qint64 CompareViewer::timeForX(int x) const
{
    const QRect lane = srcLaneRect();
    if (lane.width() <= 0)
        return m_viewStartMs;
    const qint64 span = m_viewEndMs - m_viewStartMs;
    return m_viewStartMs + qint64(double(x - lane.left()) / double(lane.width()) * double(span));
}
int CompareViewer::miniXForTime(qint64 srcMs) const
{
    const QRect mm = minimapRect();
    const qint64 dur = std::max<qint64>(1, m_src.durationMs);
    return mm.left() + int(double(srcMs) / double(dur) * mm.width());
}

qint64 CompareViewer::srcToOut(qint64 srcMs) const
{
    for (const OutPiece& p : m_out)
        if (srcMs >= p.srcStartMs && srcMs < p.srcEndMs)
            return p.outStartMs + (srcMs - p.srcStartMs);
    return -1;
}

void CompareViewer::clampView()
{
    const qint64 dur = std::max<qint64>(kMinSpanMs, m_src.durationMs);
    qint64 span = std::clamp<qint64>(m_viewEndMs - m_viewStartMs, kMinSpanMs, dur);
    if (m_viewStartMs < 0)
        m_viewStartMs = 0;
    if (m_viewStartMs + span > dur)
        m_viewStartMs = dur - span;
    if (m_viewStartMs < 0)
        m_viewStartMs = 0;
    m_viewEndMs = m_viewStartMs + span;
}

void CompareViewer::zoomAt(int anchorX, double factor)
{
    const qint64 anchorMs = timeForX(anchorX);
    qint64 span = qint64((m_viewEndMs - m_viewStartMs) * factor);
    span = std::clamp<qint64>(span, kMinSpanMs, std::max<qint64>(kMinSpanMs, m_src.durationMs));
    const QRect lane = srcLaneRect();
    const double frac = lane.width() > 0 ? double(anchorX - lane.left()) / double(lane.width()) : 0.5;
    m_viewStartMs = anchorMs - qint64(frac * span);
    m_viewEndMs = m_viewStartMs + span;
    clampView();
    update();
}

void CompareViewer::panByPixels(int dx)
{
    const QRect lane = srcLaneRect();
    if (lane.width() <= 0)
        return;
    const qint64 span = m_viewEndMs - m_viewStartMs;
    const qint64 deltaMs = qint64(double(-dx) / double(lane.width()) * double(span));
    m_viewStartMs += deltaMs;
    m_viewEndMs += deltaMs;
    clampView();
    update();
}

void CompareViewer::wheelEvent(QWheelEvent* event)
{
    const double notches = event->angleDelta().y() / 120.0;
    if (notches == 0.0)
        return;
    zoomAt(int(event->position().x()), std::pow(0.8, notches));
    event->accept();
}

void CompareViewer::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    if (minimapRect().contains(event->pos())) {
        const qint64 dur = std::max<qint64>(1, m_src.durationMs);
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

void CompareViewer::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_dragging)
        return;
    const int dx = event->pos().x() - m_pressPos.x();
    if (std::abs(dx) > kClickSlopPx)
        m_dragMoved = true;
    if (m_dragMoved) {
        m_viewStartMs = m_pressViewStart;
        m_viewEndMs = m_pressViewEnd;
        panByPixels(dx);
    }
}

void CompareViewer::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_dragging)
        return;
    m_dragging = false;
    if (!m_dragMoved) {
        const qint64 t = std::clamp<qint64>(timeForX(event->pos().x()), 0, m_src.durationMs);
        m_playheadMs = t;
        emit seekRequested(t);
        update();
    }
}

void CompareViewer::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        fitAll();
}

QSize CompareViewer::minimumSizeHint() const
{
    return QSize(560, 260);
}

void CompareViewer::drawMinimap(QPainter& p)
{
    const QRect mm = minimapRect();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(40, 44, 50));
    p.drawRoundedRect(mm, 3, 3);
    for (const OutPiece& pc : m_out) {
        const int xa = miniXForTime(pc.srcStartMs);
        const int xb = miniXForTime(pc.srcEndMs);
        QColor c = (pc.kind == PieceKind::Copy) ? kCopy : kReenc;
        c.setAlpha(150);
        p.setBrush(c);
        p.drawRect(QRect(xa, mm.top() + 4, std::max(1, xb - xa), mm.height() - 8));
    }
    const int vx0 = miniXForTime(m_viewStartMs);
    const int vx1 = miniXForTime(m_viewEndMs);
    p.setPen(QPen(QColor(235, 238, 245), 1));
    p.setBrush(QColor(120, 160, 255, 50));
    p.drawRect(QRect(vx0, mm.top() + 1, std::max(2, vx1 - vx0), mm.height() - 2));
}

void CompareViewer::drawSrcRuler(QPainter& p)
{
    const QRect ruler = srcRulerRect();
    const QRect lane = srcLaneRect();
    const qint64 span = std::max<qint64>(1, m_viewEndMs - m_viewStartMs);
    const qint64 step = niceStep(std::max<qint64>(10, span * 90 / std::max(1, lane.width())));
    p.setPen(QColor(150, 158, 170));
    for (qint64 t = (m_viewStartMs / step) * step; t <= m_viewEndMs; t += step) {
        if (t < m_viewStartMs)
            continue;
        const int x = xForTime(t);
        p.drawLine(x, ruler.bottom() - 5, x, ruler.bottom());
        p.drawText(QRect(x - 45, ruler.top(), 90, ruler.height() - 5),
                   Qt::AlignHCenter | Qt::AlignVCenter, fmtTime(t));
    }
}

void CompareViewer::drawSrcLane(QPainter& p, const QRect& lane)
{
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(52, 57, 65));
    p.drawRoundedRect(lane, 4, 4);

    // GOP bands.
    if (!m_src.rapMs.isEmpty()) {
        auto lo = std::lower_bound(m_src.rapMs.begin(), m_src.rapMs.end(), m_viewStartMs);
        if (lo != m_src.rapMs.begin())
            --lo;
        int idx = int(lo - m_src.rapMs.begin());
        for (int i = idx; i + 1 < m_src.rapMs.size(); ++i) {
            if (m_src.rapMs[i] > m_viewEndMs)
                break;
            int xa = std::max(lane.left(), xForTime(m_src.rapMs[i]));
            int xb = std::min(lane.right(), xForTime(m_src.rapMs[i + 1]));
            if (xb <= xa)
                continue;
            p.setBrush((i & 1) ? QColor(60, 66, 76) : QColor(52, 57, 65));
            p.drawRect(QRect(xa, lane.top(), xb - xa, lane.height()));
        }
    }

    // Dim the whole lane, then light the kept ranges so dropped regions recede.
    p.setBrush(QColor(20, 22, 26, 150));
    p.drawRect(lane);
    for (const OutPiece& pc : m_out) {
        const int xa = std::max(lane.left(), xForTime(pc.srcStartMs));
        const int xb = std::min(lane.right(), xForTime(pc.srcEndMs));
        if (xb <= xa)
            continue;
        QColor c = (pc.kind == PieceKind::Copy) ? kCopy : kReenc;
        c.setAlpha(70);
        p.setBrush(c);
        p.drawRect(QRect(xa, lane.top(), xb - xa, lane.height()));
    }

    // Keyframe boundary lines (legible spacing only).
    if (!m_src.rapMs.isEmpty()) {
        p.setPen(QColor(84, 155, 255, 130));
        int lastX = INT_MIN;
        for (qint64 t : m_src.rapMs) {
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

    p.setPen(QColor(210, 215, 222));
    p.drawText(lane.adjusted(6, 2, -6, 0), Qt::AlignLeft | Qt::AlignTop,
               m_src.label.isEmpty() ? QStringLiteral("A: source") : m_src.label);
}

void CompareViewer::drawOutLane(QPainter& p, const QRect& lane)
{
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(46, 50, 58));
    p.drawRoundedRect(lane, 4, 4);

    for (const OutPiece& pc : m_out) {
        const int xa = std::max(lane.left(), xForTime(pc.srcStartMs));
        const int xb = std::min(lane.right(), xForTime(pc.srcEndMs));
        if (xb <= xa)
            continue;
        QColor c = (pc.kind == PieceKind::Copy) ? kCopy : kReenc;
        c.setAlpha(190);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRect(QRect(xa, lane.top(), xb - xa, lane.height()));

        // GOP boundaries: copy keeps the source RAPs; re-encode is one IDR start.
        if (pc.kind == PieceKind::Copy) {
            p.setPen(QColor(20, 40, 30, 160));
            int lastX = INT_MIN;
            for (qint64 t : m_src.rapMs) {
                if (t <= pc.srcStartMs)
                    continue;
                if (t >= pc.srcEndMs || t > m_viewEndMs)
                    break;
                if (t < m_viewStartMs)
                    continue;
                const int x = xForTime(t);
                if (x - lastX < 3)
                    continue;
                p.drawLine(x, lane.top(), x, lane.bottom());
                lastX = x;
            }
        } else {
            p.setPen(QColor(255, 235, 200));
            p.drawLine(xa, lane.top(), xa, lane.bottom());
        }

        // Seam marker in the gap above this piece (splice of non-adjacent source).
        if (pc.seamBefore) {
            const int gy0 = srcLaneRect().bottom();
            const int gy1 = lane.top();
            p.setPen(QPen(QColor(255, 90, 90), 2));
            p.drawLine(xa, gy0, xa, gy1);
            p.setBrush(QColor(255, 90, 90));
            p.setPen(Qt::NoPen);
            p.drawPolygon(QPolygon({ QPoint(xa - 4, gy1 - 8), QPoint(xa + 4, gy1 - 8), QPoint(xa, gy1 - 1) }));
        }
    }

    p.setPen(QColor(210, 215, 222));
    p.drawText(lane.adjusted(6, 2, -6, 0), Qt::AlignLeft | Qt::AlignTop,
               QStringLiteral("B: export"));
}

void CompareViewer::drawOutRuler(QPainter& p)
{
    // Label the rebased output time at each piece's start (drawn at source x).
    const QRect ruler = outRulerRect();
    p.setPen(QColor(150, 170, 150));
    int lastLabelX = INT_MIN;
    for (const OutPiece& pc : m_out) {
        if (pc.srcEndMs < m_viewStartMs || pc.srcStartMs > m_viewEndMs)
            continue;
        const int x = xForTime(pc.srcStartMs);
        if (x < ruler.left() - 40 || x > ruler.right() + 40)
            continue;
        p.drawLine(x, ruler.top(), x, ruler.top() + 5);
        // Skip the label if it would collide with the previous one (e.g. a short
        // lead-in piece sitting right against the copy that follows it).
        if (x - lastLabelX < 52)
            continue;
        p.drawText(QRect(x + 2, ruler.top() + 4, 90, ruler.height() - 4),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("out ") + fmtTime(pc.outStartMs));
        lastLabelX = x;
    }
}

void CompareViewer::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(rect(), QColor(22, 24, 28));

    if (m_src.durationMs <= 0 || srcLaneRect().width() <= 0) {
        p.setPen(QColor(225, 229, 235));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Load source + export"));
        return;
    }

    drawMinimap(p);
    drawSrcRuler(p);
    drawSrcLane(p, srcLaneRect());
    drawOutLane(p, outLaneRect());
    drawOutRuler(p);

    // Playhead across both lanes (source time).
    if (m_playheadMs >= m_viewStartMs && m_playheadMs <= m_viewEndMs) {
        const int x = xForTime(m_playheadMs);
        p.setPen(QPen(QColor(255, 235, 120), 1));
        p.drawLine(x, srcLaneRect().top() - 4, x, outLaneRect().bottom() + 4);
    }

    p.setPen(QColor(170, 176, 186));
    const QString status =
        QString("src %1 - %2   |   export %3 (from %4 source)   |   pieces %5")
            .arg(fmtTime(m_viewStartMs))
            .arg(fmtTime(m_viewEndMs))
            .arg(fmtTime(m_outDurationMs))
            .arg(fmtTime(m_src.durationMs))
            .arg(m_out.size());
    p.drawText(kMargin, height() - 4, status);
}
