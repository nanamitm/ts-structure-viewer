#pragma once

#include <QMap>
#include <QMetaType>
#include <QString>
#include <QVector>
#include <cstdint>
#include <functional>

// Minimal dependency-free (Qt-only) MPEG-2 TS parser for the structure viewer.
// Walks the 188-byte packets once and extracts everything the viewers need:
//   * PSI: PAT -> first program's PMT -> elementary streams (PID, type, codec,
//     language, ARIB caption tag) and the PCR_PID
//   * video RAP times (adaptation field random_access_indicator)
//   * per-PES PTS/DTS for video and audio, and PCR samples, for the timing graph
//   * continuity-counter error counts per PID
// Times are RELATIVE ms (0 = first video PTS), matching the other widgets.
//
// This is deliberately a raw parser, not libav: a structure/verification tool
// wants packet-level PCR, CC, RAI and discontinuity flags, which a demuxer hides.
struct TsStreamInfo {
    int pid = -1;
    int streamType = -1; // PMT stream_type
    QString kind;        // "video" / "audio" / "caption" / "superimpose" / "data" / "private"
    QString codec;       // "MPEG-2" / "H.264" / "HEVC" / "AAC" / "ARIB caption" / ...
    QString language;    // ISO-639 from descriptor, if present
    bool isPcr = false;  // carries the PCR_PID
};

struct PesPoint {
    qint64 byte = 0;   // file offset of the PES's first packet
    qint64 ptsMs = 0;  // relative ms
    qint64 dtsMs = -1; // relative ms, -1 if absent
};

struct PcrPoint {
    qint64 byte = 0;
    qint64 pcrMs = 0;        // relative ms
    bool discontinuity = false;
};

// One coded video picture (access unit), typed from the elementary stream.
struct FramePic {
    qint64 ptsMs = 0;
    char type = '?';   // 'I' / 'P' / 'B' / '?' (couldn't determine)
    bool key = false;  // adaptation random_access_indicator (a RAP)
    bool closed = false; // IDR (H.264/HEVC) or MPEG-2 closed_gop -> closed GOP
};

struct TsScanResult {
    bool ok = false;
    QString error;
    qint64 fileSize = 0;

    int programNumber = -1;
    int pmtPid = -1;
    int pcrPid = -1;
    int videoPid = -1;
    int audioPid = -1; // first audio PID (for the timing graph)

    QVector<TsStreamInfo> streams;

    qint64 firstVideoPtsMs = 0; // absolute ms baseline that relative times use
    qint64 durationMs = 0;

    QVector<qint64> rapMs;       // video RAP times (relative ms, sorted)
    QVector<FramePic> frames;    // per-AU picture type (relative ms, sorted by PTS)
    QString videoCodec;          // "MPEG-2" / "H.264" / "HEVC" / ...
    QVector<PesPoint> videoPts;  // video PES timing (relative)
    QVector<PesPoint> audioPts;  // first-audio PES timing (relative)
    QVector<PcrPoint> pcr;       // PCR samples (relative)

    QMap<int, int> ccErrors;     // PID -> continuity-counter discontinuities
};

class TsScan {
public:
    // progress(done,total) may return false to cancel.
    static TsScanResult scanFile(const QString& path,
                                 const std::function<bool(qint64, qint64)>& progress = {});
};

// So TsScanResult can cross threads via a queued signal (see TsScanWorker).
Q_DECLARE_METATYPE(TsScanResult)
