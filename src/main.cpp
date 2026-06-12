#include "CompareMap.h"
#include "CompareViewer.h"
#include "PicTypeWidget.h"
#include "PtsGraphWidget.h"
#include "StructureViewer.h"
#include "TsScan.h"
#include "TsScanWorker.h"

#include <QApplication>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSaveFile>
#include <QSet>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QThread>
#include <QVBoxLayout>
#include <algorithm>

// Real-file TS structure viewer. Open one file (A) to inspect its structure (GOP/RAP),
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

QJsonArray streamsJson(const TsScanResult& r)
{
    QJsonArray a;
    for (const auto& s : r.streams) {
        QJsonObject o;
        o["pid"] = s.pid;
        o["pidHex"] = QString("0x%1").arg(s.pid, 4, 16, QChar('0'));
        o["streamType"] = s.streamType;
        o["streamTypeHex"] = QString("0x%1").arg(s.streamType, 2, 16, QChar('0'));
        o["kind"] = s.kind;
        o["codec"] = s.codec;
        o["language"] = s.language;
        o["pcr"] = s.isPcr;
        o["ccErrors"] = r.ccErrors.value(s.pid);
        a.push_back(o);
    }
    return a;
}

QJsonObject scanJson(const TsScanResult& r, const QString& path)
{
    qint64 ccTotal = 0;
    for (int v : r.ccErrors)
        ccTotal += v;

    QJsonArray pcrDiscontinuities;
    for (const auto& p : r.pcr) {
        if (!p.discontinuity)
            continue;
        QJsonObject o;
        o["byte"] = QString::number(p.byte);
        o["pcrMs"] = QString::number(p.pcrMs);
        pcrDiscontinuities.push_back(o);
    }

    QJsonArray ccErrorDetails;
    for (const auto& e : r.ccErrorPoints) {
        QJsonObject o;
        o["pid"] = e.pid;
        o["pidHex"] = QString("0x%1").arg(e.pid, 4, 16, QChar('0'));
        o["byte"] = QString::number(e.byte);
        o["expected"] = e.expected;
        o["actual"] = e.actual;
        ccErrorDetails.push_back(o);
    }

    QJsonObject o;
    o["path"] = path;
    o["fileName"] = QFileInfo(path).fileName();
    o["fileSize"] = QString::number(r.fileSize);
    o["programNumber"] = r.programNumber;
    o["pmtPid"] = r.pmtPid;
    o["pmtPidHex"] = QString("0x%1").arg(r.pmtPid, 4, 16, QChar('0'));
    o["pcrPid"] = r.pcrPid;
    o["pcrPidHex"] = QString("0x%1").arg(r.pcrPid, 4, 16, QChar('0'));
    o["videoPid"] = r.videoPid;
    o["videoPidHex"] = QString("0x%1").arg(r.videoPid, 4, 16, QChar('0'));
    o["audioPid"] = r.audioPid;
    o["durationMs"] = QString::number(r.durationMs);
    o["rapCount"] = r.rapMs.size();
    o["frameCount"] = r.frames.size();
    o["videoCodec"] = r.videoCodec;
    o["streamCount"] = r.streams.size();
    o["captionCount"] = captionCount(r);
    o["audioCount"] = audioCount(r);
    o["ccErrorTotal"] = QString::number(ccTotal);
    o["ccErrorDetails"] = ccErrorDetails;
    o["pcrDiscontinuityCount"] = pcrDiscontinuities.size();
    o["pcrDiscontinuities"] = pcrDiscontinuities;
    o["streams"] = streamsJson(r);
    return o;
}

QString htmlEsc(QString s)
{
    return s.toHtmlEscaped();
}

QString htmlScanTable(const TsScanResult& r)
{
    QString h = "<table><thead><tr><th>PID</th><th>Kind</th><th>Codec</th><th>Lang</th><th>PCR</th><th>CC errors</th></tr></thead><tbody>";
    for (const auto& s : r.streams) {
        h += QString("<tr><td>0x%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td><td>%6</td></tr>")
                 .arg(s.pid, 4, 16, QChar('0'))
                 .arg(htmlEsc(s.kind), htmlEsc(s.codec), htmlEsc(s.language), s.isPcr ? "yes" : "")
                 .arg(r.ccErrors.value(s.pid));
    }
    h += "</tbody></table>";
    return h;
}

QJsonObject reportJson(const TsScanResult& ra, const QString& pathA,
                       const TsScanResult& rb, const QString& pathB, bool hasB)
{
    QJsonObject root;
    root["tool"] = "ts-structure-viewer";
    root["formatVersion"] = 1;
    root["mode"] = hasB ? "compare" : "single";
    root["source"] = scanJson(ra, pathA);

    if (hasB) {
        qint64 outDur = 0;
        const auto pieces = buildComparePieces(ra, rb, outDur);
        qint64 copyMs = 0;
        qint64 reencodeMs = 0;
        QJsonArray pieceArray;
        for (const auto& p : pieces) {
            const qint64 dur = std::max<qint64>(0, p.outEndMs - p.outStartMs);
            if (p.kind == CompareViewer::PieceKind::Copy)
                copyMs += dur;
            else
                reencodeMs += dur;

            QJsonObject o;
            o["kind"] = p.kind == CompareViewer::PieceKind::Copy ? "copy" : "reencode";
            o["srcStartMs"] = QString::number(p.srcStartMs);
            o["srcEndMs"] = QString::number(p.srcEndMs);
            o["outStartMs"] = QString::number(p.outStartMs);
            o["outEndMs"] = QString::number(p.outEndMs);
            o["seamBefore"] = p.seamBefore;
            pieceArray.push_back(o);
        }

        QJsonObject cmp;
        cmp["export"] = scanJson(rb, pathB);
        cmp["captionStatus"] = captionCount(rb) >= captionCount(ra) && captionCount(ra) > 0 ? "OK" : (captionCount(ra) == 0 ? "-" : "LOST");
        cmp["audioStatus"] = audioCount(rb) == audioCount(ra) ? "OK" : (audioCount(rb) < audioCount(ra) ? "DROPPED" : "+");
        cmp["outputDurationMs"] = QString::number(outDur);
        cmp["copyMs"] = QString::number(copyMs);
        cmp["reencodeMs"] = QString::number(reencodeMs);
        cmp["copyPercent"] = outDur > 0 ? double(copyMs) * 100.0 / double(outDur) : 0.0;
        cmp["reencodePercent"] = outDur > 0 ? double(reencodeMs) * 100.0 / double(outDur) : 0.0;
        cmp["pieces"] = pieceArray;
        root["compare"] = cmp;
    }
    return root;
}

QString reportHtml(const TsScanResult& ra, const QString& pathA,
                   const TsScanResult& rb, const QString& pathB, bool hasB)
{
    QString h = "<!doctype html><meta charset=\"utf-8\"><title>TS Structure Report</title>"
                "<style>body{font:14px/1.45 Segoe UI,Arial,sans-serif;margin:24px;color:#202124}"
                "h1,h2{margin:.6em 0 .3em}table{border-collapse:collapse;margin:12px 0 20px;width:100%}"
                "th,td{border:1px solid #d0d7de;padding:6px 8px;text-align:left}th{background:#f6f8fa}"
                ".ok{color:#147d3f}.warn{color:#9a6700}.bad{color:#b42318}</style>";
    h += "<h1>TS Structure Report</h1>";
    h += QString("<p><b>Mode:</b> %1</p>").arg(hasB ? "compare" : "single");
    h += QString("<h2>Source</h2><p><b>%1</b><br>duration %2 ms / RAP %3 / streams %4 / captions %5 / audio %6</p>")
             .arg(htmlEsc(pathA)).arg(ra.durationMs).arg(ra.rapMs.size()).arg(ra.streams.size())
             .arg(captionCount(ra)).arg(audioCount(ra));
    h += htmlScanTable(ra);

    if (hasB) {
        qint64 outDur = 0;
        const auto pieces = buildComparePieces(ra, rb, outDur);
        qint64 copyMs = 0;
        qint64 reencodeMs = 0;
        for (const auto& p : pieces) {
            const qint64 dur = std::max<qint64>(0, p.outEndMs - p.outStartMs);
            if (p.kind == CompareViewer::PieceKind::Copy)
                copyMs += dur;
            else
                reencodeMs += dur;
        }
        const QString capStatus = captionCount(rb) >= captionCount(ra) && captionCount(ra) > 0 ? "OK" : (captionCount(ra) == 0 ? "-" : "LOST");
        const QString audStatus = audioCount(rb) == audioCount(ra) ? "OK" : (audioCount(rb) < audioCount(ra) ? "DROPPED" : "+");
        const double copyPct = outDur > 0 ? double(copyMs) * 100.0 / double(outDur) : 0.0;

        h += QString("<h2>Export</h2><p><b>%1</b><br>duration %2 ms / RAP %3 / streams %4 / captions %5 / audio %6</p>")
                 .arg(htmlEsc(pathB)).arg(rb.durationMs).arg(rb.rapMs.size()).arg(rb.streams.size())
                 .arg(captionCount(rb)).arg(audioCount(rb));
        h += htmlScanTable(rb);
        h += QString("<h2>Compare Summary</h2><p>captions: <b>%1</b> / audio: <b>%2</b> / copied: <b>%3 ms (%4%)</b> / re-encoded: <b>%5 ms</b></p>")
                 .arg(htmlEsc(capStatus), htmlEsc(audStatus))
                 .arg(copyMs).arg(copyPct, 0, 'f', 1).arg(reencodeMs);
        h += "<table><thead><tr><th>Kind</th><th>Source start</th><th>Source end</th><th>Output start</th><th>Output end</th><th>Seam before</th></tr></thead><tbody>";
        for (const auto& p : pieces) {
            h += QString("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td><td>%6</td></tr>")
                     .arg(p.kind == CompareViewer::PieceKind::Copy ? "copy" : "reencode")
                     .arg(p.srcStartMs).arg(p.srcEndMs).arg(p.outStartMs).arg(p.outEndMs)
                     .arg(p.seamBefore ? "yes" : "");
        }
        h += "</tbody></table>";
    }
    return h;
}

bool saveTextFile(const QString& path, const QByteArray& bytes, QString& error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        error = f.errorString();
        return false;
    }
    if (f.write(bytes) != bytes.size()) {
        error = f.errorString();
        return false;
    }
    if (!f.commit()) {
        error = f.errorString();
        return false;
    }
    return true;
}

struct Problem {
    QString fileLabel;
    QString severity;
    QString type;
    qint64 timeMs = -1;
    qint64 byte = -1;
    QString detail;
};

template <typename Point, typename GetMs, typename GetByte>
void addTimingProblems(QVector<Problem>& problems, const QVector<Point>& points,
                       const QString& fileLabel, const QString& series,
                       GetMs getMs, GetByte getByte)
{
    constexpr qint64 kGapMs = 5000;
    for (int i = 1; i < points.size(); ++i) {
        const qint64 prev = getMs(points[i - 1]);
        const qint64 cur = getMs(points[i]);
        const qint64 delta = cur - prev;
        if (delta < -100) {
            problems.push_back(Problem{ fileLabel, "error", series + " backward jump", cur, getByte(points[i]),
                                        QString("previous %1 ms, current %2 ms").arg(prev).arg(cur) });
        } else if (delta > kGapMs) {
            problems.push_back(Problem{ fileLabel, "warning", series + " gap", cur, getByte(points[i]),
                                        QString("gap %1 ms from %2 to %3").arg(delta).arg(prev).arg(cur) });
        }
    }
}

QVector<Problem> collectProblemsOne(const TsScanResult& r, const QString& label)
{
    QVector<Problem> problems;
    for (const auto& p : r.pcr) {
        if (p.discontinuity)
            problems.push_back(Problem{ label, "warning", "PCR discontinuity", p.pcrMs, p.byte,
                                        "adaptation_field.discontinuity_indicator is set" });
    }
    for (const auto& e : r.ccErrorPoints) {
        problems.push_back(Problem{ label, "error", "CC error", -1, e.byte,
                                    QString("PID 0x%1 expected %2, got %3")
                                        .arg(e.pid, 4, 16, QChar('0')).arg(e.expected).arg(e.actual) });
    }

    addTimingProblems(problems, r.videoPts, label, "video PTS",
                      [](const PesPoint& p) { return p.ptsMs; },
                      [](const PesPoint& p) { return p.byte; });
    addTimingProblems(problems, r.audioPts, label, "audio PTS",
                      [](const PesPoint& p) { return p.ptsMs; },
                      [](const PesPoint& p) { return p.byte; });
    addTimingProblems(problems, r.pcr, label, "PCR",
                      [](const PcrPoint& p) { return p.pcrMs; },
                      [](const PcrPoint& p) { return p.byte; });

    std::sort(problems.begin(), problems.end(), [](const Problem& a, const Problem& b) {
        if (a.fileLabel != b.fileLabel)
            return a.fileLabel < b.fileLabel;
        if (a.timeMs >= 0 && b.timeMs >= 0)
            return a.timeMs < b.timeMs;
        if (a.timeMs >= 0)
            return true;
        if (b.timeMs >= 0)
            return false;
        return a.byte < b.byte;
    });
    return problems;
}

QVector<Problem> collectProblems(const TsScanResult& ra, const QString& pathA,
                                 const TsScanResult& rb, const QString& pathB, bool hasB)
{
    QVector<Problem> problems = collectProblemsOne(ra, QString("A: %1").arg(QFileInfo(pathA).fileName()));
    if (hasB) {
        const auto b = collectProblemsOne(rb, QString("B: %1").arg(QFileInfo(pathB).fileName()));
        problems += b;
    }
    return problems;
}
} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ts-structure-viewer"));
    qRegisterMetaType<TsScanResult>(); // so it can cross the worker thread boundary

    QMainWindow win;
    win.setWindowTitle(QStringLiteral("TS Structure Viewer"));

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

    // Problems tab: detected discontinuities, timing jumps/gaps and CC errors.
    auto* problemsPane = new QWidget(&win);
    auto* problemsLayout = new QVBoxLayout(problemsPane);
    auto* problemsSummary = new QLabel(QStringLiteral("Open a TS file to scan for problems"), problemsPane);
    problemsSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    problemsSummary->setWordWrap(true);
    auto* problemsTable = new QTableWidget(0, 6, problemsPane);
    problemsTable->setHorizontalHeaderLabels({ "File", "Severity", "Type", "Time", "Byte", "Detail" });
    problemsTable->horizontalHeader()->setStretchLastSection(true);
    problemsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    problemsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    problemsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    problemsLayout->addWidget(problemsSummary);
    problemsLayout->addWidget(problemsTable, 1);

    // Compare tab: source-vs-export structure alignment, added only in compare
    // mode (the recovered copy / re-encode / dropped plan over a shared source axis).
    // Hide until it's added as a tab, else this not-yet-placed child paints its
    // placeholder over the menu bar at (0,0).
    auto* compare = new CompareViewer(&win);
    compare->hide();

    tabs->addTab(structure, "Structure");
    tabs->addTab(streamsPane, "Streams");
    tabs->addTab(timingPane, "Timing");
    tabs->addTab(problemsPane, "Problems");
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
        qint64 outDur = 0;
        qint64 copyMs = 0;
        qint64 reencodeMs = 0;
        for (const auto& p : buildComparePieces(ra, rb, outDur)) {
            const qint64 dur = std::max<qint64>(0, p.outEndMs - p.outStartMs);
            if (p.kind == CompareViewer::PieceKind::Copy)
                copyMs += dur;
            else
                reencodeMs += dur;
        }
        const double copyPct = outDur > 0 ? double(copyMs) * 100.0 / double(outDur) : 0.0;
        const double reencodePct = outDur > 0 ? double(reencodeMs) * 100.0 / double(outDur) : 0.0;
        summary->setText(
            QString("A %1 (%2 ms, %3 streams, RAP %4)   B %5 (%6 ms, %7 streams, RAP %8)\n"
                    "captions A=%9 B=%10 %11   audio A=%12 B=%13 %14   B seams (PCR discontinuities): %15   "
                    "copy %16% (%17 ms)   re-encode %18% (%19 ms)")
                .arg(QFileInfo(pathA).fileName()).arg(ra.durationMs).arg(ra.streams.size()).arg(ra.rapMs.size())
                .arg(QFileInfo(pathB).fileName()).arg(rb.durationMs).arg(rb.streams.size()).arg(rb.rapMs.size())
                .arg(capA).arg(capB).arg(capB >= capA && capA > 0 ? "OK" : (capA == 0 ? "-" : "LOST"))
                .arg(audA).arg(audB).arg(audB == audA ? "OK" : (audB < audA ? "DROPPED" : "+"))
                .arg(seams)
                .arg(copyPct, 0, 'f', 1).arg(copyMs)
                .arg(reencodePct, 0, 'f', 1).arg(reencodeMs));
    };

    auto buildProblems = [&] {
        const auto problems = collectProblems(ra, pathA, rb, pathB, hasB);
        problemsTable->setRowCount(problems.size());
        int errors = 0;
        int warnings = 0;
        for (int i = 0; i < problems.size(); ++i) {
            const auto& p = problems[i];
            if (p.severity == "error")
                ++errors;
            else
                ++warnings;
            const QColor color = p.severity == "error" ? QColor(210, 75, 75) : QColor(190, 135, 45);
            const QString timeText = p.timeMs >= 0 ? QString::number(p.timeMs) : QString("-");
            const QString byteText = p.byte >= 0 ? QString::number(p.byte) : QString("-");
            const QStringList cells = { p.fileLabel, p.severity, p.type, timeText, byteText, p.detail };
            for (int c = 0; c < cells.size(); ++c) {
                auto* it = new QTableWidgetItem(cells[c]);
                it->setForeground(color);
                it->setData(Qt::UserRole, p.timeMs);
                problemsTable->setItem(i, c, it);
            }
        }
        problemsTable->resizeColumnsToContents();
        problemsSummary->setText(QString("%1 problems: %2 errors, %3 warnings. Double-click a timed row to zoom there.")
                                     .arg(problems.size()).arg(errors).arg(warnings));
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
        buildProblems();
        labB->setVisible(hasB);
        graphB->setVisible(hasB);

        // Compare tab: recover the plan (copy / re-encode / dropped) by aligning
        // B's GOP durations back onto A, and show it only while comparing.
        const int compareIdx = tabs->indexOf(compare);
        if (hasB) {
            qint64 outDur = 0;
            const auto pieces = buildComparePieces(ra, rb, outDur);
            CompareViewer::Source src;
            src.label = QString("A: source - %1").arg(QFileInfo(pathA).fileName());
            src.durationMs = ra.durationMs;
            src.rapMs = ra.rapMs;
            compare->setSource(src);
            compare->setOutput(pieces, outDur);
            if (compareIdx < 0)
                tabs->addTab(compare, "Compare");
        } else if (compareIdx >= 0) {
            tabs->removeTab(compareIdx);
        }

        // Test hook: TSV_VIEW="startMs,endMs" opens the structure/picture views zoomed.
        if (const QByteArray v = qgetenv("TSV_VIEW"); !v.isEmpty()) {
            const auto parts = QString::fromLatin1(v).split(',');
            if (parts.size() == 2) {
                structure->setView(parts[0].toLongLong(), parts[1].toLongLong());
                pics->setView(parts[0].toLongLong(), parts[1].toLongLong());
                if (hasB)
                    compare->setView(parts[0].toLongLong(), parts[1].toLongLong());
            }
        }
        win.setWindowTitle(hasB
            ? QString("TS Structure Viewer - %1  vs  %2").arg(QFileInfo(pathA).fileName(), QFileInfo(pathB).fileName())
            : QString("TS Structure Viewer - %1").arg(QFileInfo(pathA).fileName()));
    };

    // Scans on a background thread; the UI stays live via a local event loop that
    // quits when the worker finishes (or after a cancel takes effect).
    auto scan = [&](const QString& path, TsScanResult& out) {
        QThread thread;
        TsScanWorker worker;
        worker.configure(path);
        worker.moveToThread(&thread);

        QProgressDialog dlg(QString("Scanning %1...").arg(QFileInfo(path).fileName()),
                            "Cancel", 0, 1000, &win);
        dlg.setWindowModality(Qt::WindowModal);
        dlg.setMinimumDuration(0);
        dlg.setAutoClose(false);
        dlg.setAutoReset(false);

        QObject::connect(&worker, &TsScanWorker::progress, &dlg, [&dlg](qint64 d, qint64 t) {
            dlg.setValue(t > 0 ? int(d * 1000 / t) : 0);
        });
        QObject::connect(&dlg, &QProgressDialog::canceled, &worker, &TsScanWorker::cancel);

        QEventLoop loop;
        QObject::connect(&worker, &TsScanWorker::finished, &loop, [&](const TsScanResult& r) {
            out = r;
            loop.quit();
        });
        QObject::connect(&thread, &QThread::started, &worker, &TsScanWorker::run);

        thread.start();
        dlg.show();
        loop.exec();
        thread.quit();
        thread.wait();
        dlg.close();

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

    QObject::connect(problemsTable, &QTableWidget::itemDoubleClicked, &win, [&](QTableWidgetItem* item) {
        if (!item)
            return;
        const qint64 problemMs = item->data(Qt::UserRole).toLongLong();
        if (problemMs < 0) {
            win.statusBar()->showMessage(QStringLiteral("This problem has a byte position but no timeline timestamp"), 5000);
            return;
        }

        qint64 srcMs = problemMs;
        const QString fileLabel = problemsTable->item(item->row(), 0)->text();
        bool mappedFromB = false;
        if (hasB && fileLabel.startsWith("B:")) {
            qint64 outDur = 0;
            const auto pieces = buildComparePieces(ra, rb, outDur);
            for (const auto& p : pieces) {
                if (problemMs >= p.outStartMs && problemMs <= p.outEndMs) {
                    srcMs = p.srcStartMs + (problemMs - p.outStartMs);
                    mappedFromB = true;
                    break;
                }
            }
        }

        const qint64 radius = 5000;
        const qint64 start = std::max<qint64>(0, srcMs - radius);
        const qint64 end = std::max<qint64>(start + 1, std::min<qint64>(ra.durationMs, srcMs + radius));
        structure->setView(start, end);
        pics->setView(start, end);
        if (hasB)
            compare->setView(start, end);
        tabs->setCurrentWidget(mappedFromB && hasB ? static_cast<QWidget*>(compare) : static_cast<QWidget*>(structure));
        win.statusBar()->showMessage(QString("Jumped to %1 ms").arg(srcMs), 4000);
    });

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
    fileMenu->addSeparator();

    auto saveReport = [&](bool html) {
        if (!ra.ok) {
            win.statusBar()->showMessage(QStringLiteral("Open a TS file first"), 5000);
            return;
        }
        const QString filter = html ? QStringLiteral("HTML report (*.html);;All files (*)")
                                    : QStringLiteral("JSON report (*.json);;All files (*)");
        QString path = QFileDialog::getSaveFileName(&win, html ? "Save HTML report" : "Save JSON report",
                                                    QString(), filter);
        if (path.isEmpty())
            return;
        if (html && !path.endsWith(".html", Qt::CaseInsensitive))
            path += ".html";
        if (!html && !path.endsWith(".json", Qt::CaseInsensitive))
            path += ".json";

        const QByteArray bytes = html
            ? reportHtml(ra, pathA, rb, pathB, hasB).toUtf8()
            : QJsonDocument(reportJson(ra, pathA, rb, pathB, hasB)).toJson(QJsonDocument::Indented);
        QString error;
        if (!saveTextFile(path, bytes, error)) {
            QMessageBox::warning(&win, "Save report failed", error);
            return;
        }
        win.statusBar()->showMessage(QString("Saved report: %1").arg(path), 6000);
    };

    QObject::connect(fileMenu->addAction("Save &JSON Report..."), &QAction::triggered, &win, [&] { saveReport(false); });
    QObject::connect(fileMenu->addAction("Save &HTML Report..."), &QAction::triggered, &win, [&] { saveReport(true); });

    win.resize(1180, 460);
    win.show();
    if (argc >= 2)
        openA(QString::fromLocal8Bit(argv[1]));
    if (argc >= 3)
        openB(QString::fromLocal8Bit(argv[2]));
    return app.exec();
}
