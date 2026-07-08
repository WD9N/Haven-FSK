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

HAVEN-FSK is an HF digital mode for free-text conversational communication,
using 16-tone **continuous-phase MFSK (CPMFSK)** — the transmitter's phase
accumulator carries continuously across every symbol boundary rather than
resetting per symbol, so only tone *frequency* changes between symbols, not
phase. It combines 16-tone MFSK modulation with LDPC forward error
correction, CRC-16 integrity checking, and preamble-based frame
synchronization, delivering ~62 bps net throughput in 500 Hz of bandwidth.

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

Tones are numbered 0 through 15, spaced 31.25 Hz apart:

| Tone | Frequency (Hz) |
|------|---------------|
| 0    | 500.00        |
| 1    | 531.25        |
| 2    | 562.50        |
| 3    | 593.75        |
| 4    | 625.00        |
| 5    | 656.25        |
| 6    | 687.50        |
| 7    | 718.75        |
| 8    | 750.00        |
| 9    | 781.25        |
| 10   | 812.50        |
| 11   | 843.75        |
| 12   | 875.00        |
| 13   | 906.25        |
| 14   | 937.50        |
| 15   | 968.75        |

**The tone transmitted for a given 4-bit data value is not the value
itself — it is Gray-coded first.** Adjacent tones (the most likely
confusion at low SNR, since they occupy neighboring FFT bins) then differ
by exactly one bit after decoding, limiting a single-tone detection error
to a single bit error rather than potentially several.

Gray encode (TX): `g = n XOR (n >> 1)`
Gray decode (RX): inverse of the above (standard Gray-to-binary conversion)

| Data value (binary) | Data value | Transmitted tone (Gray-coded) |
|---------------------|------------|--------------------------------|
| 0000 | 0  | 0  |
| 0001 | 1  | 1  |
| 0010 | 2  | 3  |
| 0011 | 3  | 2  |
| 0100 | 4  | 6  |
| 0101 | 5  | 7  |
| 0110 | 6  | 5  |
| 0111 | 7  | 4  |
| 1000 | 8  | 12 |
| 1001 | 9  | 13 |
| 1010 | 10 | 15 |
| 1011 | 11 | 14 |
| 1100 | 12 | 10 |
| 1101 | 13 | 11 |
| 1110 | 14 | 9  |
| 1111 | 15 | 8  |

This Gray-coding step applies to every transmitted nibble — header, CRC,
and FEC-encoded payload bits alike (§4.2, §5) — and must be implemented
identically by any independent encoder/decoder to interoperate with this
specification.

### 3.3 Symbol Encoding

Each byte of data is encoded as two consecutive symbols. The high nibble
(bits 7-4) is Gray-coded and transmitted first, followed by the
Gray-coded low nibble (bits 3-0).

Example: ASCII 'A' = 0x41 = 0100 0001
- High nibble = 0100 (4). Gray-coded → tone 6, transmitted at 687.50 Hz for 32ms.
- Low nibble = 0001 (1). Gray-coded → tone 1 (unchanged), transmitted at 531.25 Hz for 32ms.

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

No amplitude shaping is applied at symbol boundaries; only the tone
frequency changes.

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
does not assume zero offset. It searches a frequency-offset range of
±~220 Hz around the nominal tone positions while scanning for the
preamble, refines the winning offset using a sub-bin spectral-centroid
measurement, and optionally tracks that offset for subsequent frames.

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

No explicit end-of-frame marker is transmitted. Because the header (§4.2)
discloses NBLOCKS — the exact number of FEC blocks in the payload — the
receiver computes the total expected symbol count immediately after
decoding the header and declares the frame complete once exactly that
many symbols have been collected. End of frame is therefore determined by
message length, not by carrier sensing.

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
This is evaluated across the frequency-offset range and symbol-timing
positions described in §3.5 to find the best-aligned candidate before
applying the threshold.

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
                     bits 1-3: reserved (must be 0)
```
A receiver that decodes a VERSION it does not recognize (e.g. a v1 station
hearing a v2 transmission that also added payload interleaving, or vice
versa) rejects the frame rather than attempting to decode it — the two
versions are not wire-compatible.

In protocol v2, byte 0 has exactly one valid value: 0x21 (VERSION=2,
FEC=1). The FEC flag is always transmitted as 1 — v2 defines no
FEC-disabled mode of operation — and the reserved bits are always 0. The
reference receiver enforces this strictly, discarding any frame whose
byte 0 differs (a nonzero reserved bit or FEC=0 is treated as header
corruption, which the strictness helps reject early). A future extension
that assigns meaning to a reserved bit therefore also requires a VERSION
bump; it cannot be introduced compatibly under v2.

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
- Byte order on the wire: most-significant byte first (big-endian) —
  the high byte of the 16-bit CRC is transmitted as the first of the
  two CRC bytes
- Test vector: CRC over the ASCII bytes "123456789" = 0x29B1

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

## 7. Version History

| Version     | Date     | Changes                                         |
|-------------|----------|-------------------------------------------------|
| 0.2.0-beta  | Jul 2026 (rev. 3) | Closed two disclosure gaps, no wire-format change: documented CRC-16 wire byte order (§4.3, big-endian) and added the standard test vector; documented that v2 header byte 0 has exactly one valid value (0x21 — FEC always 1, reserved bits always 0, receiver rejects otherwise), so reserved-bit extensions require a VERSION bump (§4.2). |
| 0.2.0-beta  | Jul 2026 (rev. 2) | Corrected two disclosure errors, no wire-format change: documented Gray-coding of transmitted tone indices (§3.2/§3.3), previously omitted; corrected end-of-frame detection (§4) to header-disclosed message length rather than carrier sensing. |
| 0.2.0-beta  | Jul 2026 | Protocol v2: header sent 3x with bit-level majority vote (was 2x, "prefer copy 1"); payload interleaving across LDPC blocks added — both are wire-format-breaking changes, gated by the header VERSION field. Modulator confirmed/documented as continuous-phase (CPMFSK). Effective NBLOCKS maximum corrected to 125 (receiver-enforced sanity cap), not the 255 the field width alone would allow. |
| 0.1.0-alpha | Jun 2026 | Initial specification, pre-release alpha        |

---

## 8. License

**Mode Specification:** This specification document is placed in the 
public domain. Anyone may implement a compatible HAVEN-FSK encoder or 
decoder without restriction, including for commercial purposes.

---

## 9. Contact

**Author:** WD9N  
**Mode:** HAVEN-FSK  
**Emission designator:** 500HJ2D  
**Repository:** https://github.com/WD9N/Haven-FSK  
