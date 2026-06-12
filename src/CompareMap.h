#pragma once

#include "CompareViewer.h"
#include "TsScan.h"

#include <QVector>

// Recover the source->export mapping (the smart-render "plan") from two scans,
// without the editor's EDL. A verbatim-copied GOP keeps its RAP interval exactly,
// so a run of export (B) GOP durations that matches a contiguous slice of the
// source (A) durations is a copy region — mapped back to that source range. The
// boundary GOPs that don't match are the lead-in / tail re-encode windows.
//
// Returns the pieces to feed CompareViewer::setOutput, and the output duration.
QVector<CompareViewer::OutPiece> buildComparePieces(const TsScanResult& source,
                                                    const TsScanResult& exported,
                                                    qint64& outDurationMs);
