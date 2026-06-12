#pragma once

#include "TsScan.h"

#include <QRect>
#include <QVector>
#include <QWidget>

// Plots PES PTS/DTS and PCR against file byte position to expose continuity
// problems at seams: a backward jump or gap shows as a dip or step in the line.
// x = byte offset (0..fileSize), y = relative ms. Video PTS/DTS, the first audio
// PTS and PCR are drawn as separate series with a legend.
class PtsGraphWidget : public QWidget {
    Q_OBJECT

public:
    explicit PtsGraphWidget(QWidget* parent = nullptr);
    void setData(const TsScanResult& r);

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize minimumSizeHint() const override;

private:
    QRect plotRect() const;
    int xForByte(qint64 b) const;
    int yForMs(qint64 ms) const;
    template <typename Pt, typename GetMs>
    void drawSeries(QPainter& p, const QVector<Pt>& pts, GetMs getMs, const QColor& c);

    QVector<PesPoint> m_video;
    QVector<PesPoint> m_audio;
    QVector<PcrPoint> m_pcr;
    qint64 m_fileSize = 0;
    qint64 m_durationMs = 0;
    bool m_hasDts = false;
};
