# HAVEN-FSK Session Resume — 2026-07-05 (end of session)

## Branch

`cpp-rewrite`. Everything below is about to be **committed and pushed** in
this session's closing commit — working tree is clean except pre-existing
untracked scratch files (`PHASE*.md` notes, `Palettes.png`, `UI layout
change 1.png`, `Grok Suggested Improvements.txt`, `gen_h_matrix.py` at repo
root and in `cpp/`) which predate this session and were deliberately left
untouched, same as every prior session.

```
<this session's commit will land here — see `git log` for the actual hash>
598b77b Correct HAVEN-FSK spec: undisclosed Gray coding, wrong end-of-frame mechanism
b8f1343 docs: add ADR-109 through ADR-114
9ac0e3b Move AudioEngine + DspPipeline to a dedicated worker thread
56bbde7 Fix RX HTML-corruption, buffer underrun, O(N^2) demod, and stale preamble-lock bugs
ce49557 Direct Hamlib linking + PSK31 real-signal fixes  (prior session)
```

## What Was Done Since The Last Resume Note

Two threads, in order. Full detail in `DECISIONS.md` (ADR-109 through
ADR-119) — read those before re-deriving context by grepping the diff.

### 1. Decode-reliability investigation (ADR-109 through ADR-114)

Real over-the-air MFSK decode reliability degraded over a session ("decodes
twice, then never again"). Four independent, real bugs found and fixed in
sequence, each confirmed via a purpose-built diagnostic before moving to the
next:
- Widened RX buffer + gap detection for real-time sample loss (ADR-109).
- `RxDisplay` callsign-link double-substitution corrupting message display
  (ADR-110).
- `AudioEngine`/`DspPipeline` moved to a dedicated worker `QThread`, since
  ADR-109's larger buffer alone made the underlying real-time-audio-vs-UI
  contention survivable but not eliminated (ADR-112).
- O(N²) re-demodulation in `MfskModem::tryCompleteFrame()` — re-demodulating
  the entire growing RX buffer on every check instead of incrementally
  (ADR-113).
- **The actual root cause of the whole saga**: a stale high-water-mark in
  `PreambleSync`'s peak-picking (`m_runBestScore` never reset), causing
  every lock after the first to freeze on the *first* run's position
  forever (ADR-114) — took three iterations to fix without regressing
  `MfskLoopbackSelfTest`.

**User-confirmed result**: "clean decode at 1dB SNR" in live testing after
all four fixes landed.

Also corrected `HAVEN-FSK_Specification.md` against the actual current
code (undisclosed Gray coding, wrong end-of-frame mechanism), scoped
tightly to FCC §97.309 technical-characteristics disclosure — not a
how-to-build-a-decoder document, not UI/software/dependency detail.

### 2. UI: dockable panels + several smaller fixes (ADR-115 through ADR-119)

- **ADR-115**: `<myQTH>` macro tag was expanding to the operator's name
  (copy-paste bug in `MacroPanel.cpp`) — fixed, and added an actual QTH
  settings field (free text; "QTH" has no single fixed meaning in ham
  convention, operator's own choice of detail). Also fixed a real regex bug
  in `RxDisplay::renderMessage()` that silently truncated NAME/QTH/GRID/
  POTA click-to-populate values containing the letters N/Q/G/R/P/S/F.
- **ADR-116**: `FrequencyControl` had no way to manually enter a frequency
  with no radio connected (three independent dead ends in the placeholder
  state); fixed, plus added right-click direct numeric MHz entry for fast
  large jumps.
- **ADR-117**: Macro editor's tag reference list is now clickable —
  inserts at cursor instead of requiring exact manual typing.
- **ADR-118**: Preamble-sync-idle diagnostic (~1/sec heartbeat) now off by
  default, opt back in via `HAVEN_VERBOSE_SYNC` env var.
- **ADR-119 (the big one)**: `MainWindow`'s fixed-order `QSplitter` layout
  replaced with five independently movable/resizable `QDockWidget` panels
  (Waterfall, Received, Log, Levels, Transmit — Transmit holds both Macro
  Panel and TX input together, per operator preference after initially
  splitting them apart). Station info and frequency/mode/squelch/rig/RX
  status moved into a fixed top toolbar; only the free-text status message
  stays in a bottom toolbar. **Three non-obvious Qt pitfalls hit and fixed
  — see ADR-119 for full detail, but the short version for future work in
  this area**:
  1. Dock widgets/toolbars need `setObjectName()` or `saveState()`/
     `restoreState()` silently won't identify them correctly.
  2. `QSizePolicy::Fixed` actively fights manual dock resize (Qt keeps
     snapping back to the size hint) — use `QSizePolicy::Maximum` for
     "shrinkable but never stretched past natural content size."
  3. `splitDockWidget()` calls made during construction (before the window
     is ever shown) can leave a dock floating instead of tiled on first
     launch — `setFloating(false)` after the default arrangement is built
     is the fix, only when not restoring a saved state.

  Frequency display also resized 50% larger (font, bounds, step buttons)
  per operator request — cosmetic only.

## Current State

### Build
Clean build (`build.bat`, confirmed multiple times across this session's
edits). Self-tests pass on every rebuild (Debug build runs
`runFecSelfTest()`/`runFrameSelfTest()`/`runAudioSelfTest()` automatically
before `MainWindow` is constructed — verified via `Start-Process`/exit-code
check pattern, not just "it compiled").

### UI dockable-panel verification
Confirmed via screenshot (default arrangement, panel bundling, the
floating-dock fix) and interactively by the operator (drag, resize, float,
restart-persistence) — the `QSizePolicy::Maximum` fix (pitfall #2 above)
was specifically confirmed to resolve the reported "resizing doesn't
stick" symptom.

## What Has NOT Been Verified

- CAT control against a live rig with the current dock-widget top-bar
  layout (rig status label moved from bottom bar to top bar this session —
  functionally identical wiring, just relocated, but not re-confirmed live
  since the move).
- fldigi PSK31 interop re-test after the missing-TX-preamble fix from the
  Hamlib/PSK31 session (`ce49557`) — still outstanding, carried forward
  from that session's resume note; nothing in this session touched PSK31.
- Real hardware RX session specifically re-confirming ADR-114's fix holds
  over many hours / varied signal conditions, beyond the one strong live
  test already confirmed.

## Deferred / Not Yet Done (see DECISIONS.md / spec §7.2, §5's "5")

Unchanged from the prior resume note — none of this session's work touched
these:
- Transmit backoff / DCD-gated collision avoidance.
- MFSK pulse shaping (raised-cosine tried and reverted post-CPFSK).
- QPSK for PSK31 (BPSK only).
- `DCD_RMS_THRESHOLD` tuning (`Psk31Modem.h`, still an untuned placeholder).
- Adaptive/configurable FEC rate, narrower "weak signal" MFSK variant.

## Key Files for the New Architecture

Carried forward from the prior resume note, plus this session's additions:
- `src/dsp/PreambleSync.{h,cpp}` — `m_runBestScore`/`m_lastRunEndSample`/
  `RESET_GAP_SAMPLES` is the ADR-114 fix; do not touch the reset-gating
  logic without re-reading ADR-114 in full first.
- `src/dsp/MfskModem.cpp` — `tryCompleteFrame()`'s incremental-demod state
  (ADR-113); the `HAVEN_VERBOSE_SYNC`-gated idle diagnostic (ADR-118).
- `src/audio/AudioEngine.{h,cpp}`, `src/dsp/DspPipeline.{h,cpp}` — now
  constructed on a dedicated worker `QThread` (ADR-112); call sites from
  `MainWindow` go through `QMetaObject::invokeMethod`.
- `src/ui/MainWindow.{h,cpp}` — `setupUi()` is now dock-widget construction
  (ADR-119), not a `QSplitter`. `m_dockWaterfall`/`m_dockReceived`/
  `m_dockLog`/`m_dockLevels`/`m_dockTransmit`, `m_topBar`/`m_bottomBar`.
- `src/ui/LevelPanel.h` — `QSizePolicy::Maximum` on both axes (ADR-119
  pitfall #2); do not change back to `Fixed` or `Preferred` without
  rereading why both were tried and rejected.
- `src/ui/FrequencyControl.h` — placeholder bootstrap + right-click entry
  (ADR-116); font/size constants scaled 1.5x from their original values.
- `src/ui/RxDisplay.cpp` — `renderMessage()`'s tag regex (ADR-115);
  `makeLink()`/`onAnchorClicked()` callsign-link logic (ADR-110, untouched
  this session).
- `src/radio/RadioSettings.h` — `StationInfo::qth` (ADR-115).
- `src/ui/MacroPanel.cpp` — `onMacroRightClicked()`'s clickable tag list
  (ADR-117); the `<myQTH>` fix (ADR-115).

## Known Verified-Correct Items (do not re-investigate without new evidence)

Everything in the prior RESUME.md's list still holds (Hamlib struct tag,
fldigi PSK31 preamble length, `.lib` vs `.dll.a` linking gotcha).
Additionally, confirmed this session:
- The "decode reliability degrades over a session" complaint that predated
  this entire project's C++ rewrite testing was root-caused to
  `PreambleSync`'s stale high-water-mark (ADR-114), not RF propagation,
  not a single simple bug — it took four separate, real fixes (ADR-109,
  112, 113, 114) to fully resolve, confirmed by the operator directly
  ("clean decode at 1dB SNR").
- `QMainWindow::saveState()`/`restoreState()` require every dock widget
  and toolbar to have a unique `objectName()` — window title is not
  sufficient and the failure mode (silently not restoring correctly) is
  not obvious without knowing to check for this specifically.
- `QSizePolicy::Fixed` on a widget placed inside a `QDockWidget` actively
  prevents the operator from manually resizing that dock via drag — Qt's
  dock layout keeps re-snapping it to the size hint. `Maximum` is the
  correct policy for "has a natural default size, shrinkable on demand,
  never stretched beyond it."
