#include "PicTypeWidget.h"
#include "PtsGraphWidget.h"
#include "StructureViewer.h"
#include "TsScan.h"

#include <QApplication>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QProgressDialog>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

// Real-file TS inspector: scans a .ts with TsScan and shows its structure (GOP/
// RAP), elementary streams (PSI) and PTS/DTS/PCR timing in tabs.
int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ts-inspector"));

    QMainWindow win;
    win.setWindowTitle(QStringLiteral("TS Inspector"));

    auto* tabs = new QTabWidget(&win);
    auto* structure = new StructureViewer(&win);

    auto* streamsPane = new QWidget(&win);
    auto* streamsLayout = new QVBoxLayout(streamsPane);
    auto* summary = new QLabel(QStringLiteral("Open a TS file"), streamsPane);
    summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* table = new QTableWidget(0, 5, streamsPane);
    table->setHorizontalHeaderLabels({ "PID", "Kind", "Codec", "Lang", "PCR" });
    table->horizontalHeader()->setStretchLastSection(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    streamsLayout->addWidget(summary);
    streamsLayout->addWidget(table, 1);

    auto* graph = new PtsGraphWidget(&win);
    auto* pics = new PicTypeWidget(&win);

    tabs->addTab(structure, "Structure");
    tabs->addTab(streamsPane, "Streams");
    tabs->addTab(graph, "Timing");
    tabs->addTab(pics, "Pictures");
    win.setCentralWidget(tabs);

    auto loadFile = [&](const QString& path) {
        QProgressDialog dlg(QString("Scanning %1...").arg(QFileInfo(path).fileName()),
                            "Cancel", 0, 1000, &win);
        dlg.setWindowModality(Qt::WindowModal);
        dlg.setMinimumDuration(0);
        const auto progress = [&](qint64 done, qint64 total) {
            dlg.setValue(total > 0 ? int(done * 1000 / total) : 0);
            QApplication::processEvents();
            return !dlg.wasCanceled();
        };
        const TsScanResult r = TsScan::scanFile(path, progress);
        dlg.close();
        if (!r.ok) {
            win.statusBar()->showMessage(QString("Scan failed: %1").arg(r.error), 6000);
            return;
        }

        structure->setDuration(r.durationMs);
        structure->setRapTimes(r.rapMs);
        structure->setPlan({}); // single file: no cut plan, just GOP structure
        graph->setData(r);
        pics->setFrames(r.frames, r.durationMs, r.videoCodec);

        table->setRowCount(r.streams.size());
        for (int i = 0; i < r.streams.size(); ++i) {
            const TsStreamInfo& s = r.streams[i];
            auto set = [&](int col, const QString& t) {
                table->setItem(i, col, new QTableWidgetItem(t));
            };
            set(0, QString("0x%1").arg(s.pid, 4, 16, QChar('0')));
            set(1, s.kind);
            set(2, s.codec);
            set(3, s.language);
            set(4, s.isPcr ? QStringLiteral("●") : QString());
        }
        table->resizeColumnsToContents();

        qint64 ccTotal = 0;
        for (int v : r.ccErrors)
            ccTotal += v;
        summary->setText(QString("program %1   PMT 0x%2   PCR_PID 0x%3   video 0x%4   "
                                 "duration %5   RAP %6   CC errors %7")
                             .arg(r.programNumber)
                             .arg(r.pmtPid, 4, 16, QChar('0'))
                             .arg(r.pcrPid, 4, 16, QChar('0'))
                             .arg(r.videoPid, 4, 16, QChar('0'))
                             .arg(QString::number(r.durationMs) + " ms")
                             .arg(r.rapMs.size())
                             .arg(ccTotal));
        // Test hook: TSV_VIEW="startMs,endMs" opens the structure/picture views zoomed.
        if (const QByteArray v = qgetenv("TSV_VIEW"); !v.isEmpty()) {
            const auto parts = QString::fromLatin1(v).split(',');
            if (parts.size() == 2) {
                structure->setView(parts[0].toLongLong(), parts[1].toLongLong());
                pics->setView(parts[0].toLongLong(), parts[1].toLongLong());
            }
        }
        win.setWindowTitle(QString("TS Inspector - %1").arg(QFileInfo(path).fileName()));
        win.statusBar()->showMessage(
            QString("Loaded %1 streams, %2 RAP, %3 PCR samples")
                .arg(r.streams.size()).arg(r.rapMs.size()).arg(r.pcr.size()), 5000);
    };

    auto* openAct = win.menuBar()->addMenu("&File")->addAction("&Open TS...");
    openAct->setShortcut(QKeySequence::Open);
    QObject::connect(openAct, &QAction::triggered, &win, [&] {
        const QString path = QFileDialog::getOpenFileName(&win, "Open TS", QString(),
                                                          "MPEG-TS (*.ts *.m2ts);;All files (*)");
        if (!path.isEmpty())
            loadFile(path);
    });

    win.resize(1180, 420);
    win.show();
    if (argc >= 2)
        loadFile(QString::fromLocal8Bit(argv[1]));
    return app.exec();
}
