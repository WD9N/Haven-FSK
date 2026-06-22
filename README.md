# HAVEN-FSK

**16-tone MFSK HF Digital Mode for Amateur Radio**  
Emission designator: **500HJ2D** | Bandwidth: **500 Hz** | Net rate: **~62 bps**

---

## What is HAVEN-FSK?

HAVEN-FSK is an HF digital mode for free-text conversational communication.
It delivers ~62 bps net throughput in 500 Hz bandwidth with LDPC forward
error correction — 50% faster than Olivia 16/500 and twice as fast as
PSK31, while matching Olivia's occupied bandwidth exactly.

| Mode          | Net bps      | ~WPM        | Weak Signal | Free Text | FEC |
|---------------|--------------|-------------|-------------|-----------|-----|
| FT8           | ~10 bps      | ~15 WPM     | Excellent   | No        | Yes |
| PSK31         | ~31 bps      | ~47 WPM     | Poor        | Yes       | No  |
| Olivia 16/500 | ~42 bps      | ~63 WPM     | Good        | Yes       | Yes |
| **HAVEN-FSK** | **~62 bps**  | **~94 WPM** | **Good**    | **Yes**   | **Yes** |

---

## Current Version

**v0.2.0 — C++ / Qt6 rewrite (cpp-rewrite branch)**  
Pre-release development. Published for FCC Part 97 §97.309 disclosure.
Not yet tested on the air.

The v0.2.0 rewrite is a complete reimplementation in C++17 / Qt6 with no
Python dependency. See [CHANGELOG.md](CHANGELOG.md) for the full feature list.

---

## Key Features (v0.2.0)

- **16-tone MFSK** — 500 Hz bandwidth, 500–968.75 Hz audio range
- **LDPC(192,96) FEC** — rate 1/2 forward error correction, CRC-16 verification
- **Free text** — type anything, no rigid exchange format required
- **Waterfall display** — four color palettes, adjustable speed and range, 120-row history
- **AFC** — digital RX-only NCO correction ±75 Hz; VFO and TX frequency never move
- **Clickable RX display** — callsigns and structured tags (POTA/SOTA/grid/RS/name)
  are clickable and auto-populate log entry fields
- **TX in RX window** — transmitted messages shown in amber for conversation continuity
- **RS signal reports** — computed from FEC convergence and DCD SNR, not conventional 599
- **rigctld** — Hamlib server support for IC-705, IC-7300, TS-590SG, FT-891, G90,
  and most HF radios
- **TCI WebSocket** — direct integration with Thetis, ExpertSDR, HPSDR (PTT + VFO)
- **Three-tier TX backoff** — CQ/Activator/Standard delays, DCD gating, 120-sec watchdog
- **POTA / SOTA / Field Day** — activity-aware logging with correct ADIF export per program
- **SQLite log** — WAL crash-safe, written immediately on Log It, no contacts lost
- **Inline log edit** — double-click any logged QSO to edit or delete in place
- **Macro system** — 2 banks × 8 buttons with variable tags; `<TX>` for auto-transmit
- **FCC Part 97 compliance** — TX blocked until callsign entered; cites §97.119

---

## Quick Start (v0.2.0 — Windows)

**Requirements:**
- Windows 10/11 64-bit
- An SSB radio with USB audio interface or VAC
- Optionally: Hamlib rigctld running, or Thetis/HPSDR with TCI enabled

**Build from source:**
```
git clone https://github.com/WD9N/Haven-FSK.git
git checkout cpp-rewrite
cd cpp
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64
cmake --build . --config Release
```

Qt 6.11 with MinGW 13 is required. See `cpp/DECISIONS.md` for build notes.

**First-time setup:**
1. Launch `HavenFSK.exe`
2. Click **Radio** on the menu bar → enter your radio control method and connect
3. Open **File → Settings** → enter your callsign, grid square, and state/county
4. For POTA activations: add your park reference(s) in XX-NNNN format
5. Select your audio devices in Settings → Audio
6. Set radio to USB or DIGU mode; tune to a HAVEN-FSK frequency

---

## Suggested Operating Frequencies (USB dial)

| Band | Frequency   | Notes                                        |
|------|-------------|----------------------------------------------|
| 80m  | 3.585 MHz   | Near Olivia calling — MFSK neighborhood      |
| 40m  | 7.065 MHz   | Gap between JS8Call and PSK31 upper cluster  |
| 30m  | 10.142 MHz  | Digital-only band, clear gap                 |
| 20m  | 14.090 MHz  | Primary recommended frequency — clear gap    |
| 17m  | 18.108 MHz  | Above JS8Call, avoids FT8 at 18.104          |
| 15m  | 21.090 MHz  | Clear gap between RTTY and WSPR              |
| 10m  | 28.130 MHz  | Above WSPR/JS8Call cluster, wide sub-band    |
| 6m   | 50.323 MHz  | Above JS8Call, Es openings only              |

Signal appears 500–968 Hz above the dial frequency.

> **⚠️ Monitor before transmitting.** Always listen before transmitting.
> 3.590 MHz (80m) and 7.040 MHz (40m) are established RTTY DX calling
> frequencies — never use these. See [BAND_PLAN.md](BAND_PLAN.md).

---

## UI Layout (v0.2.0)

```
┌─────────────────────────────────────────────────┐
│ File  Radio  Operating  Help                    │  Menu bar
├─────────────────────────────────────────────────┤
│ [WD9N  DN31  IL]  [POTA: US-1234]  [ACTIVATOR] │  Station info
├──────────────────────────────────────┬──────────┤
│                                      │  Splitter│
│  Waterfall (120 rows, Earth palette) │  (drag-  │
│  -- gray passband markers --         │  gable)  │
│  -- green AFC tracking lines --      │          │
├──────────────────────────────────────┤          │
│  Received (RX decoded messages)      │          │
│  [14:23] W1XXX: CQ POTA DE W1XXX... │          │
│  [14:23][TX] WD9N: W1XXX DE WD9N.. │          │
├──────────────────────────────────────┤          │
│ Log  [Time][Call][Freq][RS-R][RS-S] │          │
│      [Parks/SOTA][Grid][Notes]       │          │
│ [Their Call][RS-R][RS-S][Parks]      │          │
│ [Grid][Name][QTH][Notes]    [Log It] │          │
└──────────────────────────────────────┘          │
│ Macros: [A][B]  [CQ POTA][Stn Info][TU 73]...  │  Macro panel
├─────────────────────────────────────────────────┤
│ Transmit: [________________text___________][TX] │  TX input
├─────────────────────────────────────────────────┤
│ DCD:--  Idle  14.090000 MHz  No rig  RX:[====]  │  Status bar
└─────────────────────────────────────────────────┘
```

Splitter dividers between waterfall, RX window, and log panel are draggable.
Positions are saved and restored between sessions.

---

## Activity Support and ADIF Export

| Station info fields set   | Activity detected  | ADIF file(s) generated              |
|---------------------------|--------------------|-------------------------------------|
| POTA reference(s) only    | POTA Activation    | One `.adi` per park                 |
| SOTA reference only       | SOTA Activation    | `CALL-SUMMIT-DATE.adi`              |
| Both POTA + SOTA          | POTA/SOTA combo    | POTA files include MY_SOTA_REF      |
| FD class + section        | Field Day          | `CALL-DATE.adi` (general)           |
| Nothing                   | General            | `CALL-DATE.adi`                     |

General ADIF always generated alongside any activity-specific files.

POTA filename: `{CALL}@{PARK}-{YYYYMMDD}.adi` — per park, for upload to pota.app  
SOTA filename: `{CALL}-{SUMMIT-SANITIZED}-{YYYYMMDD}.adi`

---

## Logging

**SQLite database** at `%APPDATA%\WD9N\HAVEN-FSK\haven_fsk_log.db` (Windows).
WAL mode — each contact safe the moment you click Log It.

**Log panel workflow:**
- Single-click a completed contact row: fills entry strip for reference
- Double-click a completed contact row: enters edit mode (Log It → Update)
- Delete button appears in edit mode with confirmation dialog (default No)
- RS-S field is auto-computed from signal measurements — read-only (grey)

**POTA reference format:** XX-NNNN (e.g. US-1234, CA-0001). The software
auto-corrects missing hyphens ("US1234" → "US-1234") in all entry points.

**RS reports:** R from FEC convergence/iterations; S from SNR in dB.
Cached per station for 10 minutes. Clicking a callsign in the RX window
auto-populates RS-S in the log entry and `<rstSent>` in macros.

> **Use a primary logger.** HAVEN-FSK is pre-release. Log every contact
> in your primary station logging software (Log4OM, N1MM, Ham Radio Deluxe,
> etc.) as your authoritative record. Verify ADIF files before submitting
> to pota.app or the SOTA database.

---

## Macro System

Two banks (A and B) of 8 buttons each. Switch banks manually. Right-click
any button to open the editor. Macros with `<TX>` auto-transmit; without
`<TX>`, text is placed in the TX input for review.

| Tag            | Expands to                              |
|----------------|-----------------------------------------|
| `<myCall>`     | Your callsign                           |
| `<myParks>`    | POTA references, space-separated        |
| `<mySOTA>`     | SOTA summit reference                   |
| `<myGrid>`     | Grid square                             |
| `<myName>`     | Operator name                           |
| `<myQTH>`      | Operator name (QTH alias)               |
| `<myState>`    | State or province                       |
| `<myCounty>`   | County                                  |
| `<myFD>`       | Field Day class + section               |
| `<theirCall>`  | Callsign from log entry (when clicked)  |
| `<rstSent>`    | Auto-computed RS report                 |
| `<TX>`         | Triggers automatic transmission         |

**Default Bank A** (activating): CQ POTA, Stn Info, TU 73, QRZ?, AGN?, QSL  
**Default Bank B** (chasing/general): CQ, Stn Info, TU 73, AGN?

---

## Radio Interface Support

| Interface      | Status      | Notes                                          |
|----------------|-------------|------------------------------------------------|
| TCI WebSocket  | Implemented | Thetis, ExpertSDR, all HPSDR — PTT + VFO       |
| Hamlib/rigctld | Implemented | IC-705, IC-7300, TS-590SG, FT-891, G90, most radios |
| VOX            | Supported   | No rig control needed — audio level drives PTT  |

---

## AFC (Automatic Frequency Correction)

HAVEN-FSK AFC corrects for inter-station frequency calibration differences
and thermal VFO drift — without ever moving the radio's VFO.

- **Range:** ±75 Hz
- **Hard lock:** direct centroid measurement on preamble detection
- **Slow tracking:** α=0.02 exponential average during frame (~1.6 sec)
- **Partial reset:** offset × 0.5 on DCD drop — head start for next station
- **Waterfall:** gray markers show where signal should be; green markers
  float with AFC offset (hidden when offset < 0.5 Hz)
- **Toggle:** Operating → AFC — Auto Frequency Correct

Why TX never moves: if AFC moved the VFO, two stations would mutually
chase each other's corrections and drift up-band together.

---

## Structured Field Tags

HAVEN-FSK messages support optional structured tags for machine-parseable
data. Receiving operators can click any tag value to populate their log entry.

| Tag        | Format                  | Example                  |
|------------|-------------------------|--------------------------|
| `NAME:`    | Free text               | `NAME:David`             |
| `QTH:`     | Free text               | `QTH:Illinois`           |
| `GRID:`    | Maidenhead              | `GRID:EN52`              |
| `RS:`      | Two digits              | `RS:57`                  |
| `POTA:`    | XX-NNNN space-separated | `POTA:US-1234 US-5678`   |
| `SOTA:`    | XX/XX-NNN               | `SOTA:W7W/SE-001`        |
| `FD:`      | Class + section         | `FD:2A MWA`              |

Tags are opt-in and transmit as plain text — operators who don't use them
see normal readable messages.

---

## FCC Regulatory Compliance

HAVEN-FSK is authorized for use by licensed amateur radio operators
under FCC Part 97.

- **Emission designator:** 500HJ2D
- **Bandwidth:** 500 Hz (within the 2.8 kHz HF limit)
- **Identification:** Software blocks all TX until callsign is entered,
  citing FCC Part 97.119
- **Disclosure:** Complete technical specification published in
  [HAVEN-FSK_Specification.md](HAVEN-FSK_Specification.md) as required
  by FCC Part 97 §97.309 for unspecified digital codes

---

## Technical Specification

See **[HAVEN-FSK_Specification.md](HAVEN-FSK_Specification.md)** for:

- Tone frequencies and modulation parameters
- Frame format and header definition
- LDPC parity check matrix generation (PEG algorithm, seed 1234)
- CRC-16/CCITT-FALSE parameters
- Preamble sequence
- Performance characteristics

The mode specification is placed in the public domain. Anyone may
implement a compatible encoder or decoder without restriction.

---

## License

Copyright (C) 2026 WD9N

This software is licensed under the **GNU General Public License v3**.  
The HAVEN-FSK **mode specification** is released to the public domain.

Commercial use of the software requires written permission from the author.

---

## Author

**WD9N**  
Developed with assistance from Claude (Anthropic)

*"HAVEN — a reliable place in noisy conditions"*
