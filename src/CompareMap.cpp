#include "CompareMap.h"

#include <QMultiHash>

namespace {
constexpr int kMinRun = 2; // matching GOPs needed to accept a run as a copy region
} // namespace

QVector<CompareViewer::OutPiece> buildComparePieces(const TsScanResult& source,
                                                    const TsScanResult& exported,
                                                    qint64& outDurationMs)
{
    using Piece = CompareViewer::OutPiece;
    using Kind = CompareViewer::PieceKind;

    QVector<Piece> pieces;
    const QVector<qint64>& rapA = source.rapMs;
    const QVector<qint64>& rapB = exported.rapMs;
    outDurationMs = exported.durationMs;
    const int nA = rapA.size();
    const int nB = rapB.size();
    if (nA < 2 || nB < 2)
        return pieces;

    const auto durA = [&](int i) { return rapA[i + 1] - rapA[i]; };
    const auto durB = [&](int k) { return rapB[k + 1] - rapB[k]; };

    // Source GOP starts indexed by duration, for quick candidate lookup.
    QMultiHash<qint64, int> byDur;
    for (int i = 0; i + 1 < nA; ++i)
        byDur.insert(durA(i), i);

    // Walk B's GOPs; greedily take the longest source-aligned run at each point.
    int k = 0;
    while (k + 1 < nB) {
        int bestJ = -1, bestLen = 0;
        const auto cands = byDur.values(durB(k));
        for (int j : cands) {
            int t = 0;
            while (k + 1 + t < nB && j + 1 + t < nA && durB(k + t) == durA(j + t))
                ++t;
            if (t > bestLen) {
                bestLen = t;
                bestJ = j;
            }
        }
        if (bestLen >= kMinRun) {
            Piece p;
            p.kind = Kind::Copy;
            p.srcStartMs = rapA[bestJ];
            p.srcEndMs = rapA[bestJ + bestLen];
            p.outStartMs = rapB[k];
            p.outEndMs = rapB[k + bestLen];
            pieces.push_back(p);
            k += bestLen;
        } else {
            Piece p; // boundary GOP -> re-encode; source span resolved below
            p.kind = Kind::Reencode;
            p.outStartMs = rapB[k];
            p.outEndMs = rapB[k + 1];
            p.srcStartMs = -1;
            pieces.push_back(p);
            ++k;
        }
    }

    // Place each re-encode window's source span against its neighbouring copy: a
    // lead-in ends where the next copy begins, a tail begins where the previous
    // copy ends; width = the window's own output duration.
    for (int i = 0; i < pieces.size(); ++i) {
        if (pieces[i].kind != Kind::Reencode)
            continue;
        const qint64 outDur = pieces[i].outEndMs - pieces[i].outStartMs;
        if (i + 1 < pieces.size() && pieces[i + 1].kind == Kind::Copy) {
            pieces[i].srcEndMs = pieces[i + 1].srcStartMs;
            pieces[i].srcStartMs = pieces[i].srcEndMs - outDur;
        } else if (i > 0 && pieces[i - 1].kind == Kind::Copy) {
            pieces[i].srcStartMs = pieces[i - 1].srcEndMs;
            pieces[i].srcEndMs = pieces[i].srcStartMs + outDur;
        } else {
            pieces[i].srcStartMs = pieces[i].outStartMs;
            pieces[i].srcEndMs = pieces[i].srcStartMs + outDur;
        }
    }

    // Mark seams where a piece's source origin isn't contiguous with the previous.
    for (int i = 1; i < pieces.size(); ++i)
        pieces[i].seamBefore = pieces[i].srcStartMs != pieces[i - 1].srcEndMs;

    return pieces;
}
