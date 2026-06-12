#include "PicTypeWidget.h"
#include "PtsGraphWidget.h"
#include "StructureViewer.h"
#include "TsScan.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QProgressDialog>
#include <QSet>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

// Real-file TS inspector. Open one file (A) to inspect its structure (GOP/RAP),
// elementary streams (PSI), PTS/DTS/PCR timing and per-frame picture types.
// Open a second file (B, an export of A) to switch into compare mode: the
// Streams tab becomes a PID/PSI diff (did captions/audio survive the cut?) and
// the Timing tab stacks A over B (the export's seams stand out). The Structure
// and Pictures tabs always show A.
namespace {
int captionCount(const TsScanResult& r)
{
    int n = 0;
    for (const auto& s : r.streams)
        if (s.kind == "caption" || s.kind == "superimpose")
            ++n;
    return n;
}
int audioCount(const TsScanResult& r)
{
    int n = 0;
    for (const auto& s : r.streams)
        if (s.kind == "audio")
            ++n;
    return n;
}
const TsStreamInfo* find(const TsScanResult& r, int pid)
{
    for (const auto& s : r.streams)
        if (s.pid == pid)
            return &s;
    return nullptr;
}
} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ts-inspector"));

    QMainWindow win;
    win.setWindowTitle(QStringLiteral("TS Inspector"));

    auto* tabs = new QTabWidget(&win);
    auto* structure = new StructureViewer(&win);
    auto* pics = new PicTypeWidget(&win);

    // Streams tab (single table -> diff table when B is loaded).
    auto* streamsPane = new QWidget(&win);
    auto* streamsLayout = new QVBoxLayout(streamsPane);
    auto* summary = new QLabel(QStringLiteral("Open a TS file (File > Open)"), streamsPane);
    summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    summary->setWordWrap(true);
    auto* table = new QTableWidget(0, 5, streamsPane);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    streamsLayout->addWidget(summary);
    streamsLayout->addWidget(table, 1);

    // Timing tab (graph A, plus graph B when comparing).
    auto* timingPane = new QWidget(&win);
    auto* timingLayout = new QVBoxLayout(timingPane);
    auto* labA = new QLabel(QStringLiteral("A"), timingPane);
    auto* graphA = new PtsGraphWidget(timingPane);
    auto* labB = new QLabel(QStringLiteral("B: export"), timingPane);
    auto* graphB = new PtsGraphWidget(timingPane);
    timingLayout->addWidget(labA);
    timingLayout->addWidget(graphA, 1);
    timingLayout->addWidget(labB);
    timingLayout->addWidget(graphB, 1);
    labB->setVisible(false);
    graphB->setVisible(false);

    tabs->addTab(structure, "Structure");
    tabs->addTab(streamsPane, "Streams");
    tabs->addTab(timingPane, "Timing");
    tabs->addTab(pics, "Pictures");
    win.setCentralWidget(tabs);

    TsScanResult ra, rb;
    QString pathA, pathB;
    bool hasB = false;

    auto buildStreamsSingle = [&] {
        table->setColumnCount(5);
        table->setHorizontalHeaderLabels({ "PID", "Kind", "Codec", "Lang", "PCR" });
        table->setRowCount(ra.streams.size());
        for (int i = 0; i < ra.streams.size(); ++i) {
            const TsStreamInfo& s = ra.streams[i];
            auto set = [&](int c, const QString& t) { table->setItem(i, c, new QTableWidgetItem(t)); };
            set(0, QString("0x%1").arg(s.pid, 4, 16, QChar('0')));
            set(1, s.kind);
            set(2, s.codec);
            set(3, s.language);
            set(4, s.isPcr ? QStringLiteral("●") : QString());
        }
        table->resizeColumnsToContents();

        qint64 ccTotal = 0;
        for (int v : ra.ccErrors)
            ccTotal += v;
        summary->setText(QString("program %1   PMT 0x%2   PCR_PID 0x%3   video 0x%4   "
                                 "duration %5 ms   RAP %6   CC errors %7")
                             .arg(ra.programNumber)
                             .arg(ra.pmtPid, 4, 16, QChar('0'))
                             .arg(ra.pcrPid, 4, 16, QChar('0'))
                             .arg(ra.videoPid, 4, 16, QChar('0'))
                             .arg(ra.durationMs).arg(ra.rapMs.size()).arg(ccTotal));
    };

    auto buildStreamsDiff = [&] {
        table->setColumnCount(6);
        table->setHorizontalHeaderLabels({ "PID", "A kind", "A codec", "B kind", "B codec", "Status" });
        QSet<int> pids;
        for (const auto& s : ra.streams) pids.insert(s.pid);
        for (const auto& s : rb.streams) pids.insert(s.pid);
        QList<int> sorted = pids.values();
        std::sort(sorted.begin(), sorted.end());
        table->setRowCount(sorted.size());
        for (int i = 0; i < sorted.size(); ++i) {
            const int pid = sorted[i];
            const TsStreamInfo* a = find(ra, pid);
            const TsStreamInfo* b = find(rb, pid);
            QString status;
            QColor color;
            if (a && b) {
                const bool same = a->kind == b->kind && a->codec == b->codec;
                status = same ? "same" : "changed";
                color = same ? QColor(150, 156, 166) : QColor(230, 200, 90);
            } else if (a) {
                status = "dropped (A only)";
                color = QColor(235, 110, 110);
            } else {
                status = "added (B only)";
                color = QColor(110, 210, 140);
            }
            auto cell = [&](int c, const QString& t) {
                auto* it = new QTableWidgetItem(t);
                it->setForeground(color);
                table->setItem(i, c, it);
            };
            cell(0, QString("0x%1").arg(pid, 4, 16, QChar('0')));
            cell(1, a ? a->kind : QString("-"));
            cell(2, a ? a->codec : QString("-"));
            cell(3, b ? b->kind : QString("-"));
            cell(4, b ? b->codec : QString("-"));
            cell(5, status);
        }
        table->resizeColumnsToContents();

        const int capA = captionCount(ra), capB = captionCount(rb);
        const int audA = audioCount(ra), audB = audioCount(rb);
        const auto seams = std::count_if(rb.pcr.begin(), rb.pcr.end(),
                                         [](const PcrPoint& p) { return p.discontinuity; });
        summary->setText(
            QString("A %1 (%2 ms, %3 streams, RAP %4)   B %5 (%6 ms, %7 streams, RAP %8)\n"
                    "captions A=%9 B=%10 %11   audio A=%12 B=%13 %14   B seams (PCR discontinuities): %15")
                .arg(QFileInfo(pathA).fileName()).arg(ra.durationMs).arg(ra.streams.size()).arg(ra.rapMs.size())
                .arg(QFileInfo(pathB).fileName()).arg(rb.durationMs).arg(rb.streams.size()).arg(rb.rapMs.size())
                .arg(capA).arg(capB).arg(capB >= capA && capA > 0 ? "OK" : (capA == 0 ? "-" : "LOST"))
                .arg(audA).arg(audB).arg(audB == audA ? "OK" : (audB < audA ? "DROPPED" : "+"))
                .arg(seams));
    };

    auto refresh = [&] {
        if (!ra.ok)
            return;
        // Structure + Pictures always show A.
        structure->setDuration(ra.durationMs);
        structure->setRapTimes(ra.rapMs);
        structure->setPlan({});
        pics->setFrames(ra.frames, ra.durationMs, ra.videoCodec);
        graphA->setData(ra);
        labA->setText(hasB ? QString("A: source - %1").arg(QFileInfo(pathA).fileName())
                           : QString("A - %1").arg(QFileInfo(pathA).fileName()));

        if (hasB) {
            graphB->setData(rb);
            labB->setText(QString("B: export - %1").arg(QFileInfo(pathB).fileName()));
            buildStreamsDiff();
        } else {
            buildStreamsSingle();
        }
        labB->setVisible(hasB);
        graphB->setVisible(hasB);

        // Test hook: TSV_VIEW="startMs,endMs" opens the structure/picture views zoomed.
        if (const QByteArray v = qgetenv("TSV_VIEW"); !v.isEmpty()) {
            const auto parts = QString::fromLatin1(v).split(',');
            if (parts.size() == 2) {
                structure->setView(parts[0].toLongLong(), parts[1].toLongLong());
                pics->setView(parts[0].toLongLong(), parts[1].toLongLong());
            }
        }
        win.setWindowTitle(hasB
            ? QString("TS Inspector - %1  vs  %2").arg(QFileInfo(pathA).fileName(), QFileInfo(pathB).fileName())
            : QString("TS Inspector - %1").arg(QFileInfo(pathA).fileName()));
    };

    auto scan = [&](const QString& path, TsScanResult& out) {
        QProgressDialog dlg(QString("Scanning %1...").arg(QFileInfo(path).fileName()),
                            "Cancel", 0, 1000, &win);
        dlg.setWindowModality(Qt::WindowModal);
        dlg.setMinimumDuration(0);
        out = TsScan::scanFile(path, [&](qint64 d, qint64 t) {
            dlg.setValue(t > 0 ? int(d * 1000 / t) : 0);
            QApplication::processEvents();
            return !dlg.wasCanceled();
        });
        if (!out.ok)
            win.statusBar()->showMessage(QString("Scan failed: %1").arg(out.error), 6000);
        return out.ok;
    };

    auto openA = [&](const QString& path) {
        if (!scan(path, ra))
            return;
        pathA = path;
        rb = TsScanResult{};   // a new source clears the comparison
        hasB = false;
        refresh();
    };
    auto openB = [&](const QString& path) {
        if (!ra.ok) {
            win.statusBar()->showMessage(QStringLiteral("Open a source (A) first"), 5000);
            return;
        }
        if (!scan(path, rb))
            return;
        pathB = path;
        hasB = true;
        refresh();
    };

    auto* fileMenu = win.menuBar()->addMenu("&File");
    auto* openAct = fileMenu->addAction("&Open TS...");
    openAct->setShortcut(QKeySequence::Open);
    QObject::connect(openAct, &QAction::triggered, &win, [&] {
        const QString p = QFileDialog::getOpenFileName(&win, "Open TS (source)", QString(),
                                                       "MPEG-TS (*.ts *.m2ts);;All files (*)");
        if (!p.isEmpty())
            openA(p);
    });
    QObject::connect(fileMenu->addAction("Open &Export (B) to compare..."), &QAction::triggered, &win, [&] {
        const QString p = QFileDialog::getOpenFileName(&win, "Open export (B)", QString(),
                                                       "MPEG-TS (*.ts *.m2ts);;All files (*)");
        if (!p.isEmpty())
            openB(p);
    });

    win.resize(1180, 460);
    win.show();
    if (argc >= 2)
        openA(QString::fromLocal8Bit(argv[1]));
    if (argc >= 3)
        openB(QString::fromLocal8Bit(argv[2]));
    return app.exec();
}
