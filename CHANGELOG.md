# HAVEN-FSK Changelog

---

## v0.2.0 — June 2026 *(current — cpp-rewrite branch)*

Complete rewrite in C++17 / Qt6. This is a ground-up reimplementation
replacing the Python/tkinter prototype. No Python dependency.

### Phase 8 — Pre-Release Fixes and Debugging (June 2026)

**Critical DSP Fix — CPFSK Modulation**
- Root cause of TX audio artifacts identified: pre-built tone table reset
  phase to zero at each symbol boundary, creating 31.25 phase
  discontinuities per second audible as clicking/pulsing
- Modulator replaced with continuous phase accumulator (CPFSK) — phase
  advances continuously across all symbol boundaries
- Preamble given same continuous phase treatment; final phase seeded to
  Modulator so preamble→header boundary is seamless
- Raised cosine amplitude ramps removed — with CPFSK they created amplitude
  dips at symbol rate (31.25 Hz) rather than smoothing anything
- Result: clean, smooth HAVEN-FSK tones verified in Audacity waveform analysis
- Root cause of Python vs C++ difference: Python NumPy used continuous time
  array naturally maintaining phase; C++ tone table optimization inadvertently
  broke phase continuity

**Radio Control**
- PTTManager wired into TX sequence (was missing entirely — radio never
  received PTT commands in the initial build)
- TCI m_ready detection changed to startsWith — fixes Thetis compatibility
  where exact string match failed due to encoding artifacts
- TCI setPTT and setFrequency guard on both connected and ready state
- TX sequencing: configurable PTT lead (default 150ms) and TX tail
  (default 200ms) in Radio → Configure...
- Settings change no longer disconnects active TCI connection
- Radio → Configure... opens directly from menu bar (one click)

**Audio Engine**
- TX uses QMediaPlayer with in-memory WAV — correct Qt6 API for one-shot
  pre-computed audio playback on all platforms
- Qt6.11 FFmpeg backend identified as root cause of QAudioSink IdleState
  premature firing — QMediaPlayer bypasses this
- RX audio source fully destroyed before TX to prevent device conflict
- Platform audio backend selection in main.cpp: WMF on Windows, ALSA on Pi

**UI Fixes**
- RS-R and RS-S labels immediately adjacent to fields (fixed width)
- RS-S field visually greyed as read-only (auto-computed)
- Double-click log row enters edit mode (Log It → Update)
- Delete button in edit mode with confirmation dialog (default No)
- Frequency column added to recent contacts log table
- TX messages shown in RX window in amber with [TX] prefix and callsign
- Empty macro buttons right-clickable to open editor
- FD Exchange fields hidden when Field Day mode inactive
- POTA references auto-corrected to XX-NNNN format
- All station info fields auto-uppercase as operator types
- State/Province and County added to station information
- ADIF export includes MY_STATE and APP_HAVEN_MY_COUNTY
- FrequencyControl always editable — amber (rig) or grey (manual entry)

**Log System**
- Log database UPDATE wired to inline edit mode
- Log database DELETE wired to delete button with confirmation
- db_id stored in table row for accurate record targeting
- Frequency column in contact table (3 decimal places)

---

### Phase 9 — License Correction, PSK31 Mode, Protocol v2 (July 2026)

**License**
- Project corrected to GPLv3 from inception (ADR-101 supersedes ADR-004's
  since-incorrect GPL-rejection reasoning); `Qt6::Charts` (already linked)
  is GPLv3-only in Qt's open-source distribution, so the app was already
  implicitly GPL-obligated before this correction
- Added `LICENSE` (canonical GPLv3 text) and `THIRD_PARTY_LICENSES.md`
  (KissFFT BSD-3-Clause, Qt6 tri-license, fldigi GPLv3 attribution for
  consulted varicode table and modulation convention)

**Modem abstraction**
- New `IModem` interface (Qt-free) so the RX/TX pipeline can host more
  than one mode; `DspPipeline` refactored from an MFSK-only monolith into
  a thin Qt-facing delegator over the active `IModem`
- `MfskModem` — the existing 16-tone CPMFSK stack relocated behind
  `IModem` (state machine, AFC, tone monitor, diagnostics), not rewritten
- `Constants.h` split into shared constants and `MfskConstants.h`
  (MFSK-only) so a second mode can't collide with MFSK-specific globals

**PSK31 mode (new)**
- BPSK31/63/125 modem: `Varicode` (G3PLX table, transcribed
  programmatically from fldigi source for interop fidelity),
  `Psk31Modulator` (differential BPSK, continuous carrier phase,
  raised-cosine constellation-transition shaping), `Psk31Demodulator`
  (Costas loop carrier tracking, integrate-and-dump matched filter,
  energy-based timing nudge)
- Verified via a standalone Qt-free test build (not just app-level
  smoke test) — this caught and fixed two real bugs: matched-filter ISI
  from integrating the full raised-cosine transition window instead of
  just its settled tail, and the demodulator silently dropping the first
  transmitted character by treating it as a reference symbol instead of
  comparing it against the modulator's known initial state
- Known limitation: carrier frequency pull-in is non-monotonic at larger
  offsets (clean at 0/5 Hz, fails at 15 Hz, recovers at 30 Hz) — a
  Costas-loop/timing-recovery interaction not yet resolved; only tested
  via internal loopback, not against real hardware or another PSK31
  implementation

**MFSK protocol v2**
- Payload interleaving: row/column block interleaver spanning all LDPC
  blocks of a message, so an HF fade lands as scattered single-bit errors
  after deinterleaving rather than a burst concentrated in one block
- Header now sent 3x with bit-level majority vote (was 2x "prefer copy 1"
  since a prior, separately-dated fix — see `DECISIONS.md` ADR-099);
  3 copies is the minimum for genuine majority voting
- Both are wire-format-breaking changes bundled into one
  `PROTOCOL_VERSION` bump (1 -> 2), rejected cleanly by version mismatch
  rather than silently misdecoded
- Fine timing recovery added as an experimental, disabled-by-default
  refinement (early-late-style energy comparison during frame collection,
  separate from the existing preamble-time coarse timing sweep)

**Rig control**
- `RigctldClient` reconnect with exponential backoff on dropped
  connections, real `setSplit()` implementation (was a stub), TX power
  level get/set (`RadioInterface::getPowerLevel()`/`setPowerLevel()`,
  normalized 0.0-1.0 matching rigctld's own protocol), periodic mode
  polling with `modeChanged()` now actually emitted
- Fixed a bug where `pttChanged()` fired even when the PTT command failed
- Corrected `RadioInterface::setMode()` doc comment: Hamlib rigctld mode
  tokens for Kenwood-family data mode are `PKTUSB`/`PKTLSB`, not
  `DIGU`/`DIGL` (those are Icom front-panel labels, not Hamlib tokens)
- Optional "set radio to data mode on connect" setting in
  `RadioConfigDialog` (opt-in, off by default)

**UI**
- Mode selector (MFSK-16 / PSK31) in the status bar, persisted across
  restarts; `WaterfallWidget::setPassband()` replaces the hardcoded
  MFSK-only passband overlay so the waterfall correctly reflects
  whichever mode is active

**Documentation**
- `HAVEN-FSK_Specification.md` corrected against actual current code
  behavior in multiple places that had drifted independently of this
  session's changes: continuous-phase (CPFSK) modulator description was
  entirely missing, preamble/detection description still described an
  old hard-decision 0-16 correlation scheme rather than the current soft
  correlation + multi-hypothesis AFC frequency search, header/CRC timing
  math, effective NBLOCKS cap (125, not the 255 the field width alone
  implies), and the radio-interface table's "Hamlib/rigctld: Planned"
  (long since implemented). Transmit backoff (§7.2) is now explicitly
  documented as not-yet-implemented in the C++ version rather than
  silently describing nonexistent behavior.

---

### Architecture

- **C++17 / Qt6** — native binary, no interpreter
- **Qt6 Multimedia** — 48 kHz mono audio engine with separate RX/TX streams
- **Qt6::Sql (SQLite)** — persistent QSO log with WAL crash safety
- **Qt6 WebSockets** — TCI 2.0 protocol client
- **KissFFT** — waterfall FFT (2048-point, Hann window)
- ADR log: 58 architecture decisions recorded in `cpp/DECISIONS.md`

---

### UI and Display

**Waterfall display**
- Scrolling spectrogram, 120 rows, newest at top
- Four color palettes: Earth (default), Classic, Greyscale, Night
- Four speeds: Slow (~3 rows/sec, default) through Fast (~23 rows/sec)
- Frequency range selector: 1.5–4.0 kHz
- Two marker sets: gray dashed (fixed passband at 500/969 Hz, never move)
  and soft green solid (#6DB640, float with AFC offset, hidden when < 0.5 Hz)
- Right-click → movable tuning line with live status bar frequency preview;
  left-click confirms tune; Escape cancels
- All waterfall settings persisted in QSettings

**Frequency control**
- Always editable — works with or without rig control
- Amber text when rig-controlled, grey with "Enter MHz" placeholder when manual
- ▲/▼ step buttons; right-click to set step size (1/10/100/1000 Hz)
- Direct MHz entry with Enter

**Station information display**
- Permanent block at top of window showing callsign (red "NO CALLSIGN" if empty),
  grid square, state/county, POTA/SOTA/FD references, operator name
- ACTIVATOR badge when any POTA or SOTA reference is configured
- Updates immediately on settings change

**RX decoded message display**
- Clickable HTML elements: callsigns, POTA/SOTA/FD/grid/RS/name/QTH tags
- Clicking a callsign populates log entry and auto-computes RS report
- Transmitted messages shown in amber with `[TX] CALLSIGN:` prefix
- Structured field tags (NAME:/GRID:/POTA:/etc.) clickable to populate log fields

---

### Settings and Configuration

**Settings dialog** (two tabs)
- Station Info: callsign, grid, operator name, state/province, county,
  POTA references (unbounded list, XX-NNNN format, auto-corrected),
  SOTA reference, Field Day class/section
- Audio: input and output device selection
- All fields auto-uppercase where appropriate; callsign placeholder "Your callsign"

**Radio configuration** (Radio menu — single click, no submenu)
- Method selector: None/VOX, rigctld, TCI WebSocket
- rigctld: host/port (default 4532)
- TCI: host/port (default 50001, compatible with all TCI versions)
- Connect/Disconnect buttons inside the dialog

**POTA reference handling**
- Unbounded list — no four-park limit
- Auto-correction: "US1234" → "US-1234" (inserts missing hyphen)
- Format enforced: XX-NNNN (ISO country code prefix)
- Compact list that grows dynamically as references are added

---

### Radio Control

**rigctld client** — Hamlib server (covers virtually all HF radios)
- TCP connection to localhost:4532 (configurable)
- PTT via `T 1` / `T 0`; frequency via `f` command
- 2-second poll timer for UI frequency display

**TCI WebSocket client** — Thetis / ExpertSDR / HPSDR
- Full handshake state machine (waits for `ready;` before any TX)
- Parses `vfo:`, `modulation:`, `trx:` server push messages
- 10-second auto-reconnect
- HL2 default port 40001 stored in QSettings

**PTTManager** — three-tier TX backoff
- CQ mode: 0 ms (no delay)
- Activator mode (any POTA/SOTA reference set): 0–50 ms
- Standard mode: 50–300 ms
- DCD gates all TX; 120-second watchdog mandatory

---

### AFC (Automatic Frequency Correction)

- Digital RX-only NCO correction — VFO never moves, TX frequency unchanged
- Range: ±75 Hz (clamps and warns at limit)
- Hard lock on preamble: direct centroid measurement, immediate correction,
  NCO phase reset
- Slow tracking during frame: α=0.02 (~50 symbol periods, ~1.6 sec), tracks thermal drift
- Partial reset on DCD drop (offset × 0.5): head start for next station
- Toggle in Operating menu; waterfall AFC lines update in real time

---

### Log System

**SQLite database**
- WAL + NORMAL synchronous mode — crash safe, no contacts lost on power failure
- Written immediately on Log It — no buffering
- Location: `%APPDATA%\WD9N\HAVEN-FSK\haven_fsk_log.db` (Windows)
- All station info fields snapshotted per QSO at log time (rover-safe)
- Migrations: new columns added non-destructively to existing databases

**Log panel**
- Unified entry strip (two rows) + contact table in one panel
- 8 columns: Time, Callsign, Freq MHz, RS-R, RS-S, Parks/SOTA, Grid, Notes
- RS-R label immediately adjacent to field (no gap); RS-S greyed as read-only
- "Double-click row to edit" hint label in entry strip
- Context-adaptive: POTA/SOTA fields shown when activator; FD exchange in FD mode
- Single-click: populate fields from completed row
- Double-click: enter inline edit mode (Log It → Update, Clear → Cancel Edit)
- Delete button appears in edit mode with confirmation dialog (default No — safe)

**RS signal reports**
- Computed from physical measurements, not conventional 599
- R: from FEC convergence (R1=failed, R3=marginal, R4=good, R5=excellent)
- S: from DCD band SNR (S1–S9 mapped at 6 dB/S-unit)
- Cached per sender callsign for 10 minutes; auto-populated when callsign clicked

**ADIF export**
- File → Export Log... with current UTC day (default) or date picker
- Preview shows exact files to be generated before export
- POTA: one file per park (`CALL@PARK-YYYYMMDD.adi`), MY_SIG + MY_SIG_INFO
- Combined POTA+SOTA: POTA files include MY_SOTA_REF
- SOTA-only (no POTA): `CALL-SUMMIT-YYYYMMDD.adi` with MY_SOTA_REF
- General ADIF always generated: `CALL-YYYYMMDD.adi`
- MY_STATE standard ADIF field; county in APP_HAVEN_MY_COUNTY
- MODE: DIGITAL, SUBMODE: HAVEN-FSK in all exports

---

### Macro System

Two banks (A/B) of 8 buttons; manual bank switching; right-click to edit.

**Tags:**
- `<myCall>` `<myParks>` `<mySOTA>` `<myGrid>` `<myName>` `<myQTH>`
- `<myState>` `<myCounty>` `<myFD>`
- `<theirCall>` `<rstSent>`
- `<TX>` — auto-transmit when macro clicked (without `<TX>`: fills TX input)

Default macros pre-populated on first run (Bank A: activating, Bank B: chasing).
Empty slots visually distinct (dashed border) but right-click always works.

---

### FCC Part 97 Compliance

- TX blocked and TX button disabled if callsign is empty
- Warning banner and confirmation dialog cite FCC Part 97.119
- Guard checked on startup and every settings change

---

## v0.1.1-alpha — June 2026 *(Python prototype)*

### New Features

**Station bar**
- Callsign, Park, Summit, and Field Day class/section inline-editable
  fields directly in the toolbar
- Park and Summit fields validate format on focus-out
- Multi-park entry: comma-separated values (e.g. `US-1234, US-5678`)

**Activity-aware logging**
- Activity auto-detected from station bar fields
- Hunting modes (POTA hunting, SOTA chasing) when activity selected
  but no park/summit entered

**Integrated QSO logger**
- Inline contact entry in the log panel
- Auto-fills Call field from detected callsigns
- Band auto-populated from TCI frequency
- Session saved to `Logs/` subfolder; offered to resume on next launch

**ADIF / Cabrillo export**
- POTA activation: one ADIF file per park
- Multi-park: P2P contacts duplicated per their park
- SOTA activation: ADIF with `MY_SOTA_REF`
- Field Day: ADIF + Cabrillo (`.log`)

**Macro system**
- 17 variable tags with live preview editor

**UDP external logger broadcast**
- Settings → External Logger: enable UDP broadcast to N1MM Logger+,
  Log4OM, Ham Radio Deluxe

**Frequency display**
- Seven-digit display with mousewheel-per-digit tuning

### Bug Fixes
- Export dialog no longer freezes on Windows
- `Launch HAVEN-FSK.bat` uses `%~dp0`-relative paths
- All dialog windows use `transient()` instead of `grab_set()`

### Performance
- Waterfall rate-limited to 10 fps
- Single `_master_tick` loop at 100ms
- Config saves only on focus-out, never on every keystroke

---

## v0.1.0-alpha — June 2026 *(Python prototype)*

Initial public release to satisfy FCC Part 97 §97.309 disclosure requirement.
Published prior to any on-air testing.

### Working at release
- 16-tone MFSK modulation and demodulation
- LDPC(192,96) rate-1/2 forward error correction
- CRC-16 frame integrity verification
- 512ms preamble detection and frame sync
- TCI WebSocket PTT control (Thetis/HPSDR)
- VAC audio engine with separate RX and TX streams
- Scrolling waterfall with adjustable bandwidth slider
- DCD carrier detect with listen-before-talk collision avoidance

### Known Issues at v0.1.0
- VAC TX audio dropout under some conditions
- VAC loopback echo after PTT release
- DCD triggers on any passband energy, not HAVEN-FSK-specific tones
- No Hamlib/rigctld interface (TCI only)
- No waterfall click-to-tune / AFC

---

## Planned

- Waterfall display — Phase 7 complete; Qt paint-based spectrogram
- Log viewer dialog — inline double-click edit covers immediate need
- LoTW direct upload via TQSL (external)
- POTA spotting — POST to pota.app after activation
- Signal-specific DCD — discriminate HAVEN-FSK from FT8/RTTY by
  tone energy at the 16 specific tone frequencies
- Multi-decoder — simultaneous decode on multiple waterfall signals
- Windows installer — NSIS or WiX packaging
