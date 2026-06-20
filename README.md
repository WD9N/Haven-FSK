# HAVEN-FSK

**16-tone MFSK HF Digital Mode for Amateur Radio**  
Emission designator: **500HJ2D** | Bandwidth: **500 Hz** | Net rate: **~62 bps**

---

## What is HAVEN-FSK?

HAVEN-FSK is an HF digital mode for free-text conversational communication.
It delivers ~62 bps net throughput in 500 Hz bandwidth with LDPC forward
error correction — 50% faster than Olivia 16/500 and twice as fast as
PSK31, while matching Olivia's occupied bandwidth exactly.

| Mode          | Net bps  | ~WPM     | Weak Signal | Free Text | FEC |
|---------------|----------|----------|-------------|-----------|-----|
| FT8           | ~10 bps  | ~15 WPM  | Excellent   | No        | Yes |
| PSK31         | ~31 bps  | ~47 WPM  | Poor        | Yes       | No  |
| Olivia 16/500 | ~42 bps  | ~63 WPM  | Good        | Yes       | Yes |
| **HAVEN-FSK** | **~62 bps** | **~94 WPM** | **Good** | **Yes** | **Yes** |

---

## Key Features

- **16-tone MFSK** — 500 Hz bandwidth, 500–968.75 Hz audio range
- **LDPC(192,96) FEC** — rate 1/2 forward error correction
- **CRC-16** — frame integrity verification
- **Free text** — type anything, no rigid exchange format
- **Conversational pace** — ~94 WPM, suitable for ragchew and nets
- **Preamble sync** — 512ms preamble for reliable frame detection
- **DCD + backoff** — collision avoidance on busy channels
- **TCI support** — direct integration with Thetis/HPSDR (PTT + frequency display)
- **Mousewheel VFO tuning** — click any frequency digit, scroll to tune
- **Hamlib/rigctld** — works with virtually any radio (planned)
- **POTA/SOTA/Field Day** — activity-aware logging with ADIF and Cabrillo export
- **Macro system** — customisable quick-send buttons with variable substitution
- **UDP broadcast** — real-time QSO broadcast to N1MM, Log4OM, Ham Radio Deluxe

---

## Quick Start

**Requirements:**
- Windows, Linux, or macOS
- Python 3.8+
- An SSB radio with audio interface (USB audio, SignaLink, VAC)

**Installation:**

```
1. Download all files to a folder
2. Double-click install_and_run.py
3. Follow the prompts
4. Use the launcher for your OS:
   - Windows: double-click `Launch HAVEN-FSK.bat`
   - Linux: run `./launch_haven_fsk.sh`
   - macOS: double-click `launch_haven_fsk.command`
```

**First time setup:**
1. Enter your callsign in the **Call** field in the station bar
2. Select your audio input (from radio) and output (to radio) devices
3. Set your radio to USB or DIGU mode
4. Tune to a HAVEN-FSK frequency (see suggested frequencies below)
5. For POTA activations enter your park reference(s) in the **Park** field
6. For SOTA activations enter your summit reference in the **Summit** field

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

> **⚠️ Monitor before transmitting.** Always listen on the frequency
> before transmitting. 3.590 MHz (80m) and 7.040 MHz (40m) are
> established RTTY DX calling frequencies — never use these.
> See [BAND_PLAN.md](BAND_PLAN.md) for full frequency guidance.

---

## Activity Support

HAVEN-FSK automatically detects your operating context from the station bar:

| Station bar fields filled | Detected activity        | Export generated              |
|---------------------------|--------------------------|-------------------------------|
| Park field only           | POTA Activation          | Per-park ADIF (one file each) |
| Summit field only         | SOTA Activation          | ADIF with MY\_SOTA\_REF       |
| Both Park and Summit      | POTA + SOTA combo        | Both files simultaneously     |
| FD class + section        | Field Day                | ADIF + Cabrillo (.log)        |
| Park + FD class           | POTA activation + FD     | Both files simultaneously     |
| Nothing filled            | General Chat             | Single ADIF log               |

Multi-park activation (two-fer, three-fer): enter parks comma-separated
in the Park field — `US-1234, US-5678`. One ADIF file per park is exported,
each containing all QSOs. P2P contacts with multi-park activators are
duplicated once per their park within each file, as required by POTA rules.

---

## Logging

HAVEN-FSK includes an integrated QSO logger. Each logged contact is saved
to a session file in the `Logs/` subfolder and can be exported to ADIF or
Cabrillo (Field Day) at any time.

### Important — Use a Primary Logger

> **⚠️ Strong recommendation: always log every contact in your primary
> station logging software (Log4OM, N1MM Logger+, Ham Radio Deluxe, DX4WIN,
> etc.) in addition to the HAVEN-FSK built-in log.**
>
> HAVEN-FSK is a new, pre-release application. Its built-in logger is
> functional but has not been tested at scale. Your primary logger is your
> authoritative record for LOTW, eQSL, DXCC, and any award program.
> The HAVEN-FSK log should be treated as a supplementary reference, not
> your station log of record.
>
> For POTA and SOTA activations especially, confirm your ADIF upload to
> pota.app or the SOTA database from a verified ADIF file — open it in a
> text editor and check that MY\_SIG\_INFO, TIME\_ON, and CALL fields look
> correct before submitting.

### UDP Broadcast to External Loggers

Enable **Settings → External Logger** to broadcast each logged QSO
automatically to your primary logging software via UDP. This is the
recommended workflow — log in HAVEN-FSK and have contacts appear
simultaneously in your main logger without double-entry.

Compatible with any logger that accepts WSJT-X UDP broadcasts:
- **N1MM Logger+** — receives automatically on port 2333
- **Log4OM** — enable WSJT-X integration in preferences
- **Ham Radio Deluxe** — DM780 UDP logging listener
- **Any ADIF-UDP compatible logger**

### ADIF Export

Click **Export...** in the recent log panel, or use **Activity → POTA/SOTA**
context. The export dialog shows exactly which files will be created and
where they will be saved before writing anything.

| Activity        | File(s) created                              |
|-----------------|----------------------------------------------|
| General Chat    | `WD9N_LOG_20260620.adi`                      |
| POTA (single)   | `WD9N@US-1234-20260620-IN.adi`               |
| POTA (two-fer)  | `WD9N@US-1234-20260620-IN.adi` + `WD9N@US-5678-20260620-IN.adi` |
| SOTA            | `WD9N-SOTA-W9-IN-001-20260620.adi`           |
| Field Day       | `WD9N_FieldDay_20260620.adi` + `WD9N.log`   |

---

## Macros

Quick-send macro buttons appear above the recent log panel. Click any button
to transmit the macro immediately. Right-click to edit.

### Available tags

| Tag             | Expands to                          |
|-----------------|-------------------------------------|
| `<CALL>`        | Your callsign                       |
| `<PARK>`        | First park reference                |
| `<PARKS>`       | All parks, comma-separated          |
| `<SUMMIT>`      | SOTA summit reference               |
| `<GRID>`        | Grid square                         |
| `<STATE>`       | State/province code                 |
| `<FDCLASS>`     | Field Day class (e.g. 1E)           |
| `<FDSECTION>`   | Field Day section                   |
| `<FDEXCHANGE>`  | Full FD exchange (class + section)  |
| `<POWER>`       | TX power in watts                   |
| `<THEIRCALL>`   | Last detected contact callsign      |
| `<THEIRPARK>`   | Their park reference (P2P)          |
| `<THEIRSUMMIT>` | Their summit reference (S2S)        |
| `<BAND>`        | Current band                        |
| `<FREQ>`        | Current frequency in MHz            |
| `<DATE>`        | UTC date (YYYYMMDD)                 |
| `<TIME>`        | UTC time (HHMM)                     |

The macro editor (right-click any button) provides one-click tag insertion
and a live preview showing the expanded text with your current station values.

---

## FCC Regulatory Compliance

HAVEN-FSK is authorized for use by licensed amateur radio operators 
under FCC Part 97.

- **Emission designator:** 500HJ2D
- **Bandwidth:** 500 Hz (within the 2.8 kHz HF limit)
- **Disclosure:** The complete technical specification is published in
  [HAVEN-FSK_Specification.md](HAVEN-FSK_Specification.md) as required
  by FCC Part 97 §97.309 for unspecified digital codes.

---

## Technical Specification

See **[HAVEN-FSK_Specification.md](HAVEN-FSK_Specification.md)** for the 
complete technical specification including:

- Tone frequencies and modulation parameters
- Frame format and header definition
- LDPC parity check matrix generation
- CRC-16 parameters
- Preamble sequence
- Performance characteristics

The mode specification is placed in the public domain. Anyone may 
implement a compatible encoder or decoder.

---

## Radio Interface Support

| Interface     | Status      | Radios                              |
|---------------|-------------|-------------------------------------|
| TCI WebSocket | Implemented | Thetis, ExpertSDR (HPSDR)          |
| Hamlib/rigctld| Planned     | IC-705, IC-7300, TS-590SG, FT-891, G90, most HF radios |
| VOX           | Supported   | Any radio                           |
| Manual PTT    | Supported   | Any radio                           |

---

## Status

**Version 0.1.1-alpha — Pre-release development**

This mode is under active development. It has not yet been tested 
on the air. The specification and software are published to satisfy 
FCC Part 97 disclosure requirements prior to on-air testing.

On-air performance data will be added to the specification as testing 
progresses.

### Changelog

**0.1.1-alpha**
- Inline station info bar: Call, Park, Summit, FD class/section editable directly in toolbar
- Activity-aware logging: POTA activation/hunting, SOTA activation/chasing, Field Day, General Chat auto-detected from station bar fields
- ADIF export: per-park POTA files, SOTA file with MY\_SOTA\_REF, Field Day ADIF + Cabrillo
- Multi-park activation support with per-park ADIF files
- P2P and S2S contact logging with correct ADIF fields
- Macro system with 17 variable tags, live preview, per-category tag insertion
- Recent QSO panel with activity-adaptive display and dupe detection (Field Day)
- UDP broadcast to external loggers (N1MM, Log4OM, Ham Radio Deluxe)
- Mousewheel VFO tuning on frequency display (per-digit, 1 Hz resolution)

**0.1.0-alpha**
- Initial release for FCC Part 97 disclosure

---

## License

Copyright (C) 2026 WD9N

This software is licensed under the **GNU General Public License v3**.
See the [LICENSE](LICENSE) file for full terms.

The HAVEN-FSK **mode specification** is released to the public domain.
Anyone may implement a compatible encoder or decoder without restriction.

Commercial use of the software requires written permission from the author.

---

## Author

**WD9N**  
Developed with assistance from Claude (Anthropic)  

*"HAVEN — a reliable place in noisy conditions"*
