# HF Digital Mode Band Plan & HAVEN-FSK Suggested Frequencies

**Current as of June 2026**  
**For North America (FCC Part 97) — IARU Region 2**

---

## Overview of Current HF Digital Mode Activity

| Category | Modes | Character |
|----------|-------|-----------|
| Weak signal/automated | FT8, FT4, WSPR, JT65 | No conversation, DX/awards |
| Conversational | PSK31, Olivia, JS8Call, HAVEN-FSK | Keyboard to keyboard |
| Messaging | Winlink/VARA, ARDOP | Email over radio |
| Legacy contest | RTTY | Teletype, active in contests |

---

## Established Digital Frequencies — All Bands
*(Frequencies to AVOID or be aware of)*

### 160 Meters (1.8 – 2.0 MHz)
```
1.800 – 1.830   CW only
1.838           FT8 (primary)
1.840           JS8Call
1.843           Olivia 8/250 calling
1.845           PSK31
```

---

### 80 Meters (3.5 – 4.0 MHz)
```
3.505           DXpedition CW (avoid)
3.560           QRP CW calling (avoid)
3.570 – 3.600   Digital sub-band
3.573           FT8 (primary)
3.575           FT4
3.578           JS8Call
3.580           PSK31 (primary)
3.583           Olivia 8/250 calling  ← MFSK neighborhood
3.585           HAVEN-FSK (suggested) ← see reasoning below
3.590           RTTY DX CALLING — DO NOT USE
3.592           WSPR
3.597           Winlink / VARA HF
```
**⚠️ 3.590 is the established RTTY DX calling frequency — maintain clearance**

**HAVEN-FSK 80m: 3.585 MHz**

80m is the primary target band for HAVEN-FSK. High noise, difficult
propagation, and QRM are exactly the conditions this mode was designed
for. The entire 30 kHz sub-band is occupied — there is no clean gap.
3.585 MHz is the most defensible available position.

```
Why 3.585:
  3.583  Olivia 8/250 calling  — compatible MFSK neighbor, low activity.
                                  Olivia operators call then QSY, so the
                                  calling frequency is often unoccupied.
                                  Most likely community to welcome HAVEN-FSK.
  3.590  RTTY DX calling       — 5 kHz away. Signal edges 4+ kHz clear.
  3.592  WSPR                  — automated low-power beacons, minimal conflict.

Operating discipline required:
  — Monitor 3.583-3.587 before every transmission
  — If Olivia QSO in progress on 3.583, wait or move slightly higher
  — Never transmit within 3 kHz of 3.590 under any circumstances
  — Use standard operating courtesy — this is a shared neighborhood
```

---

### 40 Meters (7.0 – 7.3 MHz)
```
7.025 – 7.125   RTTY/Data sub-band
7.037           Pactor calling
7.040           RTTY DX CALLING — DO NOT USE
7.047           PSK31 (primary)
7.056           Olivia 8/250 calling
7.060           JS8Call (primary — active)
7.070           PSK31 / general digital
7.074           FT8 (primary — extremely active)
7.078           FT4
7.080           RTTY general
7.094           WSPR
7.101           Winlink / VARA HF
```
**⚠️ 7.040 is the established RTTY DX calling frequency on 40m**

**HAVEN-FSK 40m suggestion: 7.065 MHz**
Gap between JS8Call (7.060) and PSK31 upper edge (7.070).
Monitor before use — some DXpedition SSB activity in this area.

---

### 30 Meters (10.1 – 10.15 MHz)
```
10.130          RTTY
10.136          FT8 (primary)
10.138          FT4
10.140          PSK31 / general digital
10.144          Olivia
10.146          JS8Call
10.148          WSPR
```
*Digital only band — no phone. Narrow — only 50 kHz total.*

**HAVEN-FSK 30m suggestion: 10.142 MHz**
Between PSK31 (10.140) and Olivia (10.144). Tight fit — monitor carefully.

---

### 20 Meters (14.0 – 14.35 MHz)
```
14.060 – 14.112  Digital/CW sub-band
14.063           PSK31 (primary — most active)
14.070 – 14.073  PSK31 / RTTY cluster
14.074           FT8 (PRIMARY — busiest digital freq worldwide)
14.076           FT4
14.078           JS8Call (primary)
14.080           RTTY / general digital
14.0956          WSPR (primary)
14.105           Winlink / VARA HF
```

**HAVEN-FSK 20m suggestion: 14.090 MHz**
Clear gap between RTTY (14.080) and WSPR (14.0956).
No established mode has claimed this spot. **Best primary frequency.**

---

### 17 Meters (18.068 – 18.168 MHz)
```
18.100 – 18.110  Digital sub-band
18.100           RTTY / PSK31
18.104           FT8 (primary)
18.106           FT4
18.108           JS8Call
```
*Very narrow digital sub-band. Tightly packed.*

**HAVEN-FSK 17m suggestion: 18.110 MHz**
Top edge of digital sub-band, above JS8Call. Limited room on this band.

---

### 15 Meters (21.0 – 21.45 MHz)
```
21.060 – 21.150  Digital sub-band
21.063           PSK31
21.071           Olivia
21.074           FT8 (primary)
21.076           FT4
21.080           RTTY
21.094           WSPR
```

**HAVEN-FSK 15m suggestion: 21.090 MHz**
Between RTTY (21.080) and WSPR (21.094). Clear gap.

---

### 12 Meters (24.89 – 24.99 MHz)
```
24.915           FT8
24.920           PSK31 / general digital
24.924           WSPR
```

**HAVEN-FSK 12m suggestion: 24.927 MHz**
Above WSPR, below end of sub-band. Solar maximum only.

---

### 10 Meters (28.0 – 29.7 MHz)
```
28.060 – 28.300  Wide digital sub-band
28.074           FT8 (primary)
28.076           FT4
28.080           PSK31 / RTTY
28.120           Olivia / MFSK calling
28.124           JS8Call
28.126           WSPR
```

**HAVEN-FSK 10m suggestion: 28.130 MHz**
Above the WSPR/JS8Call cluster. Wide sub-band gives room to breathe.

---

### 6 Meters (50.0 – 54.0 MHz)
```
50.090           CW calling (avoid)
50.290 – 50.320  Digital sub-band
50.290           Olivia / MFSK calling
50.310           FT8 (primary — active during Es openings)
50.313           FT4
50.318           JS8Call
```

**HAVEN-FSK 6m suggestion: 50.323 MHz**
Above JS8Call, below SSB territory at 50.330. Active during Es openings.

---

## HAVEN-FSK Suggested Frequencies — Clean Summary

| Band  | Dial Freq    | Notes                                    | Priority    |
|-------|-------------|------------------------------------------|-------------|
| 80m   | **3.585 MHz** | Near Olivia calling — MFSK neighborhood, 5 kHz from RTTY DX | Medium      |
| 40m   | 7.065 MHz   | Between JS8Call and PSK31 upper          | Medium      |
| 30m   | 10.142 MHz  | Digital only band, narrow                | Medium      |
| 20m   | **14.090 MHz** | Clear gap, best band — **USE THIS**   | **Primary** |
| 17m   | 18.110 MHz  | Top of narrow sub-band                   | Low         |
| 15m   | 21.090 MHz  | Clear gap, good daytime band             | Medium      |
| 12m   | 24.927 MHz  | Solar maximum only                       | Low         |
| 10m   | 28.130 MHz  | Wide sub-band, solar dependent           | Medium      |
| 6m    | 50.323 MHz  | Es openings only                         | Low         |

**20m at 14.090 MHz is the recommended primary calling frequency.**

---

## Frequencies to Absolutely Avoid

| Frequency  | Band | Reason                           |
|------------|------|----------------------------------|
| 3.590 MHz  | 80m  | RTTY DX calling — well established |
| 7.040 MHz  | 40m  | RTTY DX calling — well established |
| 14.074 MHz | 20m  | FT8 primary — busiest digital freq |
| 14.070 MHz | 20m  | PSK31/RTTY cluster               |
| 21.074 MHz | 15m  | FT8 primary                      |
| 28.074 MHz | 10m  | FT8 primary                      |
| 50.310 MHz | 6m   | FT8 primary during openings      |

---

## Activity Level Assessment (2026)

```
FT8:          Dominant. Active 24/7 on most bands.
JS8Call:      Growing. Replacing PSK31 as keyboard mode.
PSK31:        Declining but alive on 20m weekends.
RTTY:         Contest-driven activity. DX calling freqs defended.
Olivia:       Small dedicated community. Low activity.
WSPR:         Automated beacons only. Constant low-level activity.
Winlink/VARA: EmComm and messaging. Specific node frequencies.
HAVEN-FSK:    New — pre-release. On-air testing pending.
```

The 20m gap at 14.090 MHz is genuinely clear. No established mode 
has claimed it and it sits naturally in the MFSK/Olivia portion of 
the band away from the FT8 and PSK clusters.

---

## Operating Conventions (Proposed)

1. **Listen first** — always monitor the frequency before transmitting
2. **Call CQ on suggested frequency** — then QSY for extended QSOs
3. **Standard CQ:**
   ```
   CQ CQ DE WD9N HAVEN-FSK K
   CQ POTA DE WD9N K-1234 K
   ```
4. **Be a good neighbor** — HAVEN-FSK at 500 Hz is the same width as
   Olivia. It takes up reasonable spectrum. Don't park on it all day.

---

*Maintained by WD9N — https://github.com/WD9N/Haven-FSK*  
*Always verify a frequency is clear before transmitting.*  
*RTTY DX calling frequencies are strongly defended — avoid them.*
