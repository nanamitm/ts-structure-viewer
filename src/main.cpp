#include "StructureViewer.h"

#include <QApplication>
#include <QMainWindow>
#include <QStatusBar>
#include <QtGlobal>
#include <random>

// Synthetic data driver for the prototype: a ~10 min program with jittered
// GOP keyframes and two smart-rendering cuts (frame-accurate IN, lead-in +
// tail re-encode windows). Replace with real TsSourceIndex / planSegments data
// when wiring into ts-edit-gui.
static QVector<qint64> makeRaps(qint64 durationMs)
{
    QVector<qint64> raps;
    std::mt19937 rng(1234);
    std::uniform_int_distribution<int> jitter(-180, 220);
    qint64 t = 0;
    while (t < durationMs) {
        raps.push_back(t);
        t += 775 + jitter(rng); // ~775 ms average GOP, like the YuruYuri sample
    }
    return raps;
}

// Snap a time up to the first keyframe >= t.
static qint64 snapUp(const QVector<qint64>& raps, qint64 t)
{
    for (qint64 r : raps)
        if (r >= t)
            return r;
    return raps.isEmpty() ? t : raps.last();
}

static StructureViewer::PlanSeg makeSeg(const QVector<qint64>& raps, qint64 inMs, qint64 outMs)
{
    StructureViewer::PlanSeg s;
    s.inMs = inMs;
    s.outMs = outMs;
    s.copyStartMs = snapUp(raps, inMs);
    // copyEnd snaps to the keyframe at/just before OUT.
    qint64 ke = inMs;
    for (qint64 r : raps)
        if (r <= outMs)
            ke = r;
    s.copyEndMs = ke;
    s.hasLeadIn = s.copyStartMs > s.inMs;
    s.hasTail = s.copyEndMs < s.outMs;
    return s;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    const qint64 durationMs = 600000;
    const QVector<qint64> raps = makeRaps(durationMs);

    QMainWindow win;
    win.setWindowTitle(QStringLiteral("TS Structure Viewer (prototype)"));

    auto* viewer = new StructureViewer(&win);
    viewer->setDuration(durationMs);
    viewer->setRapTimes(raps);
    viewer->setPlan({
        makeSeg(raps, 60100, 90100),
        makeSeg(raps, 300100, 330100),
    });
    viewer->setPlayhead(60100);

    // Test hook: TSV_VIEW="startMs,endMs" opens zoomed to that range.
    if (const QByteArray v = qgetenv("TSV_VIEW"); !v.isEmpty()) {
        const auto parts = QString::fromLatin1(v).split(',');
        if (parts.size() == 2)
            viewer->setView(parts[0].toLongLong(), parts[1].toLongLong());
    }

    QObject::connect(viewer, &StructureViewer::seekRequested, &win, [&win](qint64 ms) {
        win.statusBar()->showMessage(QString("seek -> %1 ms").arg(ms), 2000);
    });

    win.setCentralWidget(viewer);
    win.statusBar()->showMessage(
        QStringLiteral("wheel: zoom  |  drag: pan  |  click: seek  |  double-click: fit"));
    win.resize(1100, 320);
    win.show();
    return app.exec();
}
