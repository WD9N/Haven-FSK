# HAVEN-FSK Mode Specification

**Version:** 0.2.0-beta  
**Date:** July 2026  
**Author:** WD9N  
**Status:** Development — Pre-release  
**Repository:** https://github.com/WD9N/Haven-FSK  

**Scope note:** This document specifies HAVEN-FSK's original mode — 16-tone
continuous-phase MFSK (CPMFSK), emission designator 500HJ2D. As of v0.2.0
the reference application additionally supports PSK31 as a second,
independently-selectable operating mode. PSK31 is a well-established public
digital mode (G3PLX) requiring no novel §97.309 disclosure of its own; this
specification does not cover it. Nothing in this document applies to the
application's PSK31 mode.

---

## 1. Overview

HAVEN-FSK is an HF digital mode designed for free-text conversational
communication, using 16-tone **continuous-phase MFSK (CPMFSK)** — the
transmitter's phase accumulator carries continuously across every symbol
boundary rather than resetting per symbol, so only tone *frequency* changes
between symbols, not phase. It is the fastest conversational HF digital mode
in common use, delivering ~62 bps net throughput in 500 Hz bandwidth — 50%
faster than Olivia 16/500 and twice as fast as PSK31 — while retaining LDPC
forward error correction.

The mode fills a gap in the current amateur digital landscape:

| Mode          | Net bps  | ~WPM     | Weak Signal | Free Text | FEC |
|---------------|----------|----------|-------------|-----------|-----|
| FT8           | ~10 bps  | ~15 WPM  | Excellent   | No        | Yes |
| PSK31         | ~31 bps  | ~47 WPM  | Poor        | Yes       | No  |
| Olivia 16/500 | ~42 bps  | ~63 WPM  | Good        | Yes       | Yes |
| **HAVEN-FSK** | **~62 bps** | **~94 WPM** | **Good** | **Yes** | **Yes** |

HAVEN-FSK uses 16-tone continuous-phase MFSK modulation with LDPC forward
error correction, CRC-16 integrity checking, and preamble-based frame
synchronization. DCD (carrier detect) is displayed to the operator as an
advisory indicator; automatic collision avoidance via randomized transmit
delay is planned but not yet implemented (see §7.2).

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

### 3.4 Tone Generation — Continuous Phase (CPMFSK)

Each symbol lasts exactly 32ms (1536 samples at 48000 Hz). The transmitter
maintains a single continuous phase accumulator across the entire
transmission — phase is **never reset at a symbol boundary**. Each sample
is generated as `sin(phase)`, where `phase` advances every sample by
`2π × f / Fs` for the current symbol's tone frequency `f`, and carries that
accumulated value forward unchanged into the next symbol regardless of
which tone is selected next. Only the tone (frequency) changes at a symbol
boundary; phase itself is continuous. This is what distinguishes CPMFSK
from simple/discontinuous-phase MFSK, where each symbol would restart at
phase zero.

The tones are orthogonal — each tone frequency is an integer multiple of
the symbol rate (31.25 Hz) above the base frequency, ensuring zero
inter-tone interference when the receiver is symbol-synchronized.

**Note:** frequency (not phase) still changes abruptly at each symbol
boundary. Raised-cosine amplitude shaping across that transition was
tried and deliberately removed: with phase-continuous generation, the
shaping produced audible amplitude dips at the 31.25 Hz symbol rate
rather than smoothing anything, and was worse than no shaping at all
(verified by waveform inspection). The current implementation applies no
amplitude shaping at symbol boundaries; a raised-cosine constant remains
defined in source but is intentionally unused.

Per-symbol output is normalized to full scale (1.0 peak / 0 dBFS) before
handoff to the audio output stage; actual transmit drive level is
controlled downstream by the operator-adjustable TX gain control, not by a
fixed modulator-side amplitude target.

### 3.5 Detection

The receiver uses non-coherent detection via FFT. For each symbol period
an 8× zero-padded FFT is computed (12288-point FFT for a 1536-sample
symbol, ≈3.9 Hz/bin resolution). Soft symbol energies (not just the
single winning tone) are computed for every one of the 16 tone bins and
passed downstream as soft-decision input to the LDPC decoder (see Section
5) — final hard tone selection for header/CRC fields uses the
highest-energy bin among the 16.

A guard window of ±3 zero-padded FFT bins (≈±11.7 Hz) around each tone's
center frequency provides drift tolerance beyond the raw 31.25 Hz/bin
resolution a non-zero-padded FFT would give.

**Frequency acquisition:** because independent radios' oscillators can
differ by tens to over a hundred Hz at HF (e.g. ±70 Hz at 14 MHz for 5 ppm
tolerance) even when both are dialed to the same frequency, the receiver
does not assume zero offset. It searches multiple frequency-offset
hypotheses (spaced 20 Hz apart) while scanning for the preamble, refines
the winning hypothesis using a sub-bin spectral-centroid measurement, and
optionally (operator-enabled AFC) tracks that offset as the center of the
search window for subsequent frames.

---

## 4. Frame Format

A complete HAVEN-FSK transmission (protocol version 2) consists of:

```
┌────────────────────────────────────────────────────────────────────────┐
│ PREAMBLE  │ HEADER (x3)     │ CRC-16  │ FEC-ENCODED PAYLOAD (interleaved) │
│ 16 sym    │ 2 bytes x 3     │ 2 bytes │ n_blocks × 192 bits                │
│ 512ms     │ 384ms (12 sym)  │ 128ms   │ variable (n_blocks × 1536ms)       │
└────────────────────────────────────────────────────────────────────────┘
```

The header is transmitted three times in immediate succession (see §4.2);
the payload's coded bits pass through a block interleaver before
modulation (see §4.4/§5.6) — both are required for a receiving station to
correctly decode a real transmission, not merely tuning parameters.

End of frame is indicated by carrier drop. No end marker is transmitted.
The receiver declares end of frame when the carrier is absent for
4 consecutive audio chunks of 2048 samples at 48000 Hz (~171ms holdoff).

### 4.1 Preamble

A fixed sequence of 16 symbols transmitted before every frame for
signal identification and symbol timing recovery.

**Preamble symbol sequence:**
```
[0, 15, 0, 15, 7, 8, 7, 8, 0, 15, 0, 15, 7, 8, 7, 8]
```

Duration: 16 × 32ms = 512ms

Detection uses soft correlation: for a candidate window, the fraction of
total FFT energy falling on the expected tone bin is measured at each of
the 16 preamble positions and averaged. Score range is 0.0625 (uniform
noise, i.e. 1/16) to 1.0 (perfect match); the detection threshold is 0.45.
This is evaluated across multiple frequency-offset hypotheses and multiple
symbol-timing offsets per scan (see §3.5) to find the best-aligned
candidate before applying the threshold.

### 4.2 Header

Two bytes, transmitted **three times** in immediate succession after the
preamble, without FEC. The receiver independently majority-votes each bit
position across the three received copies (the value held by at least 2
of 3 copies wins), tolerating a fade that corrupts up to one entire copy
without corrupting the recovered header.

**Byte 0:**
```
Bits 7-4: VERSION  — protocol version (currently 0010 = 2)
Bits 3-0: FLAGS    — bit 0: FEC enabled (1=yes)
                     bits 1-3: reserved
```
A receiver that decodes a VERSION it does not recognize (e.g. a v1 station
hearing a v2 transmission that also added payload interleaving, or vice
versa) rejects the frame rather than attempting to decode it — the two
versions are not wire-compatible.

**Byte 1:**
```
Bits 7-0: NBLOCKS  — number of FEC blocks in payload
                     Field width allows 0-255, but the receiver rejects
                     any decoded NBLOCKS > 125 as a corrupted-header
                     sanity check (protects against attempting a
                     multi-thousand-second collection on a bit error).
                     Effective maximum message: 125 × 12 = 1500 bytes.
```

### 4.3 CRC-16

A 16-bit CRC transmitted as 2 bytes after the header (once — not repeated
with the header), covering the header bytes plus the original unencoded
payload.

**Parameters:**
- Algorithm: CRC-16/CCITT-FALSE
- Polynomial: 0x1021
- Initial value: 0xFFFF
- Input/output reflection: None

### 4.4 FEC-Encoded Payload

Transmitted as NBLOCKS consecutive FEC blocks, each 192 bits (48 symbols).
Before modulation, the concatenated coded bits from all NBLOCKS blocks pass
through a block interleaver spanning the whole payload (see §5.6) — this is
a wire-format requirement, not an optional receiver-side enhancement: a
receiver that does not deinterleave will not recover the correct bit
ordering for FEC decoding.

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

### 5.6 Payload Interleaving (protocol v2+)

HF fading tends to corrupt several consecutive transmitted bits at once
(a fade lasting a fraction of a second spans multiple symbols), which is
a burst error — a pattern LDPC belief propagation handles far worse than
the same number of errors spread randomly through a block. To mitigate
this, the coded bits from all NBLOCKS blocks of a message (after LDPC
encoding, before modulation) pass through a row/column block interleaver
spanning the full payload:

- **Write:** bits are written row-wise into a matrix with NBLOCKS rows
  and 192 columns — row *i* holds LDPC block *i*'s 192 coded bits in
  their natural (unpermuted) order.
- **Read:** bits are read out column-wise (column 0 top-to-bottom, then
  column 1, etc.) to produce the transmitted bit sequence.

The receiver applies the inverse permutation (read the received bits back
into the matrix column-wise, read the matrix out row-wise) before handing
each block's bits to the LDPC decoder. A contiguous burst in the received
audio, once deinterleaved, lands as isolated single-bit errors scattered
across many different blocks rather than a concentrated run within one
block.

When a message is exactly one LDPC block (NBLOCKS = 1), interleaving is a
no-op — there is only one block to spread a burst across.

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
chunks of 2048 samples at 48000 Hz (~171ms) before declaring channel clear.

DCD is advisory only in the current C++ implementation — it drives a
status indicator for the operator's own judgment and does not gate
transmission (see §7.2).

### 7.2 Transmit Backoff — Not Yet Implemented (C++)

Randomized, DCD-linked transmit backoff (shorter delay for CQ calls,
longer for other traffic, resetting if the channel becomes active during
the delay) was part of the original design intent but **is not present in
the current C++ implementation** — transmission begins immediately on
operator command (subject only to the PTT lead-time sequencing delay in
§9), with no automatic randomized delay or DCD gate. Operators are
responsible for listening before transmitting. This section documents
intended future behavior; it does not describe current software behavior.

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

| Method        | Description                                    | Status      |
|---------------|-------------------------------------------------|-------------|
| TCI WebSocket | Thetis/ExpertSDR/HPSDR, default ws://localhost:50001 | Implemented |
| Hamlib/rigctld| Universal radio control via TCP, default :4532 | Implemented |
| VOX           | Voice-activated TX (no rig-control connection) | Supported   |
| Manual PTT    | Operator keys radio manually                   | Supported   |

Hamlib/rigctld support includes automatic reconnect with exponential
backoff on a dropped connection, split-frequency operation, TX power
level get/set (normalized 0.0-1.0 fraction of the radio's configured max,
matching rigctld's own `RFPOWER` level protocol — there is no
absolute-watts concept at this layer), and periodic mode polling.
Direct Hamlib library linking (bypassing rigctld) remains unimplemented;
rigctld is the supported path for Hamlib-based rig control.

PTT watchdog timer: 120 seconds maximum TX time, automatic release
(FCC Part 97 compliance safeguard against a stuck/runaway transmission).

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

Recalculated for protocol v2 framing: fixed overhead per transmission is
512ms preamble + 384ms header (3 copies) + 128ms CRC = 1.024s, before any
payload. Payload duration is `nBlocks × 48 symbols × 32ms = nBlocks × 1.536s`,
where `nBlocks = ceil((chars + 1) / 12)` (the "+1" is the trailing space
added before encoding, per §6).

| Message                            | Length | Blocks | TX Time |
|-------------------------------------|--------|--------|---------|
| CQ POTA DE WD9N K-1234 K            | 24 ch  | 3      | 5.6s    |
| KC8TYK DE WD9N 599 IN US-1017 K      | 34 ch  | 3      | 5.6s    |
| Typical ragchew sentence (80 ch)    | 80 ch  | 7      | 11.8s   |

---

## 11. Implementation

### 11.1 Reference Implementation

The production reference implementation is written in C++17/Qt6 (the
`cpp-rewrite` branch) and licensed under the GNU General Public License
v3. The original Python 3 prototype validated the mode specification and
on-air viability but is preserved only as a historical reference on the
`main` branch; it is not maintained or developed further, and its DSP
behavior has diverged from this specification in places (e.g. it uses
plain non-zero-padded FFT detection, whereas this spec and the C++
implementation use 8x zero-padding — see §3.5).

**Repository:** https://github.com/WD9N/Haven-FSK

**C++ implementation files (`src/`):**
- `dsp/Modulator.{h,cpp}`, `dsp/Demodulator.{h,cpp}` — CPMFSK modulator/demodulator
- `dsp/Preamble.{h,cpp}` — preamble generation and soft correlation detection
- `dsp/DCD.{h,cpp}` — carrier detect
- `dsp/FEC.{h,cpp}` — LDPC(192,96) encoder/decoder
- `dsp/Interleaver.{h,cpp}` — payload block interleaver (protocol v2+)
- `dsp/Frame.{h,cpp}` — frame assembly, header, CRC-16
- `dsp/MfskModem.{h,cpp}`, `dsp/DspPipeline.{h,cpp}` — RX/TX state machine and AFC
- `radio/RigctldClient.{h,cpp}`, `radio/TCIClient.{h,cpp}` — rig control
- `ui/` — Qt6 application UI

**Dependencies:**
- C++17 compiler (MSVC/MinGW/GCC/Clang)
- Qt 6.11.1+ (Core, Widgets, Network, WebSockets, SerialPort, Multimedia,
  MultimediaWidgets, Charts, Sql)
- KissFFT (vendored, BSD-3-Clause, `src/third_party/kissfft/`)

### 11.2 Compatibility

Implementable on any platform with:
- A sound card or virtual audio cable
- An SSB transceiver capable of USB/data-USB mode
- A C++17 toolchain and Qt 6.11.1+, or an equivalent DSP environment
  implementing this specification independently

No proprietary hardware or software is required.

---

## 12. Version History

| Version     | Date     | Changes                                         |
|-------------|----------|-------------------------------------------------|
| 0.2.0-beta  | Jul 2026 | Protocol v2: header sent 3x with bit-level majority vote (was 2x, "prefer copy 1"); payload interleaving across LDPC blocks added — both are wire-format-breaking changes, gated by the header VERSION field. Modulator confirmed/documented as continuous-phase (CPMFSK) — phase accumulator never resets at a symbol boundary. Detection description corrected to match actual soft-correlation + multi-hypothesis frequency search implementation. Reference implementation moved from Python prototype to production C++17/Qt6 (`cpp-rewrite` branch). Hamlib/rigctld support completed (reconnect, split, power level, mode polling) — no longer "Planned". §7.2 transmit backoff documented as not-yet-implemented in the C++ version. Effective NBLOCKS maximum corrected to 125 (receiver-enforced sanity cap), not the 255 the field width alone would allow. |
| 0.1.1-alpha | Jun 2026 | Software: inline station bar, POTA/SOTA/FD logging, ADIF export, macro tags, UDP broadcast, performance optimisations |
| 0.1.0-alpha | Jun 2026 | Initial specification, pre-release alpha        |

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
