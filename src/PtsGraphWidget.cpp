#include "PtsGraphWidget.h"

#include <QPainter>
#include <algorithm>

namespace {
constexpr int kMargin = 12;
constexpr int kAxisL = 56; // left gutter for y labels
constexpr int kAxisB = 22; // bottom gutter for x labels
constexpr int kLegendH = 20;

const QColor kVidPts(90, 200, 255);
const QColor kVidDts(110, 140, 255);
const QColor kAudPts(120, 215, 140);
const QColor kPcr(240, 160, 70);

QString fmtTime(qint64 ms)
{
    const qint64 t = ms / 1000;
    const int h = int(t / 3600), m = int((t % 3600) / 60), s = int(t % 60);
    if (h > 0)
        return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}
QString fmtBytes(qint64 b)
{
    if (b >= (1LL << 30))
        return QString("%1G").arg(double(b) / (1LL << 30), 0, 'f', 1);
    return QString("%1M").arg(double(b) / (1LL << 20), 0, 'f', 0);
}
qint64 niceStep(qint64 target)
{
    static const qint64 steps[] = { 1000, 2000, 5000, 10000, 20000, 30000, 60000,
                                    120000, 300000, 600000, 1800000, 3600000 };
    for (qint64 s : steps)
        if (s >= target)
            return s;
    return 3600000;
}
} // namespace

PtsGraphWidget::PtsGraphWidget(QWidget* parent)
    : QWidget(parent)
{
}

void PtsGraphWidget::setData(const TsScanResult& r)
{
    m_video = r.videoPts;
    m_audio = r.audioPts;
    m_pcr = r.pcr;
    m_fileSize = r.fileSize;
    m_durationMs = std::max<qint64>(1, r.durationMs);
    m_hasDts = std::any_of(m_video.begin(), m_video.end(), [](const PesPoint& p) { return p.dtsMs >= 0; });
    update();
}

QSize PtsGraphWidget::minimumSizeHint() const
{
    return QSize(480, 240);
}

QRect PtsGraphWidget::plotRect() const
{
    return QRect(kMargin + kAxisL, kMargin + kLegendH,
                 width() - 2 * kMargin - kAxisL,
                 height() - 2 * kMargin - kLegendH - kAxisB);
}

int PtsGraphWidget::xForByte(qint64 b) const
{
    const QRect pr = plotRect();
    const qint64 fs = std::max<qint64>(1, m_fileSize);
    return pr.left() + int(double(b) / double(fs) * pr.width());
}
int PtsGraphWidget::yForMs(qint64 ms) const
{
    const QRect pr = plotRect();
    return pr.bottom() - int(double(ms) / double(m_durationMs) * pr.height());
}

template <typename Pt, typename GetMs>
void PtsGraphWidget::drawSeries(QPainter& p, const QVector<Pt>& pts, GetMs getMs, const QColor& c)
{
    if (pts.isEmpty())
        return;
    const QRect pr = plotRect();
    // Downsample so we never draw more than ~2 points per pixel column.
    const int stride = std::max(1, int(pts.size() / std::max(1, pr.width() * 2)));
    p.setPen(QPen(c, 1));
    QPoint prev;
    bool have = false;
    for (int i = 0; i < pts.size(); i += stride) {
        const qint64 ms = getMs(pts[i]);
        if (ms < 0)
            continue;
        const QPoint cur(xForByte(pts[i].byte), yForMs(ms));
        if (have)
            p.drawLine(prev, cur);
        prev = cur;
        have = true;
    }
}

void PtsGraphWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(22, 24, 28));
    const QRect pr = plotRect();
    if (m_video.isEmpty() || pr.width() <= 0 || pr.height() <= 0) {
        p.setPen(QColor(225, 229, 235));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Open a TS file"));
        return;
    }

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(30, 33, 38));
    p.drawRect(pr);

    // Grid + y labels (time).
    p.setPen(QColor(150, 158, 170));
    const qint64 ystep = niceStep(std::max<qint64>(1000, m_durationMs * 40 / std::max(1, pr.height())));
    for (qint64 t = 0; t <= m_durationMs; t += ystep) {
        const int y = yForMs(t);
        p.setPen(QColor(50, 55, 62));
        p.drawLine(pr.left(), y, pr.right(), y);
        p.setPen(QColor(150, 158, 170));
        p.drawText(QRect(0, y - 8, kMargin + kAxisL - 4, 16), Qt::AlignRight | Qt::AlignVCenter, fmtTime(t));
    }
    // x labels (byte position).
    for (int i = 0; i <= 8; ++i) {
        const qint64 b = m_fileSize * i / 8;
        const int x = xForByte(b);
        p.setPen(QColor(50, 55, 62));
        p.drawLine(x, pr.top(), x, pr.bottom());
        p.setPen(QColor(150, 158, 170));
        p.drawText(QRect(x - 30, pr.bottom() + 2, 60, kAxisB - 2), Qt::AlignHCenter | Qt::AlignTop, fmtBytes(b));
    }

    drawSeries(p, m_pcr, [](const PcrPoint& s) { return s.pcrMs; }, kPcr);
    drawSeries(p, m_audio, [](const PesPoint& s) { return s.ptsMs; }, kAudPts);
    if (m_hasDts)
        drawSeries(p, m_video, [](const PesPoint& s) { return s.dtsMs; }, kVidDts);
    drawSeries(p, m_video, [](const PesPoint& s) { return s.ptsMs; }, kVidPts);

    // Discontinuity_indicator markers on the PCR series.
    p.setPen(QPen(QColor(255, 90, 90), 1));
    for (const PcrPoint& s : m_pcr)
        if (s.discontinuity) {
            const int x = xForByte(s.byte);
            p.drawLine(x, pr.top(), x, pr.bottom());
        }

    // Legend.
    struct L { const char* t; QColor c; bool on; };
    const L items[] = {
        { "video PTS", kVidPts, true },
        { "video DTS", kVidDts, m_hasDts },
        { "audio PTS", kAudPts, !m_audio.isEmpty() },
        { "PCR", kPcr, !m_pcr.isEmpty() },
        { "discontinuity", QColor(255, 90, 90), true },
    };
    int lx = kMargin + kAxisL;
    for (const L& it : items) {
        if (!it.on)
            continue;
        p.setPen(QPen(it.c, 2));
        p.drawLine(lx, kMargin + kLegendH / 2, lx + 18, kMargin + kLegendH / 2);
        p.setPen(QColor(200, 205, 212));
        const QString t = QString::fromLatin1(it.t);
        p.drawText(lx + 22, kMargin + kLegendH / 2 + 4, t);
        lx += 32 + p.fontMetrics().horizontalAdvance(t);
    }
}
