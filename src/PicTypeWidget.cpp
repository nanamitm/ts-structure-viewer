#include "PicTypeWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kMinimapH = 22;
constexpr int kRulerH = 20;
constexpr int kMargin = 12;
constexpr int kLegendH = 18;
constexpr qint64 kMinSpanMs = 40;
constexpr int kClickSlopPx = 4;

QColor colorFor(char t)
{
    switch (t) {
    case 'I': return QColor(64, 190, 120);
    case 'P': return QColor(110, 150, 255);
    case 'B': return QColor(230, 170, 90);
    default: return QColor(120, 125, 135);
    }
}
double heightFrac(char t)
{
    switch (t) {
    case 'I': return 1.0;
    case 'P': return 0.66;
    case 'B': return 0.40;
    default: return 0.50;
    }
}
QString fmtTime(qint64 ms)
{
    const qint64 t = ms / 1000;
    const int h = int(t / 3600), m = int((t % 3600) / 60), s = int(t % 60);
    const int frac = int(ms % 1000);
    if (h > 0)
        return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    return QString("%1:%2.%3").arg(m).arg(s, 2, 10, QChar('0')).arg(frac, 3, 10, QChar('0'));
}
qint64 niceStep(qint64 target)
{
    static const qint64 steps[] = { 10, 20, 50, 100, 200, 500, 1000, 2000, 5000,
                                    10000, 20000, 30000, 60000, 120000, 300000, 600000 };
    for (qint64 s : steps)
        if (s >= target)
            return s;
    return 600000;
}
} // namespace

PicTypeWidget::PicTypeWidget(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void PicTypeWidget::setFrames(const QVector<FramePic>& frames, qint64 durationMs, const QString& codec)
{
    m_frames = frames;
    m_durationMs = std::max<qint64>(kMinSpanMs, durationMs);
    m_codec = codec;
    m_nI = m_nP = m_nB = m_nU = 0;
    for (const FramePic& f : frames) {
        switch (f.type) {
        case 'I': ++m_nI; break;
        case 'P': ++m_nP; break;
        case 'B': ++m_nB; break;
        default: ++m_nU; break;
        }
    }
    fitAll();
}

void PicTypeWidget::fitAll()
{
    m_viewStartMs = 0;
    m_viewEndMs = m_durationMs;
    update();
}
void PicTypeWidget::setView(qint64 startMs, qint64 endMs)
{
    m_viewStartMs = startMs;
    m_viewEndMs = endMs;
    clampView();
    update();
}

QRect PicTypeWidget::minimapRect() const
{
    return QRect(kMargin, kMargin, width() - 2 * kMargin, kMinimapH);
}
QRect PicTypeWidget::rulerRect() const
{
    return QRect(kMargin, minimapRect().bottom() + 6, width() - 2 * kMargin, kRulerH);
}
QRect PicTypeWidget::laneRect() const
{
    const int top = rulerRect().bottom() + 2;
    return QRect(kMargin, top, width() - 2 * kMargin, height() - top - kMargin - kLegendH);
}

int PicTypeWidget::xForTime(qint64 ms) const
{
    const QRect lane = laneRect();
    const qint64 span = std::max<qint64>(1, m_viewEndMs - m_viewStartMs);
    return lane.left() + int(double(ms - m_viewStartMs) / double(span) * lane.width());
}
qint64 PicTypeWidget::timeForX(int x) const
{
    const QRect lane = laneRect();
    if (lane.width() <= 0)
        return m_viewStartMs;
    return m_viewStartMs + qint64(double(x - lane.left()) / double(lane.width()) * double(m_viewEndMs - m_viewStartMs));
}
int PicTypeWidget::miniXForTime(qint64 ms) const
{
    const QRect mm = minimapRect();
    return mm.left() + int(double(ms) / double(std::max<qint64>(1, m_durationMs)) * mm.width());
}

void PicTypeWidget::clampView()
{
    const qint64 dur = std::max<qint64>(kMinSpanMs, m_durationMs);
    qint64 span = std::clamp<qint64>(m_viewEndMs - m_viewStartMs, kMinSpanMs, dur);
    if (m_viewStartMs < 0) m_viewStartMs = 0;
    if (m_viewStartMs + span > dur) m_viewStartMs = dur - span;
    if (m_viewStartMs < 0) m_viewStartMs = 0;
    m_viewEndMs = m_viewStartMs + span;
}

void PicTypeWidget::zoomAt(int anchorX, double factor)
{
    const qint64 anchorMs = timeForX(anchorX);
    qint64 span = qint64((m_viewEndMs - m_viewStartMs) * factor);
    span = std::clamp<qint64>(span, kMinSpanMs, m_durationMs);
    const QRect lane = laneRect();
    const double frac = lane.width() > 0 ? double(anchorX - lane.left()) / double(lane.width()) : 0.5;
    m_viewStartMs = anchorMs - qint64(frac * span);
    m_viewEndMs = m_viewStartMs + span;
    clampView();
    update();
}

void PicTypeWidget::wheelEvent(QWheelEvent* e)
{
    const double notches = e->angleDelta().y() / 120.0;
    if (notches != 0.0)
        zoomAt(int(e->position().x()), std::pow(0.8, notches));
    e->accept();
}
void PicTypeWidget::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    if (minimapRect().contains(e->pos())) {
        const QRect mm = minimapRect();
        const qint64 centerMs = qint64(double(e->pos().x() - mm.left()) / double(mm.width()) * double(m_durationMs));
        const qint64 span = m_viewEndMs - m_viewStartMs;
        m_viewStartMs = centerMs - span / 2;
        m_viewEndMs = m_viewStartMs + span;
        clampView();
        update();
        return;
    }
    m_dragging = true;
    m_dragMoved = false;
    m_pressPos = e->pos();
    m_pressViewStart = m_viewStartMs;
    m_pressViewEnd = m_viewEndMs;
}
void PicTypeWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_dragging)
        return;
    const int dx = e->pos().x() - m_pressPos.x();
    if (std::abs(dx) > kClickSlopPx)
        m_dragMoved = true;
    if (m_dragMoved) {
        const QRect lane = laneRect();
        const qint64 span = m_pressViewEnd - m_pressViewStart;
        const qint64 deltaMs = qint64(double(-dx) / double(std::max(1, lane.width())) * double(span));
        m_viewStartMs = m_pressViewStart + deltaMs;
        m_viewEndMs = m_pressViewEnd + deltaMs;
        clampView();
        update();
    }
}
void PicTypeWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (!m_dragging)
        return;
    m_dragging = false;
    if (!m_dragMoved && laneRect().contains(e->pos()))
        emit seekRequested(std::clamp<qint64>(timeForX(e->pos().x()), 0, m_durationMs));
}
void PicTypeWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
        fitAll();
}
QSize PicTypeWidget::minimumSizeHint() const
{
    return QSize(480, 160);
}

void PicTypeWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(22, 24, 28));
    const QRect lane = laneRect();
    if (m_frames.isEmpty() || lane.width() <= 0) {
        p.setPen(QColor(225, 229, 235));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Open a TS file"));
        return;
    }

    // Minimap (I-frame density + view window).
    const QRect mm = minimapRect();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(40, 44, 50));
    p.drawRoundedRect(mm, 3, 3);
    p.setPen(QColor(64, 190, 120, 120));
    for (const FramePic& f : m_frames)
        if (f.type == 'I') {
            const int x = miniXForTime(f.ptsMs);
            p.drawLine(x, mm.top() + 3, x, mm.bottom() - 3);
        }
    p.setPen(QPen(QColor(235, 238, 245), 1));
    p.setBrush(QColor(120, 160, 255, 50));
    p.drawRect(QRect(miniXForTime(m_viewStartMs), mm.top() + 1,
                     std::max(2, miniXForTime(m_viewEndMs) - miniXForTime(m_viewStartMs)), mm.height() - 2));

    // Ruler.
    const qint64 span = std::max<qint64>(1, m_viewEndMs - m_viewStartMs);
    const qint64 step = niceStep(std::max<qint64>(10, span * 90 / std::max(1, lane.width())));
    p.setPen(QColor(150, 158, 170));
    for (qint64 t = (m_viewStartMs / step) * step; t <= m_viewEndMs; t += step) {
        if (t < m_viewStartMs) continue;
        const int x = xForTime(t);
        p.drawLine(x, rulerRect().bottom() - 5, x, rulerRect().bottom());
        p.drawText(QRect(x - 50, rulerRect().top(), 100, rulerRect().height() - 5),
                   Qt::AlignHCenter | Qt::AlignVCenter, fmtTime(t));
    }

    // Lane background + baseline.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(30, 33, 38));
    p.drawRoundedRect(lane, 4, 4);

    // Visible frame window via binary search on PTS.
    auto lo = std::lower_bound(m_frames.begin(), m_frames.end(), m_viewStartMs,
                               [](const FramePic& f, qint64 v) { return f.ptsMs < v; });
    if (lo != m_frames.begin())
        --lo;
    const int baseY = lane.bottom() - 2;
    const int H = lane.height() - 4;
    const bool wide = (double(lane.width()) / std::max(1, int((span) / 33)) > 6.0); // ~labels when frames are fat

    for (auto it = lo; it != m_frames.end(); ++it) {
        if (it->ptsMs > m_viewEndMs)
            break;
        const int x = xForTime(it->ptsMs);
        const int h = int(heightFrac(it->type) * H);
        p.setPen(QPen(colorFor(it->type), 1));
        p.drawLine(x, baseY, x, baseY - h);
        if (it->key) {
            // Open/closed GOP marker at the top.
            const QColor mc = it->closed ? QColor(80, 230, 140) : QColor(255, 170, 70);
            p.setPen(Qt::NoPen);
            p.setBrush(mc);
            p.drawPolygon(QPolygon({ QPoint(x - 3, lane.top() + 1), QPoint(x + 3, lane.top() + 1), QPoint(x, lane.top() + 7) }));
        }
        if (wide) {
            p.setPen(QColor(210, 215, 222));
            p.drawText(x - 3, baseY - h - 2, QString(QChar(it->type)));
        }
    }

    // Legend / footer.
    p.setPen(QColor(180, 186, 196));
    const int fy = height() - 5;
    int lx = kMargin;
    auto chip = [&](const QString& t, const QColor& c) {
        p.setPen(QPen(c, 3));
        p.drawLine(lx, fy - 4, lx + 14, fy - 4);
        p.setPen(QColor(190, 196, 205));
        p.drawText(lx + 18, fy, t);
        lx += 26 + p.fontMetrics().horizontalAdvance(t);
    };
    chip(QString("I %1").arg(m_nI), colorFor('I'));
    chip(QString("P %1").arg(m_nP), colorFor('P'));
    chip(QString("B %1").arg(m_nB), colorFor('B'));
    if (m_nU)
        chip(QString("? %1").arg(m_nU), colorFor('?'));
    p.setPen(QColor(150, 156, 166));
    p.drawText(QRect(kMargin, fy - 12, width() - 2 * kMargin, 14), Qt::AlignRight | Qt::AlignVCenter,
               QString("%1   ▲closed ▲open GOP   view %2 - %3")
                   .arg(m_codec).arg(fmtTime(m_viewStartMs)).arg(fmtTime(m_viewEndMs)));
}
