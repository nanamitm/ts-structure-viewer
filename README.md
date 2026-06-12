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

## Next direction: two-file comparison viewer
A dedicated viewer that loads **two files side by side** (e.g. the source TS and
the smart-rendered export) and aligns their structure on a shared viewport, to
verify a smart-render round-trip:
- two stacked structure lanes sharing one zoom/pan range
- diff the GOP/RAP layout: which keyframes are copied verbatim vs. re-encoded
- highlight the cut seams and check continuity (PTS/DTS/PCR, CC) across them
- stream/PSI diff (PIDs, ARIB caption, audio tracks preserved through the cut)

Build it here against synthetic A/B data first, then port the stable widget
into `ts-edit-gui`.

### Later drill-downs
- PTS/DTS/PCR continuity graph (GUI of `tools/ts_pts_scan.py`)
- picture-type (I/P/B) / open-vs-closed GOP view
