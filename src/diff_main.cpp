#include "PtsGraphWidget.h"
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

// Two-file TS diff for smart-render verification: scans a source (A) and an
// export (B) and shows a PID/PSI diff (did captions / audio survive the cut?)
// plus both timing graphs stacked, where B's discontinuity markers flag the
// seams. Reuses TsScan + PtsGraphWidget.
namespace {
bool scan(const QString& path, TsScanResult& out, QWidget* parent)
{
    QProgressDialog dlg(QString("Scanning %1...").arg(QFileInfo(path).fileName()),
                        "Cancel", 0, 1000, parent);
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setMinimumDuration(0);
    out = TsScan::scanFile(path, [&](qint64 d, qint64 t) {
        dlg.setValue(t > 0 ? int(d * 1000 / t) : 0);
        QApplication::processEvents();
        return !dlg.wasCanceled();
    });
    return out.ok;
}

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
    QApplication::setApplicationName(QStringLiteral("ts-diff"));

    QMainWindow win;
    win.setWindowTitle(QStringLiteral("TS Diff (source vs export)"));

    auto* tabs = new QTabWidget(&win);

    // Streams-diff tab.
    auto* diffPane = new QWidget(&win);
    auto* diffLayout = new QVBoxLayout(diffPane);
    auto* summary = new QLabel(QStringLiteral("Open a source (A) and an export (B)"), diffPane);
    summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    summary->setWordWrap(true);
    auto* table = new QTableWidget(0, 6, diffPane);
    table->setHorizontalHeaderLabels({ "PID", "A kind", "A codec", "B kind", "B codec", "Status" });
    table->horizontalHeader()->setStretchLastSection(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    diffLayout->addWidget(summary);
    diffLayout->addWidget(table, 1);

    // Timing tab: A over B.
    auto* timingPane = new QWidget(&win);
    auto* timingLayout = new QVBoxLayout(timingPane);
    auto* labA = new QLabel(QStringLiteral("A: source"), timingPane);
    auto* graphA = new PtsGraphWidget(timingPane);
    auto* labB = new QLabel(QStringLiteral("B: export"), timingPane);
    auto* graphB = new PtsGraphWidget(timingPane);
    timingLayout->addWidget(labA);
    timingLayout->addWidget(graphA, 1);
    timingLayout->addWidget(labB);
    timingLayout->addWidget(graphB, 1);

    tabs->addTab(diffPane, "Streams diff");
    tabs->addTab(timingPane, "Timing");
    win.setCentralWidget(tabs);

    TsScanResult ra, rb;
    QString pathA, pathB;

    auto refresh = [&] {
        if (!ra.ok || !rb.ok)
            return;
        graphA->setData(ra);
        graphB->setData(rb);
        labA->setText(QString("A: source - %1").arg(QFileInfo(pathA).fileName()));
        labB->setText(QString("B: export - %1").arg(QFileInfo(pathB).fileName()));

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
            } else if (a && !b) {
                status = "dropped (A only)";
                color = QColor(235, 110, 110);
            } else {
                status = "added (B only)";
                color = QColor(110, 210, 140);
            }
            auto cell = [&](int col, const QString& t) {
                auto* it = new QTableWidgetItem(t);
                it->setForeground(color);
                table->setItem(i, col, it);
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
        summary->setText(
            QString("A %1 (%2 ms, %3 streams, RAP %4)   B %5 (%6 ms, %7 streams, RAP %8)\n"
                    "captions A=%9 B=%10 %11   audio A=%12 B=%13 %14   "
                    "B PCR discontinuities (seams): %15")
                .arg(QFileInfo(pathA).fileName()).arg(ra.durationMs).arg(ra.streams.size()).arg(ra.rapMs.size())
                .arg(QFileInfo(pathB).fileName()).arg(rb.durationMs).arg(rb.streams.size()).arg(rb.rapMs.size())
                .arg(capA).arg(capB).arg(capB >= capA && capA > 0 ? "OK" : (capA == 0 ? "-" : "LOST"))
                .arg(audA).arg(audB).arg(audB == audA ? "OK" : (audB < audA ? "DROPPED" : "+"))
                .arg(std::count_if(rb.pcr.begin(), rb.pcr.end(), [](const PcrPoint& p) { return p.discontinuity; })));
        win.setWindowTitle(QString("TS Diff - A:%1  B:%2")
                               .arg(QFileInfo(pathA).fileName(), QFileInfo(pathB).fileName()));
    };

    auto* fileMenu = win.menuBar()->addMenu("&File");
    QObject::connect(fileMenu->addAction("Open &Source (A)..."), &QAction::triggered, &win, [&] {
        const QString p = QFileDialog::getOpenFileName(&win, "Open source (A)", QString(), "MPEG-TS (*.ts *.m2ts)");
        if (!p.isEmpty() && scan(p, ra, &win)) { pathA = p; refresh(); }
    });
    QObject::connect(fileMenu->addAction("Open &Export (B)..."), &QAction::triggered, &win, [&] {
        const QString p = QFileDialog::getOpenFileName(&win, "Open export (B)", QString(), "MPEG-TS (*.ts *.m2ts)");
        if (!p.isEmpty() && scan(p, rb, &win)) { pathB = p; refresh(); }
    });

    win.resize(1180, 560);
    win.show();
    if (argc >= 3) {
        if (scan(QString::fromLocal8Bit(argv[1]), ra, &win)) pathA = QString::fromLocal8Bit(argv[1]);
        if (scan(QString::fromLocal8Bit(argv[2]), rb, &win)) pathB = QString::fromLocal8Bit(argv[2]);
        refresh();
    }
    return app.exec();
}
