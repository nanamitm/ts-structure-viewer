# ts-structure-viewer

`ts-structure-viewer` — a Qt6/C++ tool for inspecting **MPEG-2 TS** structure and
verifying **smart-rendering** (frame-accurate, mostly-lossless cut) exports.
Companion to the editor
[`ts-edit-gui`](https://github.com/nanamitm/ts-edit-gui): when a cut copies whole
GOPs verbatim and only re-encodes the boundary GOPs, this lets you *see* what was
copied, re-encoded, dropped or preserved.

It is built on a dependency-free (Qt-only) raw 188-byte TS parser (`TsScan`) —
no libav/ffmpeg — because a structure/verification tool wants the packet-level
PCR, CC, RAI and discontinuity flags a demuxer hides. The scan runs on a
background thread, so the UI stays responsive on multi-GB files.

## Tabs
Open a real `.ts` (`File > Open`, or pass it as the first argument):

- **Structure** — the GOP/RAP timeline on a zoom/pan viewport (`StructureViewer`).
- **Streams** — the PSI: program / PMT / PCR_PID / video PID and the elementary
  stream table (PID, kind, codec, language, PCR), including ARIB caption /
  superimpose detection and a per-PID continuity-counter error count.
- **Timing** — PTS/DTS/PCR plotted against byte position; backward jumps, gaps
  and `discontinuity_indicator` show up directly (a GUI of `ts_pts_scan.py`).
- **Pictures** — per-frame picture type (I/P/B) as a skyline, with open/closed
  GOP markers at the RAPs (`PicTypeWidget`). Zoom into a GOP to read its cadence.
  MPEG-2 and H.264 are typed from the ES; HEVC is best-effort (IRAP + a guess).

`TsScan` extracts, in one pass: PAT→PMT streams + PCR_PID, video RAP times
(adaptation `random_access_indicator`), per-PES PTS/DTS for video/audio, PCR
samples, and CC errors. Validated against ts-edit-gui's libav scan (same RAP
count on the same source).

### Viewport controls (Structure / Pictures)
wheel = zoom (cursor-anchored) · left-drag = pan · left-click = seek ·
double-click = fit · click the minimap = recenter.

## Compare mode (source vs export)
Open a second file (`File > Open Export (B) to compare...`, or pass it as the
second argument) to verify a smart-render round trip:

- **Streams** becomes a merged PID diff (A vs B kind/codec) with status
  (same / changed / dropped / added) plus a summary: caption and audio track
  counts preserved (OK / LOST / DROPPED) and B's PCR discontinuity (seam) count
  — answers "did the cut keep the ARIB captions and every audio track?"
- **Timing** stacks A's and B's graphs; the export's seams show up as
  discontinuity markers where the source is continuous.
- a **Compare** tab appears: the source GOP structure with kept ranges lit and
  dropped ranges dimmed, and the export drawn beneath the source time it came
  from (copy green, re-encode windows orange, splice seams marked). The plan is
  recovered with no EDL — `CompareMap` aligns the export's GOP-duration sequence
  back onto the source (verbatim copies keep their RAP intervals). Boundary
  (lead-in/tail) classification is heuristic; the copy/dropped alignment is solid.
- **Structure** and **Pictures** keep showing A.

Verified on a real round trip (a H.264 source + a 2-cut smart-render export from
ts-edit-gui): video, audio and the **ARIB caption (0x1305)** all diff as `same`,
captions/audio reported OK, and B shows exactly one seam where the source timing
is clean.

## Build
```powershell
$env:PATH="C:\Program Files\CMake\bin;"+$env:PATH
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH=C:\Qt\6.11.1\msvc2022_64
cmake --build build-msvc --config Release
```
The Qt runtime is deployed next to the `.exe` (windeployqt), so it runs
standalone — no Qt on PATH needed.

## Run
```powershell
.\build-msvc\Release\ts-structure-viewer.exe "path\to\file.ts"        # inspect one file
.\build-msvc\Release\ts-structure-viewer.exe "source.ts" "export.ts"  # compare mode
```

## History
This repo started as a sandbox of synthetic-data viewers for prototyping the
structure/compare widgets (and a brief, since-reverted integration into
`ts-edit-gui`). Those prototypes have been folded into the single real tool,
`ts-structure-viewer`; `ts-edit-gui` stays focused on editing, and the structure /
verification tooling lives here.

## License
MIT — see [LICENSE](LICENSE).
