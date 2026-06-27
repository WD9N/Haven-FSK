# HAVEN-FSK Session Resume — 2026-06-26 (updated end-of-day)

## Branch
`cpp-rewrite` — all uncommitted changes described below build clean.

## What Was Done (Previous Session)

### Deep evaluation of DSP decoding pipeline
All source files in `src/dsp/` were audited for bugs affecting decoding by
time-of-arrival and frequency. The bugs were all in code, not configuration.

### Fixes applied (all in `src/dsp/`)

#### Bug 1 — resetRx() dead zone (HIGH) ✅ FIXED
**Symptom:** After each successful decode (or timeout), preamble detection was
blind for ~680 ms.

**Root cause:** `resetRx()` cleared `m_preTrigger` and set `m_scanTicks = 0`.
With `SCAN_INTERVAL_CHUNKS = 8`, the first scan after reset fired at chunk 8
with only 16384 samples — below the 24576 minimum for preamble detection.
First viable scan was at chunk 16 = 682 ms.

**Fix in `DspPipeline.cpp`:**
- During `Collecting` state, `m_preTrigger` is updated with each incoming
  chunk (same trim logic as Idle). By decode time it contains only
  post-preamble audio — no stale preamble to re-trigger on.
- `resetRx()` no longer clears `m_preTrigger`.
- `resetRx()` sets `m_scanTicks = SCAN_INTERVAL_CHUNKS - 1` so the next
  scan fires on the very next chunk.

#### Bug 2 — nBlocks out-of-range causes 20-30 s hang (HIGH) ✅ FIXED
**Symptom:** A noise burst corrupting the header byte causes the pipeline to
wait for `nBlocks * 48` symbols that never arrive.

**Fix:** `MAX_NBLOCKS = 125` guard added in both `DspPipeline::tryCompleteFrame()`
and `Frame::parse()`. Out-of-range nBlocks immediately discards the frame.

#### Bugs 4+5 — fecIterations always 0 / double LDPC decode (MEDIUM) ✅ FIXED
**Fix:** `FEC::decodeMessage()` return type changed to
`DecodeMessageResult {bytes, allConverged, totalIterations}`.
`Frame::parse()` uses the result directly — redundant second `decodeBlock()`
removed. `fecIterations` now flows through `ParseResult` → `RxMessage` →
RS computation.

**Files changed:** `FEC.h`, `FEC.cpp`, `FecSelfTest.h`, `Frame.h`,
`Frame.cpp`, `DspPipeline.cpp`.

#### Bug 6 — CRC failure on text with trailing spaces (LOW) ✅ FIXED
**Fix in `Frame.cpp`:** `stripPadding()` now removes trailing null bytes then
exactly one trailing space — no more whitespace trimming loop.

#### Bug 7 — Dead code removed ✅ FIXED
Removed: `applyAfcCorrection()`, `updateAfcTracking()`, `m_afcPhase`,
`AFC_TRACK_ALPHA`, `AFC_RESET_DECAY`, `buildHannWindow()` / `m_hannWindow[]`
in Demodulator, `buildToneTable()` / `m_toneTable` in Modulator.

---

## What Was Done (This Session — 2026-06-26 Afternoon)

### Audio format mismatch identified and fixed
**Root cause found:** VAC was configured as 2-channel (stereo) based on
incorrect earlier advice. `pcmToFloat()` interpreted interleaved L,R int16
pairs as sequential mono samples, halving the apparent sample rate and
destroying all tone bin mapping.

**Fix applied:** User changed Windows VAC `Line 1 (Virtual Audio Cable)` to
`1 channel, 16 bit, 48000 Hz` on both Playback and Recording sides.

**Confirmed by:** `AudioEngine: RX actual format — 48000 Hz, 1 ch, Int16`

**Code added in `AudioEngine.cpp`:** Post-start format verification logs the
negotiated format and emits a UI error if sample rate or channel count doesn't
match the requested format. Makes VAC misconfigurations immediately visible.
See ADR-100.

### Preamble still not detecting — investigation ongoing

**Symptoms:**
- All self-tests pass (FEC, Frame, Audio)
- DspPipeline demod self-test: score 0.867938 [PASS]
- Real HAVEN signal visible in waterfall and audible on RX radio (dummy loads)
- Preamble scan scores: max 0.23 observed (threshold 0.30)
- Frac values are inconsistent: adjacent positions expecting the same tone
  score wildly differently (0.87 and 0.00 for tone 0 two symbols apart)

**Critical observation:** Inconsistent per-position frac values within the
same scan window indicate signal destruction, not frequency offset or weak
signal. A uniform frequency error produces consistently low fracs; a filter
or DSP process removing tones selectively produces the irregular pattern seen.

**Primary hypothesis: Thetis ANF (Auto Notch Filter)** — ANF detects and
removes steady-state tones, which is exactly what HAVEN's CPFSK tones look
like to a spectral NR algorithm. The waterfall (runs before DSP chain) shows
the signal while the VAC audio output (after DSP chain) has tones suppressed.

**ACTION REQUIRED before next session:**
In Thetis DSP panel, turn OFF: **NR** (Noise Reduction), **ANF** (Auto Notch
Filter), **NB1**, **NB2** (Noise Blankers). Set AGC to Slow or Off.
Then rebuild and test with the new `maxf` diagnostic.

### maxf diagnostic added to preamble scan log

**File changed:** `src/dsp/DspPipeline.cpp` (`tryFindPreamble()` logging).

The scan log now shows a fourth line `maxf:`. `maxf[i]` is the fraction of
total HAVEN-band energy at the ACTUAL dominant tone (argmax), while `frac[i]`
is the fraction at the EXPECTED tone.

**How to read it:**
- `maxf ≈ 0.85, frac ≈ 0.85` → correct tone detected, good signal ✓
- `maxf ≈ 0.85, frac ≈ 0.00` → signal at WRONG tone (freq offset > ±40 Hz)
- `maxf ≈ 0.06–0.12` everywhere → signal destroyed (ANF/NR likely cause)

---

## Current State

### Build
Clean build on `cpp-rewrite`. Executable at `c:\Haven\cpp\build\HavenFSK.exe`.

### Modified files (all committed after this session)
```
M DECISIONS.md                 — ADR-100 added
M src/audio/AudioEngine.cpp    — format mismatch detection + logging
M src/dsp/Demodulator.cpp
M src/dsp/Demodulator.h
M src/dsp/DspPipeline.cpp      — demod self-test, full RX rewrite, maxf diag
M src/dsp/DspPipeline.h
M src/dsp/FEC.cpp
M src/dsp/FEC.h
M src/dsp/FecSelfTest.h
M src/dsp/Frame.cpp
M src/dsp/Frame.h
M src/dsp/Modulator.cpp
M src/dsp/Modulator.h
M src/dsp/Preamble.cpp
M src/dsp/Preamble.h
```

### Active AFC state
AFC is currently **disabled** (`m_afcEnabled = false` in DspPipeline.h).
The 5-hypothesis frequency scan (±40 Hz in 20 Hz steps) is always active.
When AFC is disabled, the bin offset from preamble detection is used for that
frame but not persisted across frames.

### Key protocol constants (do not change)
See `src/dsp/Constants.h`. Critical values:
- `SAMPLE_RATE = 48000` (only valid rate)
- `SYMBOL_RATE = 31.25` Hz
- `SAMPLES_PER_SYMBOL = 1536`
- `FFT_SIZE = 12288` (8× zero-padded)
- `PREAMBLE_LENGTH = 16`
- `SOFT_PREAMBLE_THRESHOLD = 0.30f`
- `COARSE_PREAMBLE_THRESHOLD = 0.18f`
- `SCAN_INTERVAL_CHUNKS = 8` (≈340 ms)

---

## Deferred / Not Yet Done

### Bug 3 — O(n²) re-demodulation during frame collect (MEDIUM, deferred)
`tryCompleteFrame()` re-demodulates the entire growing `m_rxBuffer` on every
call. For large messages on slow hardware (Raspberry Pi), total FFT work grows
as O(n²). No concrete report yet; defer until needed.

**Fix when needed:** Add `m_softSymbolCache` in `DspPipeline`. Only
demodulate newly arrived symbols; invalidate cache on `resetRx()`.

### End-to-end decode not yet verified (BLOCKED on Thetis configuration)
The DSP pipeline is correct — self-tests all pass. The blocker is suspected
Thetis DSP processing (ANF) destroying HAVEN tones before they reach HAVEN.
Next step: disable Thetis NR/ANF/NB and test with `maxf` diagnostic.

---

## Known Verified-Correct Items
(Do not re-investigate these without new evidence)
- `softToLLR` bit ordering: MSB-first, consistent with modulator nibble encoding
- BPSK sign convention: `0→+1.0f, 1→-1.0f` encode; `total < 0 → bit 1` decode
- FEC systematic bit extraction: both paths use `m_colPerm[0..K-1]`
- Frame wire layout: preamble(16) + header_copy1(4) + header_copy2(4) + CRC(4)
  + payload(n×48) — consistent between `assemble()` and `parse()`
- `m_timingOffset` / `m_preambleSymOff` alignment: correctly describes the
  preamble start at `bestTiming` samples
- `measureToneOffset()`: returns near-zero for HAVEN (integer cycles property)
- CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, symmetric at TX and RX
- Tone bins are exact integers: no rounding error in `buildToneBins()`, no
  guard-bin leakage between adjacent tones (8 bins apart, guard ±3 bins)
- Demodulator uses rectangular window (correct — all tones produce exactly
  integer cycles per symbol; Hann would reduce tone discrimination)
