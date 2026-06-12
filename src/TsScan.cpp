#include "TsScan.h"

#include <QFile>
#include <algorithm>

namespace {
constexpr int kPkt = 188;

// 33-bit PTS/DTS in a 5-byte field at p, returned in 90 kHz ticks.
qint64 readTs33(const uint8_t* p)
{
    return (qint64(p[0] >> 1 & 0x07) << 30) | (qint64(p[1]) << 22) | (qint64(p[2] >> 1 & 0x7F) << 15)
        | (qint64(p[3]) << 7) | qint64(p[4] >> 1 & 0x7F);
}

QString codecForStreamType(int st)
{
    switch (st) {
    case 0x01: return QStringLiteral("MPEG-1 video");
    case 0x02: return QStringLiteral("MPEG-2 video");
    case 0x1B: return QStringLiteral("H.264");
    case 0x24: return QStringLiteral("HEVC");
    case 0x03:
    case 0x04: return QStringLiteral("MPEG audio");
    case 0x0F: return QStringLiteral("AAC (ADTS)");
    case 0x11: return QStringLiteral("AAC (LATM)");
    case 0x06: return QStringLiteral("PES private");
    case 0x0D: return QStringLiteral("data");
    default: return QStringLiteral("0x%1").arg(st, 2, 16, QChar('0'));
    }
}
bool isVideoType(int st) { return st == 0x01 || st == 0x02 || st == 0x1B || st == 0x24; }
bool isAudioType(int st) { return st == 0x03 || st == 0x04 || st == 0x0F || st == 0x11; }

enum VCodec { VC_OTHER, VC_MPEG2, VC_H264, VC_HEVC };
VCodec vcodecOf(int st)
{
    if (st == 0x01 || st == 0x02) return VC_MPEG2;
    if (st == 0x1B) return VC_H264;
    if (st == 0x24) return VC_HEVC;
    return VC_OTHER;
}

// Minimal MSB-first bit reader for Exp-Golomb slice-header fields. Emulation-
// prevention bytes are not stripped: slice_type sits in the first ~2 bytes of
// the slice header, well before a 00 00 03 sequence could plausibly appear.
struct BitReader {
    const uint8_t* d;
    int nbits;
    int pos = 0;
    BitReader(const uint8_t* d_, int nbytes) : d(d_), nbits(nbytes * 8) {}
    int u1()
    {
        if (pos >= nbits) return 0;
        const int b = (d[pos >> 3] >> (7 - (pos & 7))) & 1;
        ++pos;
        return b;
    }
    uint32_t ue()
    {
        int z = 0;
        while (pos < nbits && u1() == 0) ++z;
        uint32_t v = 0;
        for (int i = 0; i < z; ++i) v = (v << 1) | u1();
        return (1u << z) - 1 + v;
    }
};

// Classify the first coded picture in an elementary-stream fragment (one TS
// packet's worth). Returns 'I'/'P'/'B' or '?', and sets `closed` for an IDR /
// MPEG-2 closed_gop. Best-effort for HEVC (IRAP typing + a slice_type guess).
char picTypeInEs(const uint8_t* es, int len, VCodec vc, bool& closed)
{
    closed = false;
    bool gopClosed = false;
    bool sawGop = false, sawSeq = false; // MPEG-2: both only precede an I picture
    for (int i = 0; i + 4 < len; ++i) {
        if (!(es[i] == 0 && es[i + 1] == 0 && es[i + 2] == 1))
            continue;
        const int sc = i + 3; // first byte after the 00 00 01 start code
        if (vc == VC_MPEG2) {
            if (es[sc] == 0xB3) {                        // sequence header
                sawSeq = true;
            } else if (es[sc] == 0xB8 && sc + 4 < len) { // group_of_pictures header
                gopClosed = (es[sc + 4] >> 6) & 1;
                sawGop = true;
            } else if (es[sc] == 0x00 && sc + 2 < len) { // picture header (definitive)
                const int pct = (es[sc + 2] >> 3) & 0x07;
                closed = gopClosed || sawGop;
                if (pct == 1) return 'I';
                if (pct == 2) return 'P';
                if (pct == 3) return 'B';
                return '?';
            }
        } else if (vc == VC_H264) {
            const int nal = es[sc] & 0x1F;
            if (nal == 5) { closed = true; return 'I'; } // IDR slice
            if (nal == 1) {                              // non-IDR slice
                BitReader br(es + sc + 1, len - sc - 1);
                br.ue();                                  // first_mb_in_slice
                const uint32_t st = br.ue() % 5;          // slice_type
                if (st == 0) return 'P';
                if (st == 1) return 'B';
                if (st == 2) return 'I';
                return '?';
            }
        } else if (vc == VC_HEVC) {
            const int nal = (es[sc] >> 1) & 0x3F;
            if (nal == 19 || nal == 20) { closed = true; return 'I'; } // IDR
            if (nal >= 16 && nal <= 22) return 'I';                    // BLA/CRA (open)
            if (nal <= 9) {                                            // trailing/leading VCL
                BitReader br(es + sc + 2, len - sc - 2); // skip 2-byte NAL header
                br.u1();                                  // first_slice_segment_in_pic_flag
                br.ue();                                  // slice_pic_parameter_set_id (assume no extra bits)
                const uint32_t st = br.ue();              // slice_type
                if (st == 0) return 'B';
                if (st == 1) return 'P';
                if (st == 2) return 'I';
                return '?';
            }
        }
    }
    // MPEG-2 I picture whose picture header sits past this packet: a sequence or
    // GOP header (which only precede an I) is enough to type it.
    if (vc == VC_MPEG2 && (sawGop || sawSeq)) {
        closed = sawGop && gopClosed;
        return 'I';
    }
    return '?';
}

// Reassembles one PSI section per PID across packets, enough for PAT/PMT.
struct SectionAsm {
    QByteArray buf;
    int want = 0; // total section bytes expected (3 + section_length), 0 = idle
    void feed(const uint8_t* payload, int len, bool pusi, std::function<void(const uint8_t*, int)> onSection)
    {
        int off = 0;
        if (pusi) {
            if (len < 1)
                return;
            const int ptr = payload[0]; // pointer_field
            off = 1 + ptr;
            buf.clear();
            want = 0;
        } else if (want == 0) {
            return; // not collecting and no section start -> skip
        }
        if (off > len)
            return;
        buf.append(reinterpret_cast<const char*>(payload + off), len - off);
        if (want == 0 && buf.size() >= 3) {
            const auto* b = reinterpret_cast<const uint8_t*>(buf.constData());
            want = 3 + (((b[1] & 0x0F) << 8) | b[2]);
        }
        if (want > 0 && buf.size() >= want) {
            onSection(reinterpret_cast<const uint8_t*>(buf.constData()), want);
            buf.clear();
            want = 0;
        }
    }
};
} // namespace

TsScanResult TsScan::scanFile(const QString& path, const std::function<bool(qint64, qint64)>& progress)
{
    TsScanResult r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        r.error = QStringLiteral("Cannot open: %1").arg(path);
        return r;
    }
    r.fileSize = f.size();

    SectionAsm patAsm, pmtAsm;
    QMap<int, int> lastCc; // pid -> last continuity_counter
    bool haveFirstPts = false;
    VCodec vcodec = VC_OTHER;

    auto parsePat = [&](const uint8_t* s, int n) {
        if (r.pmtPid >= 0 || s[0] != 0x00)
            return;
        const int secLen = ((s[1] & 0x0F) << 8) | s[2];
        const int end = std::min(n, 3 + secLen) - 4; // drop CRC
        for (int i = 8; i + 4 <= end; i += 4) {
            const int prog = (s[i] << 8) | s[i + 1];
            const int pid = ((s[i + 2] & 0x1F) << 8) | s[i + 3];
            if (prog != 0) { // skip NIT
                r.programNumber = prog;
                r.pmtPid = pid;
                break;
            }
        }
    };

    auto parsePmt = [&](const uint8_t* s, int n) {
        if (s[0] != 0x02 || !r.streams.isEmpty())
            return;
        const int secLen = ((s[1] & 0x0F) << 8) | s[2];
        const int end = std::min(n, 3 + secLen) - 4;
        r.pcrPid = ((s[8] & 0x1F) << 8) | s[9];
        const int pil = ((s[10] & 0x0F) << 8) | s[11];
        int i = 12 + pil;
        while (i + 5 <= end) {
            TsStreamInfo si;
            si.streamType = s[i];
            si.pid = ((s[i + 1] & 0x1F) << 8) | s[i + 2];
            const int esil = ((s[i + 3] & 0x0F) << 8) | s[i + 4];
            const int dstart = i + 5;
            const int dend = std::min(end, dstart + esil);
            int componentTag = -1;
            for (int d = dstart; d + 2 <= dend;) {
                const int tag = s[d];
                const int dl = s[d + 1];
                const uint8_t* dv = s + d + 2;
                if (tag == 0x0A && dl >= 3) // ISO_639_language
                    si.language = QString::fromLatin1(reinterpret_cast<const char*>(dv), 3);
                else if (tag == 0x52 && dl >= 1) // stream_identifier
                    componentTag = dv[0];
                d += 2 + dl;
            }
            si.codec = codecForStreamType(si.streamType);
            if (isVideoType(si.streamType)) {
                si.kind = QStringLiteral("video");
                if (r.videoPid < 0) {
                    r.videoPid = si.pid;
                    vcodec = vcodecOf(si.streamType);
                    r.videoCodec = si.codec;
                }
            } else if (isAudioType(si.streamType)) {
                si.kind = QStringLiteral("audio");
                if (r.audioPid < 0)
                    r.audioPid = si.pid;
            } else if (si.streamType == 0x06) {
                // ARIB: caption component_tag 0x30-0x37, superimpose 0x38-0x3F.
                if (componentTag >= 0x30 && componentTag <= 0x37) {
                    si.kind = QStringLiteral("caption");
                    si.codec = QStringLiteral("ARIB caption");
                } else if (componentTag >= 0x38 && componentTag <= 0x3F) {
                    si.kind = QStringLiteral("superimpose");
                    si.codec = QStringLiteral("ARIB superimpose");
                } else {
                    si.kind = QStringLiteral("private");
                }
            } else {
                si.kind = QStringLiteral("data");
            }
            r.streams.push_back(si);
            i = dstart + esil;
        }
        for (auto& s2 : r.streams)
            if (s2.pid == r.pcrPid)
                s2.isPcr = true;
    };

    QByteArray chunk;
    const int kChunkPkts = 8192;
    qint64 pos = 0;
    bool cancelled = false;

    while (!cancelled) {
        chunk = f.read(qint64(kPkt) * kChunkPkts);
        if (chunk.size() < kPkt)
            break;
        const auto* base = reinterpret_cast<const uint8_t*>(chunk.constData());
        const int npk = chunk.size() / kPkt;
        for (int k = 0; k < npk; ++k) {
            const uint8_t* pk = base + k * kPkt;
            const qint64 byte = pos + qint64(k) * kPkt;
            if (pk[0] != 0x47)
                continue;
            const int pid = ((pk[1] & 0x1F) << 8) | pk[2];
            const bool pusi = pk[1] & 0x40;
            const int afc = (pk[3] >> 4) & 3;
            const int cc = pk[3] & 0x0F;

            // Continuity counter check (only when a payload is present).
            if (afc & 1) {
                if (lastCc.contains(pid)) {
                    const int expected = (lastCc[pid] + 1) & 0x0F;
                    if (cc != expected && cc != lastCc[pid]) // dup (==) is allowed
                        r.ccErrors[pid] += 1;
                }
                lastCc[pid] = cc;
            }

            int payoff = 4;
            bool rai = false, disc = false;
            qint64 pcrMs = -1;
            if (afc & 2) {
                const int aflen = pk[4];
                if (aflen > 0) {
                    const uint8_t fl = pk[5];
                    disc = fl & 0x80;
                    rai = fl & 0x40;
                    if ((fl & 0x10) && aflen >= 7) { // PCR present
                        const qint64 base33 = (qint64(pk[6]) << 25) | (qint64(pk[7]) << 17)
                            | (qint64(pk[8]) << 9) | (qint64(pk[9]) << 1) | (pk[10] >> 7);
                        pcrMs = base33 / 90; // ignore the 27 MHz extension
                    }
                }
                payoff = 5 + pk[4];
            }

            // PSI.
            if ((afc & 1) && payoff < kPkt) {
                if (pid == 0x0000)
                    patAsm.feed(pk + payoff, kPkt - payoff, pusi, parsePat);
                else if (r.pmtPid >= 0 && pid == r.pmtPid)
                    pmtAsm.feed(pk + payoff, kPkt - payoff, pusi, parsePmt);
            }

            // PCR (record absolute first, rebase after baseline is known).
            if (pcrMs >= 0 && pid == r.pcrPid)
                r.pcr.push_back(PcrPoint{ byte, pcrMs, disc });

            // PES PTS/DTS on PUSI packets.
            if ((afc & 1) && pusi && payoff + 14 <= kPkt) {
                const uint8_t* p = pk + payoff;
                if (p[0] == 0 && p[1] == 0 && p[2] == 1) {
                    const int sid = p[3];
                    const int flags = p[7];
                    qint64 ptsMs = -1, dtsMs = -1;
                    if (flags & 0x80) {
                        ptsMs = readTs33(p + 9) / 90;
                        if ((flags & 0x40))
                            dtsMs = readTs33(p + 14) / 90;
                    }
                    if (ptsMs >= 0) {
                        if (!haveFirstPts && pid == r.videoPid) {
                            r.firstVideoPtsMs = ptsMs;
                            haveFirstPts = true;
                        }
                        PesPoint pp{ byte, ptsMs, dtsMs };
                        if (pid == r.videoPid) {
                            r.videoPts.push_back(pp);
                            if (rai)
                                r.rapMs.push_back(ptsMs); // RAP: rebased below
                            // Picture type from the ES that follows the PES header.
                            const int esOff = 9 + p[8];
                            const int esLen = (kPkt - payoff) - esOff;
                            FramePic fp;
                            fp.ptsMs = ptsMs;
                            fp.key = rai;
                            if (esLen > 4)
                                fp.type = picTypeInEs(p + esOff, esLen, vcodec, fp.closed);
                            r.frames.push_back(fp);
                        } else if (pid == r.audioPid) {
                            r.audioPts.push_back(pp);
                        }
                    }
                }
            }
        }
        pos += chunk.size();
        if (progress && r.fileSize > 0 && !progress(pos, r.fileSize)) {
            cancelled = true;
            break;
        }
    }

    if (cancelled) {
        r.error = QStringLiteral("Cancelled");
        return r;
    }
    if (r.videoPid < 0 || r.videoPts.isEmpty()) {
        r.error = QStringLiteral("No video PES found");
        return r;
    }

    // Rebase all times so 0 = first video PTS.
    const qint64 base = r.firstVideoPtsMs;
    auto rebase = [base](qint64 ms) { return ms - base; };
    for (auto& p : r.videoPts) { p.ptsMs = rebase(p.ptsMs); if (p.dtsMs >= 0) p.dtsMs = rebase(p.dtsMs); }
    for (auto& p : r.audioPts) { p.ptsMs = rebase(p.ptsMs); if (p.dtsMs >= 0) p.dtsMs = rebase(p.dtsMs); }
    for (auto& p : r.pcr) p.pcrMs = rebase(p.pcrMs);
    for (auto& t : r.rapMs) t = rebase(t);
    std::sort(r.rapMs.begin(), r.rapMs.end());
    for (auto& fp : r.frames) fp.ptsMs = rebase(fp.ptsMs);
    std::sort(r.frames.begin(), r.frames.end(),
              [](const FramePic& a, const FramePic& b) { return a.ptsMs < b.ptsMs; });

    qint64 maxPts = 0;
    for (const auto& p : r.videoPts)
        maxPts = std::max(maxPts, p.ptsMs);
    r.durationMs = maxPts;
    r.ok = true;
    return r;
}
