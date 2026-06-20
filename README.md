# HAVEN-FSK

**16-tone MFSK HF Digital Mode for Amateur Radio**  
Emission designator: **500HJ2D** | Bandwidth: **500 Hz** | Net rate: **~62 bps**

---

## What is HAVEN-FSK?

HAVEN-FSK is a new HF digital mode for free-text conversational communication.
It is designed to fill the gap between PSK31 (fast but fragile) and FT8 
(robust but rigid) — delivering real conversation at ~40 WPM with LDPC 
forward error correction.

| Mode          | Speed    | Weak Signal | Free Text | FEC |
|---------------|----------|-------------|-----------|-----|
| PSK31         | ~50 WPM  | Poor        | Yes       | No  |
| Olivia 16/500 | ~45 WPM  | Good        | Yes       | Yes |
| FT8           | ~10 WPM  | Excellent   | No        | Yes |
| **HAVEN-FSK** | **~40 WPM** | **Good** | **Yes** | **Yes** |

---

## Key Features

- **16-tone MFSK** — 500 Hz bandwidth, 500–968.75 Hz audio range
- **LDPC(192,96) FEC** — rate 1/2 forward error correction
- **CRC-16** — frame integrity verification
- **Free text** — type anything, no rigid exchange format
- **Conversational pace** — ~40 WPM, suitable for ragchew and nets
- **Preamble sync** — 512ms preamble for reliable frame detection
- **DCD + backoff** — collision avoidance on busy channels
- **TCI support** — direct integration with Thetis/HPSDR
- **Hamlib support** — works with virtually any radio (planned)
- **POTA/SOTA friendly** — designed for field portable operation

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
4. Use Launch HAVEN-FSK.bat to start
```

**First time setup:**
1. Enter your callsign in the Call field
2. Select your audio input (from radio) and output (to radio) devices
3. Set your radio to USB or DIGU mode
4. Tune to a HAVEN-FSK frequency (see suggested frequencies below)

---

## Suggested Operating Frequencies (USB dial)

| Band | Frequency  |
|------|-----------|
| 80m  | 3.590 MHz |
| 40m  | 7.090 MHz |
| 30m  | 10.142 MHz|
| 20m  | 14.090 MHz|
| 17m  | 18.104 MHz|
| 15m  | 21.090 MHz|
| 10m  | 28.120 MHz|

Signal appears 500–1000 Hz above the dial frequency.

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

**Version 0.1.0 — Pre-release development**

This mode is under active development. It has not yet been tested 
on the air. The specification and software are published to satisfy 
FCC Part 97 disclosure requirements prior to on-air testing.

On-air performance data will be added to the specification as testing 
progresses.

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
