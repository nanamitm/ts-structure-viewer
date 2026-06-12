# ts-structure-viewer (prototype)

A standalone Qt6/C++ prototype of the **TS structure viewer** for
[`ts-edit-gui`](../ts-edit-gui). It is a read-only, viewport-based (zoom/pan)
timeline for verifying smart-rendering exports: it overlays the GOP/RAP
structure with the exporter's plan so you can see, at a glance, which regions
are passed through verbatim (copy) and which are partial-GOP re-encode windows.

This is a sandbox: the widget is driven by **synthetic data** (`main.cpp`) so it
builds and runs with nothing but Qt. Once the interaction model is settled, the
`StructureViewer` widget is meant to be lifted into `ts-edit-gui` and fed real
data from `TsSourceIndex` (RAP times) and `TsSmartExporter::planSegments`
(copy / lead-in / tail windows).

## Controls
- **wheel** — zoom in/out, centered on the cursor
- **left-drag** — pan the visible range
- **left-click** — seek (emits `seekRequested`)
- **double-click** — fit the whole duration
- **click the minimap** — recenter the view there

## Layout
- **Minimap** (top): whole-program overview + the current view window box
- **Ruler**: time ticks for the visible range
- **Lane**: GOP bands (alternating shade), keyframe lines, and the plan overlay
  - green = copy region (verbatim passthrough)
  - orange = lead-in / tail partial-GOP re-encode windows
  - red / orange grips = segment IN / OUT

## Build
```powershell
$env:PATH="C:\Program Files\CMake\bin;"+$env:PATH
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH=C:\Qt\6.11.1\msvc2022_64
cmake --build build-msvc --config Release
```

## Run
```powershell
$env:PATH="C:\Qt\6.11.1\msvc2022_64\bin;"+$env:PATH
.\build-msvc\Release\ts-structure-viewer.exe
# Test hook: open zoomed to a range (ms):
$env:TSV_VIEW="59500,62500"; .\build-msvc\Release\ts-structure-viewer.exe
```

## Status
The `StructureViewer` widget has been **ported into `ts-edit-gui`** as a
read-only "Structure" tab (commit `5d64467`), fed by the real RAP scan and the
exporter's plan via `TsSmartExporter::planSegments`. The canonical copy now
lives in `ts-edit-gui/src/ui/StructureViewer.*`; the copy here is the sandbox
where new ideas are prototyped against synthetic data before porting back.
Keep the two in sync deliberately (the integrated one is canonical).

## Two-file comparison viewer (`ts-compare-viewer`)
`CompareViewer` aligns a **source** and its **smart-render export** on one shared
viewport to verify a round trip. The master axis is SOURCE time:

- **A: source** lane — the whole source GOP/RAP structure; kept ranges are lit,
  dropped ranges dimmed.
- **B: export** lane — each output piece drawn directly beneath the source time
  it came from: a verbatim copy lines up vertically and keeps its GOP boundaries
  (green); a lead-in / tail re-encode window shows as a short orange block; cut
  regions leave a gap. A red seam marker flags each splice of non-adjacent source.
- A secondary ruler under lane B reads the rebased OUTPUT time.

Same controls as the structure viewer (wheel zoom / drag pan / click seek /
double-click fit). Driven by synthetic A/B round-trip data in `compare_main.cpp`
(mirrors `planSegments` + the export assembly); swap in two real `TsSourceIndex`
scans + the plan when porting into `ts-edit-gui`.

```powershell
$env:PATH="C:\Qt\6.11.1\msvc2022_64\bin;"+$env:PATH
.\build-msvc\Release\ts-compare-viewer.exe
$env:TSV_VIEW="58000,95000"; .\build-msvc\Release\ts-compare-viewer.exe  # zoom to a seam
```

## Real-file inspector (`ts-inspector`)
Scans an actual `.ts` and shows it in three tabs. Unlike the two viewers above
(synthetic data), this reads real files via `TsScan` — a dependency-free
(Qt-only) raw 188-byte TS parser:

- **Structure** — the real GOP/RAP timeline (reuses `StructureViewer`).
- **Streams** — the PSI: program / PMT / PCR_PID / video PID and the elementary
  stream table (PID, kind, codec, language, PCR), including ARIB caption /
  superimpose detection and a per-PID continuity-counter error count.
- **Timing** — PTS/DTS/PCR plotted against byte position; backward jumps, gaps
  and `discontinuity_indicator` show up directly (the GUI of `ts_pts_scan.py`).
- **Pictures** — per-frame picture type (I/P/B) as a skyline, with open/closed
  GOP markers at the RAPs (`PicTypeWidget`). Zoom into a GOP to read its cadence.
  MPEG-2 and H.264 are typed from the ES; HEVC is best-effort (IRAP + a guess).

`TsScan` extracts: PAT→PMT streams + PCR_PID, video RAP times (adaptation
`random_access_indicator`), per-PES PTS/DTS for video/audio, PCR samples, and
CC errors — all in one pass. Validated against ts-edit-gui's libav scan (same
RAP count, 3712, on the same source).

```powershell
$env:PATH="C:\Qt\6.11.1\msvc2022_64\bin;"+$env:PATH
.\build-msvc\Release\ts-inspector.exe "path\to\file.ts"   # or File > Open
```

## Two-file diff (`ts-diff`)
Scans a **source (A)** and an **export (B)** and verifies the smart-render round
trip:

- **Streams diff** — a merged PID table (A vs B kind/codec) with status
  (same / changed / dropped / added) plus a summary line: caption and audio
  track counts preserved (OK / LOST / DROPPED) and B's PCR discontinuity (seam)
  count. Answers "did the cut keep the ARIB captions and every audio track?"
- **Timing** — A's and B's PTS/DTS/PCR graphs stacked; the export's seams show
  up as discontinuity markers where the source is continuous.

```powershell
$env:PATH="C:\Qt\6.11.1\msvc2022_64\bin;"+$env:PATH
.\build-msvc\Release\ts-diff.exe "source.ts" "export.ts"   # or File > Open A / B
```

Verified on a real round trip (ゆるゆり H.264 source + a 2-cut smart-render
export): all three streams — video, audio and **ARIB caption (0x1305)** — diff
as `same`, captions/audio reported OK, and B shows exactly one seam where the
source timing is clean.
