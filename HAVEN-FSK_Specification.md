# HAVEN-FSK Mode Specification

**Version:** 0.1.0-alpha  
**Date:** June 2026  
**Author:** WD9N  
**Status:** Development — Pre-release  
**Repository:** https://github.com/WD9N/Haven-FSK  

---

## 1. Overview

HAVEN-FSK is an HF digital mode designed for free-text conversational
communication. It is the fastest conversational HF digital mode in common
use, delivering ~62 bps net throughput in 500 Hz bandwidth — 50% faster
than Olivia 16/500 and twice as fast as PSK31 — while retaining LDPC
forward error correction.

The mode fills a gap in the current amateur digital landscape:

| Mode          | Net bps  | ~WPM     | Weak Signal | Free Text | FEC |
|---------------|----------|----------|-------------|-----------|-----|
| FT8           | ~10 bps  | ~15 WPM  | Excellent   | No        | Yes |
| PSK31         | ~31 bps  | ~47 WPM  | Poor        | Yes       | No  |
| Olivia 16/500 | ~42 bps  | ~63 WPM  | Good        | Yes       | Yes |
| **HAVEN-FSK** | **~62 bps** | **~94 WPM** | **Good** | **Yes** | **Yes** |

HAVEN-FSK uses 16-tone MFSK modulation with LDPC forward error correction,
CRC-16 integrity checking, preamble-based frame synchronization, and
collision avoidance via DCD monitoring and randomized transmit delay.

---

## 2. Regulatory Information

**Emission Designator:** 500HJ2D  
**Bandwidth:** 500 Hz  
**Modulation type:** J — Single sideband suppressed carrier  
**Signal type:** 2D — Two or more channels, digital data  
**FCC Part 97 compliance:** Yes — bandwidth within 2.8 kHz HF limit  
**Disclosure:** This document constitutes public disclosure of technical 
characteristics as required under FCC Part 97 §97.309 for unspecified 
digital codes. This specification is publicly available at:
https://github.com/WD9N/Haven-FSK

**Recommended operating frequencies (USB dial):**

| Band | Suggested Frequency | Notes                                        |
|------|---------------------|----------------------------------------------|
| 80m  | 3.585 MHz           | Near Olivia calling — MFSK neighborhood      |
| 40m  | 7.065 MHz           | Gap between JS8Call and PSK31 upper cluster  |
| 30m  | 10.142 MHz          | Digital-only band, clear gap                 |
| 20m  | 14.090 MHz          | Primary recommended frequency — clear gap    |
| 17m  | 18.108 MHz          | Above JS8Call, avoids FT8 at 18.104          |
| 15m  | 21.090 MHz          | Clear gap between RTTY and WSPR              |
| 10m  | 28.130 MHz          | Above WSPR/JS8Call cluster, wide sub-band    |
| 6m   | 50.323 MHz          | Above JS8Call, Es openings only              |

*These are suggested starting points. Operators should always listen before
transmitting and follow normal band etiquette. 3.590 MHz (80m) and
7.040 MHz (40m) are established RTTY DX calling frequencies — avoid them.
HAVEN-FSK occupies 500 Hz of bandwidth, with tones from 500–968 Hz above
the dial frequency.*

---

## 3. Modulation

### 3.1 Basic Parameters

| Parameter             | Value              |
|-----------------------|--------------------|
| Modulation            | 16-tone MFSK       |
| Number of tones       | 16                 |
| Tone spacing          | 31.25 Hz           |
| Symbol duration       | 32 ms              |
| Symbol rate           | 31.25 baud         |
| Bits per symbol       | 4 (log₂16)         |
| Raw data rate         | 125 bps            |
| Net data rate         | ~62.5 bps          |
| Occupied bandwidth    | 500 Hz             |
| Audio frequency range | 500.00 — 968.75 Hz |
| Recommended mode      | USB (DIGU)         |
| Reference sample rate | 48000 Hz           |

### 3.2 Tone Map

Tones are numbered 0 through 15. Each tone carries 4 bits of data.
The tone number equals the 4-bit value it represents.

| Tone | Frequency (Hz) | 4-bit value |
|------|---------------|-------------|
| 0    | 500.00        | 0000        |
| 1    | 531.25        | 0001        |
| 2    | 562.50        | 0010        |
| 3    | 593.75        | 0011        |
| 4    | 625.00        | 0100        |
| 5    | 656.25        | 0101        |
| 6    | 687.50        | 0110        |
| 7    | 718.75        | 0111        |
| 8    | 750.00        | 1000        |
| 9    | 781.25        | 1001        |
| 10   | 812.50        | 1010        |
| 11   | 843.75        | 1011        |
| 12   | 875.00        | 1100        |
| 13   | 906.25        | 1101        |
| 14   | 937.50        | 1110        |
| 15   | 968.75        | 1111        |

### 3.3 Symbol Encoding

Each byte of data is encoded as two consecutive symbols. The high nibble
(bits 7-4) is transmitted first, followed by the low nibble (bits 3-0).

Example: ASCII 'A' = 0x41 = 0100 0001
- Symbol 1: tone 4 (0100) at 625.00 Hz for 32ms
- Symbol 2: tone 1 (0001) at 531.25 Hz for 32ms

### 3.4 Tone Generation

Each symbol is a pure sine wave at the tone frequency lasting exactly
32ms (1536 samples at 48000 Hz, or 256 samples at 8000 Hz). The tones
are orthogonal — each tone frequency is an integer multiple of the 
symbol rate (31.25 Hz), ensuring zero inter-tone interference when the 
receiver is symbol-synchronized.

Output amplitude is normalized to 0.25 peak (-12 dBFS) to provide 
headroom and prevent ALC activation on typical radio audio inputs.

### 3.5 Detection

The receiver uses non-coherent detection via FFT. For each symbol period
an 8× zero-padded FFT is computed. The tone with the highest energy at
the 16 designated frequency bins is selected as the decoded symbol.

A guard window of ±3 sub-bins around each tone center frequency provides
sub-Hz frequency resolution, tolerating minor frequency drift.

---

## 4. Frame Format

A complete HAVEN-FSK transmission consists of:

```
┌───────────────────────────────────────────────────────────────┐
│ PREAMBLE  │ HEADER  │ CRC-16  │ FEC-ENCODED PAYLOAD          │
│ 16 sym    │ 2 bytes │ 2 bytes │ n_blocks × 192 bits          │
│ 512ms     │ 64ms    │ 64ms    │ variable                     │
└───────────────────────────────────────────────────────────────┘
```

End of frame is indicated by carrier drop. No end marker is transmitted.
The receiver declares end of frame when the carrier is absent for
4 consecutive audio chunks (~100ms holdoff).

### 4.1 Preamble

A fixed sequence of 16 symbols transmitted before every frame for
signal identification and symbol timing recovery.

**Preamble symbol sequence:**
```
[0, 15, 0, 15, 7, 8, 7, 8, 0, 15, 0, 15, 7, 8, 7, 8]
```

Duration: 16 × 32ms = 512ms

Detection uses normalized correlation scoring with a threshold of 1.4
(scale 0–16). This threshold rejects voice, noise, and other digital modes.

### 4.2 Header

Two bytes transmitted immediately after the preamble without FEC.

**Byte 0:**
```
Bits 7-4: VERSION  — protocol version (currently 0001)
Bits 3-0: FLAGS    — bit 0: FEC enabled (1=yes)
                     bits 1-3: reserved
```

**Byte 1:**
```
Bits 7-0: NBLOCKS  — number of FEC blocks in payload (0-255)
                     Maximum message: 255 × 12 = 3060 bytes
```

### 4.3 CRC-16

A 16-bit CRC transmitted as 2 bytes after the header, covering the
header bytes plus the original unencoded payload.

**Parameters:**
- Algorithm: CRC-16/CCITT-FALSE
- Polynomial: 0x1021
- Initial value: 0xFFFF
- Input/output reflection: None

### 4.4 FEC-Encoded Payload

Transmitted as NBLOCKS consecutive FEC blocks, each 192 bits (48 symbols).

---

## 5. Forward Error Correction

### 5.1 LDPC Code Parameters

| Parameter          | Value                    |
|--------------------|--------------------------|
| Code               | LDPC(192, 96)            |
| Code rate          | 1/2                      |
| Payload bits       | 96 bits = 12 bytes/block |
| Coded bits         | 192 bits/block           |
| Parity bits        | 96 bits/block            |
| Variable node deg  | 3                        |
| Check node degree  | 6                        |
| Construction       | Progressive Edge Growth  |
| PEG seed           | 1234                     |
| Decoder            | Belief propagation       |
| BP algorithm       | Min-sum                  |
| Max iterations     | 200                      |
| Scaling factor     | 0.75                     |

### 5.2 Parity Check Matrix

The parity check matrix H is a (96 × 192) binary matrix generated by
the Progressive Edge Growth algorithm with seed 1234, variable node
degree 3, and check node degree 6.

The matrix is fully deterministic — the same parameters always produce
the identical H matrix, which is what enables interoperability between
independent implementations.

**Python reference implementation:**

```python
import numpy as np

def build_parity_check_matrix(n=192, k=96, d_v=3, d_c=6, seed=1234):
    np.random.seed(seed)
    m    = n - k
    H    = np.zeros((m, n), dtype=np.uint8)
    cdeg = np.zeros(m, dtype=int)

    def get_reachable(H, j, depth):
        visited_c, visited_v = set(), {j}
        frontier = set(np.where(H[:, j] == 1)[0])
        visited_c.update(frontier)
        for _ in range(depth):
            nv = set()
            for c in frontier:
                nv.update(v for v in np.where(H[c,:]==1)[0]
                          if v not in visited_v)
            visited_v.update(nv)
            nc = set()
            for v in nv:
                nc.update(c for c in np.where(H[:,v]==1)[0]
                          if c not in visited_c)
            if not nc:
                break
            frontier = nc
            visited_c.update(nc)
        return visited_c

    for j in range(n):
        for edge in range(d_v):
            avail = np.where(cdeg < d_c)[0]
            if edge == 0:
                mn     = cdeg[avail].min()
                chosen = np.random.choice(avail[cdeg[avail] == mn])
            else:
                reach  = get_reachable(H, j, edge * 2 + 1)
                pool   = [c for c in avail if c not in reach] or list(avail)
                mn     = min(cdeg[c] for c in pool)
                chosen = np.random.choice(
                    [c for c in pool if cdeg[c] == mn])
            H[chosen, j] = 1
            cdeg[chosen] += 1
    return H
```

### 5.3 Encoding

Systematic encoding — codeword = [message_bits | parity_bits].
Parity bits computed via Gaussian elimination over GF(2).

### 5.4 Decoding

Belief propagation with min-sum approximation. LLR values derived
from received signal soft outputs are input to the decoder.

### 5.5 Message Blocking

Messages longer than 12 bytes are split into multiple 12-byte blocks,
each independently encoded. Short messages are padded with null bytes
and padding is stripped after decoding.

---

## 6. Text Encoding

Message payload is UTF-8 encoded text. A single trailing space is
appended before encoding to ensure the final symbol fully transmits
before carrier drop. The trailing space is stripped on receive.

Primary character set: printable ASCII (0x20 through 0x7E).

---

## 7. Carrier Sense and Collision Avoidance

### 7.1 DCD — Data Carrier Detect

Monitors the 450–1050 Hz audio band for signal energy. DCD threshold
is 12 dB above measured noise floor. Holdoff: 4 consecutive audio
chunks (~100ms) before declaring channel clear.

### 7.2 Transmit Backoff

| Message type              | Backoff range |
|---------------------------|---------------|
| CQ (message starts "CQ") | 50 — 100ms    |
| All other messages        | 200 — 1500ms  |

If the channel becomes active during backoff, the timer resets.

---

## 8. ARQ — Automatic Repeat Request

Optional manual ARQ. Failed CRC frames may be requested for
retransmission using:

```
{THEIRCALL} DE {MYCALL} RESEND
```

Software does not automatically retransmit. ARQ retransmits the
originating station's own data only, consistent with FCC §97.113.

---

## 9. PTT and Radio Interface

| Method        | Description                               | Status      |
|---------------|-------------------------------------------|-------------|
| TCI WebSocket | Thetis/ExpertSDR via ws://localhost:40001 | Implemented |
| Hamlib/rigctld| Universal radio control via TCP:4532      | Planned     |
| VOX           | Voice-activated TX                        | Supported   |
| Manual PTT    | Operator keys radio manually              | Supported   |

PTT watchdog timer: 120 seconds maximum TX time, automatic release.

---

## 10. Performance Characteristics

### 10.1 Estimated SNR Threshold

| Condition               | Threshold              |
|-------------------------|------------------------|
| AWGN, modem only        | ~0 dB                  |
| AWGN, with LDPC FEC     | ~+3 dB                 |
| Real HF (estimated)     | ~-3 to -5 dB           |
| PSK31 (reference)       | ~-2 dB                 |

*Real HF performance will be published after on-air testing.*

### 10.2 Throughput Examples

| Message                             | Length | TX Time |
|-------------------------------------|--------|---------|
| CQ POTA DE WD9N K-1234 K           | 24 ch  | 5.4s    |
| KC8TYK DE WD9N 599 IN US-1017 K   | 34 ch  | 5.4s    |
| Typical ragchew sentence            | 80 ch  | 9.2s    |

*TX time includes 512ms preamble + 128ms header/CRC + FEC payload.*

---

## 11. Implementation

### 11.1 Reference Implementation

The reference implementation is written in Python 3 and licensed
under the GNU General Public License v3.

**Repository:** https://github.com/WD9N/Haven-FSK

**Files:**
- `haven_fsk.py`  — main application (GUI, audio engine, TX/RX)
- `modem.py`      — MFSK modulator and demodulator
- `fec.py`        — LDPC(192,96) encoder and decoder
- `frame.py`      — frame assembly and CRC-16
- `tci.py`        — TCI WebSocket radio control client

**Dependencies:**
- Python 3.8+
- numpy, scipy, sounddevice, matplotlib
- ldpc (Python LDPC library)
- websocket-client
- tkinter (included with Python)

### 11.2 Compatibility

Implementable on any platform with:
- A sound card or virtual audio cable
- An SSB transceiver capable of USB/DIGU mode
- Python 3.8+ or equivalent DSP environment

No proprietary hardware or software is required.

---

## 12. Version History

| Version | Date     | Changes                            |
|---------|----------|------------------------------------|
| 0.1.0-alpha | Jun 2026 | Initial specification, pre-release alpha |

---

## 13. License

**Software:** GNU General Public License v3. See LICENSE file.

**Mode Specification:** This specification document is placed in the 
public domain. Anyone may implement a compatible HAVEN-FSK encoder or 
decoder without restriction, including for commercial purposes, provided 
they do not use the reference implementation codebase directly.

---

## 14. Contact

**Author:** WD9N  
**Mode:** HAVEN-FSK  
**Emission designator:** 500HJ2D  
**Repository:** https://github.com/WD9N/Haven-FSK  

*This specification will be updated as the mode is refined through
on-air testing.*
