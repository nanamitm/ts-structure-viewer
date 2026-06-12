#include "CompareViewer.h"

#include <QApplication>
#include <QMainWindow>
#include <QStatusBar>
#include <random>

// Synthetic source-vs-export round trip for the comparison prototype.
// Mirrors TsSmartExporter::planSegments / the export assembly: each EDL cut snaps
// to GOP boundaries (copy region), with optional lead-in / tail re-encode windows
// for a frame-accurate IN/OUT, then the kept runs are concatenated into the
// rebased output timeline. Replace with two real TsSourceIndex scans + the plan
// when wiring into ts-edit-gui.
namespace {
constexpr qint64 kThreshMs = 20;

QVector<qint64> makeRaps(qint64 durationMs)
{
    QVector<qint64> raps;
    std::mt19937 rng(1234);
    std::uniform_int_distribution<int> jitter(-180, 220);
    for (qint64 t = 0; t < durationMs; t += 775 + jitter(rng))
        raps.push_back(t);
    return raps;
}

qint64 rapAtOrAfter(const QVector<qint64>& r, qint64 t)
{
    for (qint64 v : r)
        if (v >= t)
            return v;
    return -1;
}
qint64 rapAtOrBefore(const QVector<qint64>& r, qint64 t)
{
    qint64 best = -1;
    for (qint64 v : r) {
        if (v <= t)
            best = v;
        else
            break;
    }
    return best;
}

struct Cut {
    qint64 inMs, outMs;
};

// Build the export piece list (and total output duration) from the source RAPs
// and the cuts, matching the exporter's lead-in / copy / tail decomposition.
QVector<CompareViewer::OutPiece> assemble(const QVector<qint64>& raps, qint64 srcDuration,
                                          const QVector<Cut>& cuts, qint64& outDurationOut)
{
    using Piece = CompareViewer::OutPiece;
    QVector<Piece> out;
    qint64 cursor = 0;
    for (const Cut& c : cuts) {
        const qint64 copyStart = rapAtOrAfter(raps, c.inMs);
        if (copyStart < 0)
            continue;
        const qint64 kb = rapAtOrBefore(raps, c.outMs);
        const qint64 ka = rapAtOrAfter(raps, c.outMs);
        const bool needTail = kb > copyStart && (c.outMs - kb) > kThreshMs;
        const qint64 copyEnd = needTail ? kb : (ka > copyStart ? ka : srcDuration);
        const bool needLead = (copyStart - c.inMs) > kThreshMs && c.inMs > 0;
        const qint64 inExact = needLead ? c.inMs : copyStart;
        const qint64 outExact = needTail ? c.outMs : copyEnd;
        bool firstOfCut = true;

        auto push = [&](qint64 s, qint64 e, CompareViewer::PieceKind k) {
            if (e <= s)
                return;
            Piece pc;
            pc.srcStartMs = s;
            pc.srcEndMs = e;
            pc.outStartMs = cursor;
            pc.outEndMs = cursor + (e - s);
            pc.kind = k;
            pc.seamBefore = firstOfCut && !out.isEmpty();
            firstOfCut = false;
            cursor = pc.outEndMs;
            out.push_back(pc);
        };

        if (needLead)
            push(inExact, copyStart, CompareViewer::PieceKind::Reencode);
        push(copyStart, copyEnd, CompareViewer::PieceKind::Copy);
        if (needTail)
            push(copyEnd, outExact, CompareViewer::PieceKind::Reencode);
    }
    outDurationOut = cursor;
    return out;
}
} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    const qint64 srcDuration = 600000;
    const QVector<qint64> raps = makeRaps(srcDuration);
    const QVector<Cut> cuts = { { 60100, 90100 }, { 300100, 330100 } };

    qint64 outDuration = 0;
    const auto pieces = assemble(raps, srcDuration, cuts, outDuration);

    QMainWindow win;
    win.setWindowTitle(QStringLiteral("TS Compare Viewer (prototype): source vs export"));

    auto* viewer = new CompareViewer(&win);
    CompareViewer::Source src;
    src.label = QStringLiteral("A: source  (600s, GOP ~775ms)");
    src.durationMs = srcDuration;
    src.rapMs = raps;
    viewer->setSource(src);
    viewer->setOutput(pieces, outDuration);

    QObject::connect(viewer, &CompareViewer::seekRequested, &win, [&win](qint64 ms) {
        win.statusBar()->showMessage(QString("seek source -> %1 ms").arg(ms), 2000);
    });

    if (const QByteArray v = qgetenv("TSV_VIEW"); !v.isEmpty()) {
        const auto parts = QString::fromLatin1(v).split(',');
        if (parts.size() == 2)
            viewer->setView(parts[0].toLongLong(), parts[1].toLongLong());
    }

    win.setCentralWidget(viewer);
    win.statusBar()->showMessage(
        QStringLiteral("wheel: zoom  |  drag: pan  |  click: seek  |  double-click: fit"));
    win.resize(1180, 360);
    win.show();
    return app.exec();
}
