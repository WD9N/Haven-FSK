# Architecture Decision Records — HAVEN-FSK C++ Rewrite

This document records significant technical decisions made during the C++
rewrite of HAVEN-FSK, along with the reasoning behind each. It exists so
that collaborators joining later, and the original authors six months from
now, can understand why things are the way they are without relitigating
settled questions.

Entries are in chronological order. Each entry has a status:
**Decided** — settled, not open for re-discussion without new information.
**Revisable** — reasonable to reconsider if circumstances change.

---

## ADR-001 — Rewrite in C++ with Qt6 rather than improve the Python

**Status:** Decided
**Date:** June 2026

**Decision:** The production implementation will be C++17 with Qt6. The Python
prototype will be preserved on `main` as a reference but will not be developed
further.

**Reasoning:** The Python prototype validated the mode specification and
demonstrated on-air viability. For production release, Python's deployment
complexity (runtime, venv, pip dependencies), audio callback jitter under
the GIL, and tkinter UI limitations are blocking issues. Qt6 in C++ solves
all three: single executable deployment, deterministic real-time audio, and
a native cross-platform UI. The DSP workload is well within C++ reach on
all target hardware including Raspberry Pi.

**Alternatives considered:**
- PyInstaller bundling of the Python version — rejected because it produces
  large bundles with poor startup time and doesn't solve the audio jitter issue
- Cython acceleration of hot paths — rejected because it complicates the build
  without addressing deployment or UI concerns

---

## ADR-002 — Qt6 minimum, no Qt5 support

**Status:** Decided
**Date:** June 2026

**Decision:** Qt 6.11.1 is the minimum. Qt5 compatibility will not be
maintained.

**Reasoning:** Qt6 Multimedia's QAudioSource/QAudioSink API is significantly
cleaner than Qt5's QAudioInput/QAudioOutput. Qt6 is available on all target
platforms including Raspberry Pi OS Bullseye. Supporting both Qt5 and Qt6
would require preprocessor guards throughout the audio layer with no benefit
since no target platform requires Qt5.

---

## ADR-003 — DSP layer uses std:: only, no Qt types

**Status:** Decided
**Date:** June 2026

**Decision:** All classes in `src/dsp/` use only C++ standard library types.
No Qt types (QString, QVector, QByteArray, etc.) appear in DSP class
interfaces or implementations. Qt types appear only at the AudioEngine
boundary in `src/audio/`.

**Reasoning:** The DSP layer is the most testable and most likely to be
reused or ported. Keeping it free of Qt types means it can be compiled and
tested without a Qt installation, run in unit tests without a Qt application
context, and potentially reused in a command-line tool or non-Qt port. The
conversion between Qt audio buffers and std::vector<float> is a one-line
operation at the AudioEngine boundary — a trivial cost for significant
architectural cleanliness.

---

## ADR-004 — KissFFT chosen as FFT library

**Status:** Decided (GPL-rejection reasoning below superseded by ADR-101 —
KissFFT remains the correct choice, but "commercial use" was never a real
project constraint)
**Date:** June 2026

**Decision:** KissFFT is used for all FFT operations in the DSP layer.

**Reasoning:** KissFFT is two .h and two .c files, MIT/BSD licensed, has no
build system requirements of its own, compiles identically with GCC, Clang,
and MSVC, and has optional ARM NEON support for future Raspberry Pi
optimization. It produces output equivalent to numpy's np.fft.rfft which
makes validation against the Python reference straightforward.

FFTW was considered and rejected: it requires a separate installation,
has licensing complications for binary distribution (the GPL version is
incompatible with potential commercial use; the commercial license costs
money), and its performance advantage is irrelevant at 31.25 symbols/second.

A hand-rolled DFT computing only 16 bins was considered and rejected: it
would need to implement zero-padding manually, has no established test record,
and offers no meaningful advantage over KissFFT for this use case.

---

## ADR-005 — KissFFT vendored into repo, not fetched at build time

**Status:** Decided
**Date:** June 2026

**Decision:** KissFFT source files are committed directly to
`src/third_party/kissfft/`. CMake FetchContent is not used.

**Reasoning:** HAVEN-FSK is intended to be buildable in the shack, potentially
offline, on a Raspberry Pi without reliable internet access. FetchContent
requires network access at CMake configure time. Vendoring two .h and two .c
files (approximately 1,200 lines total) has negligible repo size impact and
guarantees the build works anywhere.

**License compliance:** KissFFT is BSD-3-Clause. The copyright headers in
the KissFFT source files must not be modified or removed — this is a
legal requirement of the BSD-3-Clause license, not a style preference.
Full license text is in `THIRD_PARTY_LICENSES.md`. Do not delete or
truncate that file.

---

## ADR-006 — Demodulator implements spec, not Python behavior

**Status:** Decided
**Date:** June 2026

**Decision:** The C++ demodulator implements the HAVEN-FSK specification
(§3.5) faithfully. Where the Python prototype diverges from the spec, the
spec is authoritative.

**Specific divergence:** The Python demodulator uses a plain 1536-point FFT
with no zero-padding and no guard window. The specification calls for 8×
zero-padding (12288-point FFT) and a ±3 sub-bin guard window.

**Reasoning:** The Python prototype was written for simplicity and worked
well enough to validate the mode. The spec describes the correct behavior.
The C++ implementation is the production version — it should implement the
spec correctly from the start rather than inherit prototype shortcuts.

The 8× zero-padding gives ~3.9 Hz/bin resolution (vs 31.25 Hz/bin without
padding). The ±3 bin guard window covers ±11.7 Hz, providing tolerance for
HF frequency drift without requiring precise frequency lock between stations.

Cross-validation against Python output was considered as a reason to match
Python behavior. This was rejected: the Python is a prototype, not a
reference implementation. Loopback testing (C++ modulate → C++ demodulate)
is a more meaningful validation.

---

## ADR-007 — Phase continuity not maintained between symbols

**Status:** Decided
**Date:** June 2026

**Decision:** The modulator generates each symbol independently from a
pre-built tone table. Phase continuity between consecutive symbols is not
maintained.

**Reasoning:** HAVEN-FSK uses non-coherent detection — the receiver measures
energy at each tone frequency and does not track phase. Phase continuity
therefore provides no decoding benefit. Maintaining continuity would require
tracking the phase of the previous symbol and adjusting the start phase of
the next, complicating the modulator with no measurable improvement in
receiver performance. The raised cosine shaping on each symbol eliminates
key clicks regardless of phase relationship between symbols.

---

## ADR-008 — LDPC matrix generated algorithmically, not stored as a constant

**Status:** Decided
**Date:** June 2026

**Decision:** The LDPC(192,96) parity check matrix H is generated at runtime
by the Progressive Edge Growth algorithm with seed 1234, rather than stored
as a hardcoded constant array.

**Reasoning:** The matrix is 96×192 = 18,432 bits. Storing it as a C++ array
is feasible but makes the source harder to read and verify. The PEG algorithm
with a fixed seed is fully deterministic — it always produces the identical
matrix. Generating it at startup takes negligible time. The Python reference
implementation uses the same approach. Any independent implementation that
uses the same parameters (n=192, k=96, d_v=3, d_c=6, seed=1234) will produce
the identical matrix, which is what enables interoperability.

**Implementation note (Phase 3):** The matrix should be generated once at
FEC object construction and cached. It must not be regenerated per-frame.

---

## ADR-009 — TX amplitude normalized to 0.25 peak (-12 dBFS)

**Status:** Revisable
**Date:** June 2026

**Decision:** Modulator output is normalized to 0.25 peak amplitude
(-12 dBFS). Final output level is controlled by a TX gain slider in the UI.

**Reasoning:** Most radio USB audio interfaces and sound card inputs clip at
0 dBFS. ALC (Automatic Level Control) on HF transceivers begins to activate
at approximately -6 dBFS on many radios, causing audio compression and
intermodulation distortion. -12 dBFS leaves 6 dB of headroom before ALC
activation while providing a strong enough signal for the radio's audio input.
The operator adjusts final level with the TX gain slider to match their
specific radio's input sensitivity.

**Revisable because:** On-air testing may reveal that a different default
level works better across the range of common radios. The constant is defined
in Constants.h (TX_AMPLITUDE = 0.25) and can be adjusted.

---

## ADR-010 — DCD noise reference band is 150–400 Hz

**Status:** Revisable
**Date:** June 2026

**Decision:** The DCD noise floor reference is measured in the 150–400 Hz
band, immediately below the HAVEN-FSK signal band (450–1050 Hz).

**Reasoning:** This band is typically clean HF background noise on an SSB
receiver with no signal present. It is below the HAVEN-FSK audio range,
above the DC offset and low-frequency rumble region, and not occupied by
other common digital modes at the same dial frequency. Measuring noise just
below the signal band gives a reference that tracks the actual noise floor
as band conditions change.

**Revisable because:** On-air testing on specific bands or with specific
radios may reveal interference in this reference band. The band limits are
not currently in Constants.h as named constants — if on-air testing suggests
adjustment, they should be promoted to constants.

---

## ADR-012 — H matrix embedded as C++ constant, not generated at runtime

**Status:** Decided
**Date:** June 2026

**Decision:** The LDPC(192,96) parity check matrix is embedded in FEC.cpp
as a static constant array of nonzero column indices, generated once from
the Python reference and hard-coded.

**Reasoning:** The Python reference uses numpy's MT19937 RNG with seed 1234.
Replicating numpy's exact RNG behavior in C++ would require implementing
numpy's MT19937 variant, which has subtle differences from std::mt19937.
Any discrepancy in the matrix would silently break interoperability between
the Python and C++ implementations. Embedding the matrix from Python output
guarantees byte-for-byte identical matrices on all platforms with zero
runtime cost. The matrix is a published part of the mode specification and
does not change.

**Note:** ADR-008 described the original intent to generate the matrix at
runtime. ADR-012 supersedes ADR-008 for the C++ implementation.

---

## ADR-011 — No DspEngine orchestrator class in Phase 2

**Status:** Revisable
**Date:** June 2026

**Decision:** Phase 2 implements Modulator, Demodulator, DCD, and Preamble
as independent classes. No DspEngine orchestrator class is created in Phase 2.

**Reasoning:** The orchestration responsibility — connecting AudioEngine's
rxDataReady signal to the Demodulator, feeding soft symbols to Preamble
detection, managing the TX pipeline — belongs at the boundary between the
DSP layer and the audio/UI layers. That boundary is not fully defined until
Phase 4 (AudioEngine) is implemented. Creating an orchestrator in Phase 2
risks designing it around assumptions that Phase 4 will invalidate.

**Revisable:** A DspEngine class will likely be added in Phase 4 or Phase 5
once the AudioEngine interface is fully implemented and the signal flow is
clear.

---

## ADR-013 — Audio uses Int16 PCM internally, float32 at DSP boundary

**Status:** Decided
**Date:** June 2026

**Decision:** QAudioSource and QAudioSink are configured for Int16 PCM.
AudioEngine converts to/from float32 at the boundary before emitting
rxDataReady and before writing TX samples.

**Reasoning:** Qt6 Multimedia's platform backends (WASAPI on Windows,
ALSA on Linux, CoreAudio on macOS) have the widest device compatibility
with Int16 PCM. Some USB radio interfaces and virtual audio cables do
not correctly negotiate Float32 format even when the Qt API accepts it.
Int16 at 48000 Hz mono is universally supported. The conversion loss
(~96 dB dynamic range from 16-bit) is far beyond what HF radio SNR
requires. The DSP layer always sees float32 regardless.

---

## ADR-014 — Sample rate fixed at 48000 Hz, not user-configurable

**Status:** Decided
**Date:** June 2026

**Decision:** The sample rate is fixed at 48000 Hz and is not exposed
as a user setting. AudioEngine always opens devices at 48000 Hz and
reports an error if the device does not support it.

**Reasoning:** The HAVEN-FSK symbol rate of 31.25 Hz requires that
SAMPLE_RATE / SYMBOL_RATE be a whole number. Of the common audio rates,
only 8000 Hz (256 SPS), 16000 Hz (512 SPS), and 48000 Hz (1536 SPS)
satisfy this constraint. 8000 and 16000 Hz are not supported by HF radio
USB interfaces and provide insufficient headroom above the tone range.
48000 Hz is the universal standard for USB radio interfaces, virtual
audio cables, and software-defined radio applications. Exposing a sample
rate setting would offer options (e.g. 44100 Hz) that cannot work with
this mode, causing operator confusion. Changing the sample rate would
require publishing a new HAVEN-FSK specification with a new FCC emission
designator — it is a protocol change, not a configuration change.

---

## ADR-015 — Three-tier TX backoff replaces two-tier spec

**Status:** Decided
**Date:** June 2026

**Decision:** The TX backoff system uses three tiers based on operating
context, replacing the two-tier CQ/other system in the original spec:

- CQ transmissions (TX text contains "CQ", case-insensitive): 0ms — no hold.
  The operator has already checked the frequency before calling CQ.
- Activator mode (POTA or SOTA reference entered in Station Information):
  0-50ms random. The activator is running the frequency and should respond
  quickly. After all, it is their frequency.
- All other transmissions: 50-300ms random. The 50ms floor is deliberate —
  it guarantees a responding station always waits at least as long as an
  activator's maximum delay, preventing responding stations from transmitting
  simultaneously with the activator.

**Reasoning:** The original spec's 1500ms maximum was too long for POTA/SOTA
activator use. The randomized window prevents the synchronized-release problem
where all stations hear DCD clear simultaneously and transmit at the same
instant. The 50ms floor on standard transmissions ensures the activator always
gets first access to the channel after a DCD holdoff.

**DCD check always applies** regardless of tier. If the channel is busy,
the software notifies the operator and does not transmit. The operator
decides whether to retry.

---

## ADR-016 — Full RadioInterface defined now, partially implemented

**Status:** Decided
**Date:** June 2026

**Decision:** RadioInterface is expanded to define the full rig control
interface in Phase 5B, even though only PTT and frequency readback are
implemented in Phase 5B. Frequency set, mode, and split are stubbed.

**Full interface:** connect/disconnect/isConnected/rigName/setPTT/getFrequency/
setFrequency/getMode/setMode/setSplit — all pure virtual.

**Reasoning:** Operators should not need to switch between HAVEN-FSK and
their SDR front end or logging software during a QSO. Full rig control
from within HAVEN-FSK is a long-term goal. Defining the interface now
prevents breaking changes to RadioInterface later when clients
(RigctldClient, TCIClient, future HamlibClient) are already deployed.

---

## ADR-017 — RigctldClient uses rigctld TCP protocol

**Status:** Decided
**Date:** June 2026

**Decision:** RigctldClient connects to a running rigctld instance over
TCP. Default host:port is localhost:4532 (rigctld universal default).
Host and port are user-configurable in Settings → Radio Control.

**Reasoning:** rigctld covers essentially every radio made in the last
30 years via the Hamlib library. Most operators running digital modes
already have rigctld running since WSJT-X, fldigi, and JS8Call all
support it. No Hamlib library dependency in the HAVEN-FSK build —
rigctld handles the radio-specific protocol. This is the most universal
radio control method available.

**PTT:** `T 1\n` to key, `T 0\n` to unkey.
**Frequency get:** `f\n` returns frequency in Hz as ASCII integer.
**Frequency set:** `F {hz}\n`
**Mode set:** `M {mode} {passband}\n`

**HamlibClient** (direct Hamlib linking) is deferred to a future phase.

---

## ADR-018 — TCIClient uses TCI protocol 2.0 core commands

**Status:** Decided
**Date:** June 2026

**Decision:** TCIClient connects via WebSocket to a TCI server (Thetis,
ExpertSDR2/3, or any TCI-compatible SDR front end). Default host:port is
localhost:50001 (standard Thetis default). Host and port are
user-configurable in Settings → Radio Control.

**Protocol version handling:** The server announces its version in the
handshake. HAVEN-FSK parses and logs the version string for diagnostics
but does not change behavior based on it. Core commands are identical
across all TCI versions from 1.5 onwards.

**Commands:** PTT on: `trx:0,true;` / PTT off: `trx:0,false;`
Frequency readback: parse incoming `vfo:0,0,{hz};` messages.
Handshake: wait for `ready;` before sending any commands.

**Unknown server messages are silently ignored** per TCI specification.

**Your Thetis HL2 setup uses port 40001** (non-default). Configure this
in Settings → Radio Control → TCI Port.

---

## ADR-019 — Station Information displayed on main window

**Status:** Decided
**Date:** June 2026

**Decision:** A non-editable Station Information display block appears
permanently on the main window showing exactly what the macros will send.
The operator edits values in Settings → Station Information.

**Fields:** Callsign, grid square, active POTA references (up to 4),
SOTA reference, Field Day class/section, operator name.

**FCC compliance:** If callsign is empty, all TX is blocked with a clear
warning. Transmitting without a callsign violates FCC Part 97.

---

## ADR-020 — Multiple POTA references supported (up to 4)

**Status:** Decided
**Date:** June 2026

**Decision:** Station Information supports up to four simultaneous POTA
references for activators at park boundary overlaps. The `<myParks>` macro
tag expands to all populated references space-separated.

**Example:** K-1234 and K-5678 populated → `<myParks>` expands to `K-1234 K-5678`.

**Reasoning:** Park overlaps are common in POTA, particularly where state
parks, national forests, and county parks share boundaries. Four references
covers all known real-world overlap scenarios.

---

## ADR-021 — Macro system with <TX> tag for auto-transmit

**Status:** Decided
**Date:** June 2026

**Decision:** Macros containing `<TX>` automatically transmit when clicked.
Macros without `<TX>` expand into the TX input field for operator review.

**Available macro tags (Phase 5C):** `<myCall>`, `<theirCall>`, `<myParks>`,
`<mySOTA>`, `<myGrid>`, `<myName>`, `<myFD>`, `<TX>`.

---

## ADR-022 — Click-to-populate from RX window replaces auto-populate

**Status:** Decided
**Date:** June 2026

**Decision:** `<theirCall>` and other log entry fields are populated by
the operator clicking on structured data elements in the RX decoded message
display. There is no automatic population from decoded messages.

**Reasoning:** Auto-populating from the last decoded callsign creates a
serious problem in pileup operation — an intruding station would
automatically replace the callsign the operator is working.

---

## ADR-023 — RX display parsed for clickable structured data

**Status:** Decided
**Date:** June 2026

**Decision:** The RX decoded message display parses each message for
structured data elements (callsigns, RST, POTA/SOTA refs, grid squares,
Field Day exchange) and renders them as clickable items at display time.

---

## ADR-024 — PTT watchdog 120 seconds, operator notified on trip

**Status:** Decided
**Date:** June 2026

**Decision:** PTTManager implements a 120-second TX watchdog timer
(PTT_WATCHDOG_SEC from Constants.h). If TX remains active for 120 seconds
continuously, the watchdog unkeys the radio and emits watchdogTripped().

**Reasoning:** FCC Part 97 and good amateur practice require identifiable,
non-continuous transmissions. 120 seconds covers any legitimate HAVEN-FSK
frame (32-block maximum ≈ 50 seconds) while catching stuck-PTT conditions.

---

## ADR-025 — Logging RX side vs export side separated

**Status:** Decided — details deferred
**Date:** June 2026

**Decision:** The RX-side logging design (click-to-populate, their parks,
log entry fields) is designed and implemented independently of the log
export format. Export format (ADIF, POTA CSV, SOTA CSV, Cabrillo) is
discussed and implemented separately in the logging phase.

---

## ADR-026 — Single export button, activity type auto-detected

**Status:** Decided
**Date:** June 2026

**Decision:** The log export UI presents a single Export button. The export
engine examines station information and log entries to determine which
export files to generate automatically.

**Auto-detection logic:**
- POTA refs in station info → one ADIF per park reference
- SOTA ref in station info → one ADIF with MY_SOTA_REF
- POTA + SOTA both present → POTA files each containing MY_SOTA_REF
- No activity refs → general ADIF only
- All cases → general ADIF always generated

**Reasoning:** Operators should not need to decide which export format to
use. The system knows what activity is in progress from the station info
and generates exactly what is needed.

---

## ADR-027 — POTA export: one ADIF file per park reference

**Status:** Decided
**Date:** June 2026

**Decision:** For POTA activations, the export engine generates one ADIF
file per active park reference. Each file contains all QSOs with
MY_SIG_INFO set to that park's reference.

**Filename convention:** `{callsign}@{park-ref}-{YYYYMMDD}.adi`
Example: `WD9N@K-1234-20260621.adi`

**Required POTA ADIF fields per QSO:**
- STATION_CALLSIGN, OPERATOR, CALL
- QSO_DATE, TIME_ON, BAND, MODE, SUBMODE, FREQ
- MY_SIG: POTA
- MY_SIG_INFO: activator's park reference for this file
- SIG: POTA (P2P contacts only)
- SIG_INFO: their park reference(s) (P2P credit)

**Reasoning:** POTA requires separate log files per park for multi-park
activations. HAVEN-FSK eliminates the current manual edit-and-duplicate
workflow by generating all files automatically.

---

## ADR-028 — SOTA export uses ADIF format, not CSV

**Status:** Decided
**Date:** June 2026

**Decision:** SOTA log export uses ADIF format. SOTA's database accepts
ADIF uploads. The SOTA CSV V2 format is not implemented.

**SOTA ADIF fields:**
- MY_SOTA_REF — activator's summit reference (e.g. W7W/SE-001)
- SOTA_REF — contacted station's summit reference (S2S only)

**Filename:** `{callsign}-{summit-ref-sanitized}-{YYYYMMDD}.adi`

**Reasoning:** SOTA now accepts ADIF, making CSV unnecessary. Using ADIF
for SOTA means the same export engine handles all activity types.

---

## ADR-029 — Combined POTA+SOTA handled in one export

**Status:** Decided
**Date:** June 2026

**Decision:** When both POTA and SOTA references are present, the export
engine generates POTA ADIF files each containing MY_SOTA_REF. No separate
SOTA-only file is generated — the POTA files satisfy both programs when
uploaded to both pota.app and sotadata.org.uk.

---

## ADR-030 — General ADIF always generated

**Status:** Decided
**Date:** June 2026

**Decision:** Every export operation generates a general ADIF file in
addition to any activity-specific files. Contains all fields including
activity-specific ones — programs that don't understand those fields
ignore them.

**Filename:** `{callsign}-{YYYYMMDD}.adi`

**Purpose:** LoTW (via TQSL), QRZ logbook, eQSL, and any other logging
service or application the operator uses.

---

## ADR-031 — LoTW upload handled by TQSL, not HAVEN-FSK

**Status:** Decided
**Date:** June 2026

**Decision:** HAVEN-FSK generates a standard ADIF for LoTW use. The
operator submits to LoTW by opening the ADIF in TQSL. HAVEN-FSK does
not integrate with TQSL or the LoTW API.

**Reasoning:** LoTW requires cryptographic signing using the operator's
TQSL certificate. This is handled by the TQSL application, which every
LoTW user already has installed. Integration would add significant
complexity for no practical benefit.

---

## ADR-032 — Field Day export is general ADIF only, no Cabrillo

**Status:** Decided
**Date:** June 2026

**Decision:** When Field Day class and section are present in station
information, export generates a general ADIF only. No Cabrillo format
is implemented.

**Reasoning:** The ARRL has changed Field Day submission format
requirements multiple times, breaking third-party software. Many Field
Day operators already use dedicated contest logging software (N1MM+,
N3FJP ACLog) that handles FD submission. HAVEN-FSK exports clean ADIF
for import into FD logging software. FD class and section are captured
in the log data model for completeness.

---

## ADR-033 — Log data model captures all fields for all export formats

**Status:** Decided
**Date:** June 2026

**Decision:** Every QSO record captures the complete set of fields needed
for any export format.

**QSO record fields:**

Core: their_callsign, date_utc, time_utc, frequency_mhz, band, mode, submode

Standard: rst_sent, rst_received, their_name, their_qth, their_grid, notes

POTA: their_pota_refs (list, up to 4)
SOTA: their_sota_ref
Field Day: their_fd_class, their_fd_section

Station info snapshot (copied at log time):
my_callsign, my_grid, my_pota_refs, my_sota_ref, my_fd_class,
my_fd_section, my_op_name

---

## ADR-034 — Click-to-populate their POTA parks adds all parks at once

**Status:** Decided
**Date:** June 2026

**Decision:** Clicking on a POTA park reference cluster in the RX display
adds ALL references from that message to Their Parks simultaneously.

**Reasoning:** FEC accuracy means decoded references can be trusted.
Treating the park cluster as one clickable unit matches natural reading —
"K-1234 K-5678" is one piece of information. Tag-based UI allows removal
of individual incorrect references.

---

## ADR-035 — Station info snapshot copied to QSO at log time

**Status:** Decided
**Date:** June 2026

**Decision:** When a QSO is logged, current station information is copied
into the QSO record as a snapshot. The QSO does not reference station
info dynamically.

**Reasoning:** A rover activating multiple parks updates station info
between parks. QSOs logged at K-1234 must export with MY_SIG_INFO:K-1234
even after station info is updated to K-5678. The snapshot ensures each
QSO accurately reflects the operating location at time of contact, which
is a POTA requirement.

---

## ADR-036 — MODE field in ADIF export

**Status:** Decided
**Date:** June 2026

**Decision:** ADIF exports use MODE:DIGITAL and SUBMODE:HAVEN-FSK.
The mode string is a constant, not hard-coded in multiple places, so it
can be updated easily when HAVEN-FSK is added to the ADIF specification
as a recognized submode.

**Reasoning:** HAVEN-FSK is a new mode not yet in the ADIF submode list.
DIGITAL is the correct fallback. Once the mode gains adoption, a request
to add it as a recognized ADIF submode is appropriate.

## ADR-020 — REVISED: POTA references unbounded list, no fixed limit

**Status:** Decided (revised from original ADR-020)
**Date:** June 2026

**Revision:** The original ADR-020 specified a maximum of four POTA
references. This limit has been removed. The POTA reference list is now
unbounded.

**Reasoning:** Operators in high-density areas (e.g. Washington DC) may
simultaneously activate seven or more overlapping parks — national
monuments, national parks, C&O Canal, Rock Creek Park, and others can all
overlap at a single operating location. A fixed limit of four would
prevent valid activations in these areas. The list is now dynamic with
no upper bound.

**Implementation:** QSettings stores the list as a QStringList under
a single key (station/pota/refs) rather than four numbered keys. The
UI presents a dynamic list widget with Add/Remove buttons. The
`StationInfo.potaRefs` field is a QStringList. The `<myParks>` macro
expands to all references space-separated regardless of count. Export
generates one ADIF file per reference regardless of count.

## ADR-037 — Settings organized in tabbed dialog

**Status:** Decided
**Date:** June 2026

**Decision:** All operator-configurable settings are in a single tabbed
QDialog: Station Information, Radio Control, and Audio Devices. A single
Settings menu item (Ctrl+,) opens the dialog. Apply button allows
verification without closing.

**Reasoning:** Standard pattern for application preferences. Keeps all
configuration in one place. Operator can configure everything before
first transmission without hunting through multiple menus.

## ADR-038 — TX blocked at FCC compliance guard if callsign empty

**Status:** Decided
**Date:** June 2026

**Decision:** If operator callsign is empty, all TX is blocked. TX button
and input field are disabled. Attempting TX via Enter key shows a warning
dialog citing FCC Part 97.119. Station Information display shows
"NO CALLSIGN" in red. Guard is checked on startup and on every
settings change.

**Reasoning:** Transmitting without identifying by callsign violates
FCC Part 97.119. The software makes this requirement visible and prevents
accidental unidentified transmissions. The warning disappears as soon as
the operator enters their callsign.

## ADR-039 — RS report computed from physical signal measurements

**Status:** Decided
**Date:** June 2026

**Decision:** HAVEN-FSK generates RS (Readability/Strength) signal reports
from actual physical measurements rather than conventional 599 reporting.
Tone (T) is omitted as it is not applicable to digital modes.

R (Readability) derived from FEC convergence:
- R1: FEC did not converge (CRC fail)
- R3: Converged >150 iterations (marginal)
- R4: Converged 50-150 iterations (good)
- R5: Converged <50 iterations (excellent)

S (Strength) derived from measured SNR:
- S1-S9 mapped from dB SNR using standard 6dB/S-unit scale

RS is cached per sending station for 10 minutes. When operator clicks
a callsign in the RX window, the cached RS for that station populates
RS-S in the log entry and <rstSent> in macros automatically.

This is documented in the HAVEN-FSK specification as a defined feature.

**Reasoning:** Conventional 599 reporting is meaningless in digital modes.
HAVEN-FSK has the data to generate accurate reports — FEC iteration count
and DCD band energy ratio are already computed for every decoded frame.
Using them for RS reports is a genuine differentiator and provides
operators with meaningful signal quality information.

## ADR-040 — RS cache keyed by sender callsign, 10-minute expiry

**Status:** Decided
**Date:** June 2026

**Decision:** DspPipeline maintains a QMap<QString, RxMeasurement>
cache of per-station signal measurements. Cache entries expire after
10 minutes. The sender callsign is parsed from decoded frame text
using the standard DE pattern. Cache is in memory only.

**Reasoning:** In pileup operation multiple stations decode sequentially
(not simultaneously — our backoff system staggers transmissions and
simultaneous signals don't decode). The cache correctly associates
each measurement with the specific station that generated it. If two
stations somehow decode simultaneously and corrupt each other, neither
decodes — the cache receives nothing, failing safely.

## ADR-041 — Structured field tags in transmitted messages

**Status:** Decided
**Date:** June 2026

**Decision:** HAVEN-FSK defines optional structured field tags for
machine-parseable data in free-text messages. Tags use FIELD:value
format. Documented in the HAVEN-FSK specification.

Defined tags:
- NAME:  operator name
- QTH:   location (state, province, country)
- GRID:  Maidenhead grid square
- RS:    signal report (RS format, not RST)
- POTA:  park reference(s), space-separated, US-XXXX format
- SOTA:  summit reference, XX/XX-XXX format
- FD:    Field Day exchange (class + section)

POTA references use current ISO country code format (US-XXXX)
not legacy ham prefix format (K-XXXX).

The RxDisplay parser recognizes these tags and renders values as
clickable elements that populate log entry fields automatically.

**Reasoning:** HAVEN-FSK is a free-text mode. Structured tags are
opt-in — operators who use them get click-to-populate efficiency,
operators who don't use them lose nothing. The <stationInfo> macro
makes structured tag transmission trivial.

## ADR-042 — Two macro banks (A/B) of 8 buttons, manually switched

**Status:** Decided
**Date:** June 2026

**Decision:** MacroPanel provides two banks (A and B) of 8
user-configurable macro buttons. Banks are switched manually via
A/B selector buttons. No automatic bank switching based on operating
context — operator controls which bank is active at all times.

Bank A defaults: activating macros (CQ POTA, Stn Info, TU 73, etc.)
Bank B defaults: chasing/general macros (CQ, Stn Info, TU 73, etc.)

Macros containing <TX> auto-transmit when clicked. Macros without
<TX> populate the TX input field for operator review.

Right-click any button to edit label and macro text. All macros
persist in QSettings. Default macros pre-populated on first run.

**Reasoning:** Manual bank switching respects operator customization.
Auto-switching based on activator mode could disrupt operators who
have customized their banks differently. Banks are labeled A/B with
no implied purpose so operators can use them however they prefer.

## ADR-043 — Log panel unified: entry strip + contact table

**Status:** Decided
**Date:** June 2026

**Decision:** The log entry fields and recent contacts list are
presented as a single unified LogPanel widget. The entry strip
occupies the top of the panel with the same column alignment as
the completed contacts table below. To the operator it reads as
one continuous log window — the top row is the contact being worked.

Clicking a completed contact row re-populates the entry strip for
correction. Most recent contact is always at the top of the table.

Context-adaptive field visibility:
- POTA fields visible when station info has POTA references
- SOTA field visible when station info has SOTA reference
- Field Day mode (menu toggle) replaces activity fields with FD exchange
- General fields (grid, name, QTH) hidden in Field Day mode

**Reasoning:** Separating log entry from log display creates an
artificial visual boundary in what is logically one continuous
activity. The unified panel matches the familiar feel of other
logging software and reduces the operator's visual scanning area.

## ADR-044 — Splitter layout with QSettings persistence

**Status:** Decided
**Date:** June 2026

**Decision:** The main window uses a QSplitter covering the waterfall
placeholder, RX display, and log panel. Fixed elements (station info
block, macro buttons, TX input, status bar) are outside the splitter.
Splitter state is saved to QSettings on close and restored on startup.

**Reasoning:** Different operators have different priorities — a POTA
activator running a pileup wants maximum log panel visibility, a casual
ragchewer wants maximum RX text visibility. Resizable panels let each
operator configure the layout for their operating style, and persistence
means they only configure it once.

## ADR-045 — SQLite log database with WAL mode for crash safety

**Status:** Decided
**Date:** June 2026

**Decision:** QSO log is persisted to SQLite via Qt6::Sql (QSQLITE
driver). Database uses WAL (Write-Ahead Logging) journal mode and
NORMAL synchronous mode. Each contact is written immediately when
the operator clicks Log It — no buffering or batch writes.

**Database location:** QStandardPaths::AppDataLocation —
%APPDATA%\WD9N\HAVEN-FSK\haven_fsk_log.db on Windows,
~/.local/share/WD9N/HAVEN-FSK/haven_fsk_log.db on Linux.

**Reasoning:** WAL mode allows reads concurrent with writes and
provides crash safety — a crash mid-write leaves the database in
its last consistent state. Writing immediately on Log It means no
contacts are lost to application crashes or power loss in the field.
SQLite is built into Qt6 (QSQLITE driver) with no additional
dependency.

## ADR-046 — All station info snapshotted per QSO in database

**Status:** Decided
**Date:** June 2026

**Decision:** All my_* fields (callsign, grid, pota_refs, sota_ref,
fd_class, fd_section, op_name) are stored directly in each contact
row. There is no separate sessions table.

**Reasoning:** Per ADR-035, station info must be snapshotted at log
time. A rover activating multiple parks updates station info between
parks — QSOs logged at US-1234 must export with MY_SIG_INFO:US-1234
even after station info changes to US-5678. The self-contained row
approach makes export queries simple and ensures historical accuracy.

## ADR-047 — Export UI: two-option dialog, current day or date picker

**Status:** Decided
**Date:** June 2026

**Decision:** File → Export Log... opens a minimal dialog with two
options: Current UTC day (default, one radio button) or Select date
(date picker). Preview shows exactly which files will be generated
before Export is clicked. Confirmation after export lists files
created. Export folder defaults to Documents/HAVEN-FSK/ and is
remembered in QSettings.

**Reasoning:** Operators in the field need one-click export of
today's activation. Operators at home need to retrieve past
activations. Two options cover both cases with minimal UI surface.
The preview eliminates uncertainty about what will be exported.

## ADR-048 — ADIF export auto-detects activity from contact records

**Status:** Decided
**Date:** June 2026

**Decision:** AdifExporter examines my_pota_refs and my_sota_ref
fields in the selected contacts to determine which files to generate.
No operator input required beyond date selection.

**Output per UTC date:**
- One ADIF per distinct my_pota_ref (POTA activation files)
- One ADIF per distinct my_sota_ref if no POTA refs (SOTA file)
- Combined POTA+SOTA: POTA files contain MY_SOTA_REF
- General ADIF always generated (LoTW, QRZ, eQSL)

**POTA filename:** {callsign}@{park_ref}-{YYYYMMDD}.adi
**SOTA filename:** {callsign}-{sanitized_ref}-{YYYYMMDD}.adi
**General filename:** {callsign}-{YYYYMMDD}.adi

**MODE:** DIGITAL, **SUBMODE:** HAVEN-FSK in all exports.
**RS reports:** RST_SENT and RST_RCVD fields contain two-digit RS
values (no tone component) as generated by the RS measurement cache.

## ADR-049 — Waterfall receives raw audio, never AFC-corrected

**Status:** Decided
**Date:** June 2026

**Decision:** WaterfallWidget::pushChunk() is connected directly to
AudioEngine::rxDataReady with Qt::QueuedConnection. It receives raw,
unmodified audio. DspPipeline::onAudioChunk() applies AFC correction
to a separate copy of the audio for demodulation only.

**Reasoning:** If the waterfall received AFC-corrected audio, the operator
would see signals centered on the passband markers regardless of actual
tuning accuracy. This would make the waterfall misleading — the operator
could not tell whether they were well-tuned or AFC was compensating for
poor tuning. Two operators in contact could also see apparently-correct
displays while their radios are significantly off from each other. The
waterfall must always show truth.

## ADR-050 — Two waterfall marker sets: fixed gray and floating green

**Status:** Decided
**Date:** June 2026

**Decision:** Two sets of vertical lines on the waterfall:
- Gray dashed (#B8B8B8, 1.2px): fixed at BASE_FREQ and
  BASE_FREQ + NUM_TONES * SYMBOL_RATE. Never move. Show where
  signal SHOULD be.
- Soft green solid (#6DB640, 1.5px): float at above frequencies
  plus afcOffsetHz. Show where AFC is actually tracking the signal.
  Only visible when |afcOffset| >= 0.5 Hz.

Color #6DB640 chosen after testing against all four palettes. Provides
readable contrast against Earth (warm tones), Classic (yellow zone),
Greyscale (mid-grey), and Night (orange/yellow) without being harsh.

**Reasoning:** The two-marker system gives the operator complete
information: where signals should be (gray) and where they actually
are (green). When green sits on gray, AFC offset is near zero and tuning
is accurate. When green drifts from gray, the operator can see the
offset and decide whether to retune or let AFC handle it.

## ADR-051 — Waterfall default palette is Earth, default speed is Slow

**Status:** Decided
**Date:** June 2026

**Decision:** Default palette: Earth (dark forest → bark brown →
amber → gold → warm white). Default speed: Slow (~3 rows/second,
one row per 8 audio chunks). Both settings persisted in QSettings.

**Reasoning:** Earth tones are visually distinctive from other digital
mode software and consistent with HAVEN-FSK's field operating identity.
Slow default speed suits relaxed field operation — a fast waterfall
creates unnecessary urgency. Operators who prefer faster can adjust.

## ADR-052 — Waterfall right-click tuning with status bar confirmation

**Status:** Decided
**Date:** June 2026

**Decision:** Right-click enters tuning mode with a bright movable line.
Left-click confirms and calls setFrequency(). Escape cancels.
Status bar shows "Tune to X.XXXXXX MHz [Left-click confirms | Esc cancels]"
during tuning.

**Tune math:** newDialFreq = currentDialFreq + (clickedAudioHz - BASE_FREQ)
This places the lowest HAVEN-FSK tone at the clicked audio frequency.

**If no rig connected:** status bar message, no frequency change.

## ADR-053 — AFC is digital RX-only correction, TX never moves

**Status:** Decided
**Date:** June 2026

**Decision:** AFC applies an NCO (Numerically Controlled Oscillator)
correction to received audio only. The radio VFO is never moved by AFC.
TX frequency is always exactly what the operator set.

**Primary use case:** Correcting inter-station calibration differences.
Two operators tuned to the same nominal frequency may be 10-40 Hz apart
due to different radio calibrations. AFC corrects this on receive.

**Secondary use case:** Tracking thermal drift during a QSO as the
contacted station's finals warm up.

**Two-stage operation:**
1. Hard lock on preamble: direct measurement, immediate correction,
   NCO phase reset. Handles the jump when a new station calls.
2. Slow tracking during frame (alpha=0.02, ~50 symbol periods ~1.6s):
   follows thermal drift without chasing noise.

**Between contacts:** partial reset (offset × 0.5). Keeps half the
previous correction as a starting point for the next station.

**Range:** ±75 Hz. Beyond this AFC clamps and warns operator to retune.

**Why TX never moves:** If AFC moved the radio VFO, two stations in
contact would mutually track each other's corrections and slowly drift
up the band together. Each station independently corrects its own
receive path — no coordination needed, no frequency crawl.

## ADR-054 — FrequencyControl replaces static frequency label

**Status:** Decided
**Date:** June 2026

**Decision:** Status bar frequency display is a FrequencyControl widget:
editable MHz field, ▲/▼ step buttons, right-click step size menu
(1/10/100/1000 Hz). Enter after typing sends setFrequency().

**Three complementary tuning methods:**
1. Waterfall right-click (mouse users)
2. FrequencyControl step buttons (touchpad/laptop users)
3. FrequencyControl direct entry (all users)
4. Thetis/radio VFO (TCI users — HAVEN-FSK follows automatically)

All methods send setFrequency() to the active RadioInterface.

## ADR-055 — Radio Control in Radio menu, not Settings dialog

**Status:** Decided
**Date:** June 2026

**Decision:** Radio control configuration (method, rigctld host/port,
TCI host/port) is accessed via Radio → Configure... on the main menu
bar. It is not part of the Settings dialog. Settings contains only
Station Information and Audio device selection.

**Reasoning:** Radio control is an operational setting operators may
need to change quickly between sessions or when switching radios.
Embedding it in the Radio menu where Connect/Disconnect also lives
keeps all radio-related actions in one place. The Settings dialog
is for station identity and audio hardware selection.

## ADR-056 — POTA reference auto-correction to XX-NNNN format

**Status:** Decided
**Date:** June 2026

**Decision:** POTA references are auto-corrected to the canonical
format: two uppercase letters, hyphen, one or more digits (XX-NNNN).
Any two-letter country prefix is accepted — the validator does not
restrict to known POTA country codes to avoid maintenance burden
as POTA expands internationally. Auto-correction inserts the hyphen
if missing (US1234 → US-1234) and forces uppercase.

## ADR-057 — Log inline edit mode on double-click

**Status:** Decided
**Date:** June 2026

**Decision:** Double-clicking a completed contact row in the log
panel enters inline edit mode. The entry strip populates with the
contact's data, Log It changes to Update, and saving overwrites
the existing database record preserving the original date/time.
Cancel returns to normal entry mode without changes.

**Reasoning:** Operators need to correct errors (wrong callsign,
RS report, missing park reference) immediately after logging without
hunting through a separate editor dialog. Inline edit is the fastest
correction path for the common case.

## ADR-058 — Frequency always manually enterable regardless of rig control

**Status:** Decided
**Date:** June 2026

**Decision:** The FrequencyControl widget is always editable. When
rig control is connected, the field shows amber text (rig-sourced).
When no rig control is connected, the field shows grey text with
"Enter MHz" placeholder — the operator types the frequency and presses
Enter for logging purposes. Manual entry does not send setFrequency()
to any radio interface.

**Reasoning:** Operators using older radios without CAT or TCI support
still need to log the operating frequency. Requiring rig control for
frequency entry would exclude a significant portion of field operators
using vintage or budget equipment.

## ADR-065 — PTTManager wired into MainWindow TX sequence

**Status:** Decided
**Date:** June 2026

**Decision:** PTTManager is instantiated in MainWindow and wired
into the TX sequence. PTT is keyed via PTTManager before audio
plays and unkeyed in onTxComplete() after audio finishes.
PTTManager is recreated when the radio connects to receive the
active RadioInterface pointer. Operating mode (Standard/Activator)
is set from station info whenever settings change.

**Reasoning:** Phase 5B implemented PTTManager but it was not
wired into the MainWindow TX flow. First-run testing revealed
that the radio never received PTT commands because nothing was
calling PTTManager::requestTX(). The TX audio was playing (or
attempting to play) without the radio being keyed.

## ADR-066 — TX audio debug logging for diagnosis

**Status:** Decided
**Date:** June 2026

**Decision:** Comprehensive qDebug() logging added throughout the
TX pipeline — txAudioReady handler, AudioEngine::startTx(),
onTxStateChanged(), PTTManager, and TCIClient::setPTT(). Logging
remains in the codebase to assist future debugging but can be
disabled by defining QT_NO_DEBUG_OUTPUT in release builds.

**Reasoning:** First-run testing revealed TX completing instantly
without audio playing. Systematic debug logging throughout the
pipeline is the fastest path to diagnosing where the failure occurs.

## ADR-071 — TX sequencing: configurable PTT lead and tail times

**Status:** Decided
**Date:** June 2026

**Decision:** TX sequence adds two configurable delays:
- PTT lead time (default 150ms): delay from PTT assert to audio
  start, giving radio time to switch from RX to TX
- TX tail time (default 200ms): delay from audio end to PTT
  release, ensuring audio drains from hardware buffer before
  radio switches back to RX

Both are configurable in Radio → Configure... TX Sequencing.
PTT lead is implemented via QTimer::singleShot() in the
txAudioReady handler. TX tail is implemented via QTimer::singleShot()
in onTxComplete(). The AudioEngine TX timer covers audio duration
only (plus 50ms WASAPI acceptance latency).

**Reasoning:** Different radios require different lead times —
SDR/TCI (50-150ms), modern CAT (100-200ms), older relay-switched
radios (200-500ms). A fixed hardcoded delay would be wrong for
many operators. Configurable values let each operator tune for
their specific radio. The TX tail ensures the last part of audio
is not cut off when the radio switches back to RX before the
hardware buffer drains.

**Default values:** 150ms lead and 200ms tail were chosen as
conservative defaults that work correctly for HL2/Thetis TCI
(the primary development platform) while being acceptable for
most other radios without operator intervention.

## ADR-072 — AudioEngine TX uses QBuffer pull mode (platform-correct)

**Status:** Decided
**Date:** June 2026

**Decision:** AudioEngine TX uses QAudioSink in pull mode.
PCM data is stored in a QByteArray member (m_txPcmBuffer),
wrapped in a QBuffer member (m_txQBuffer), and passed to
QAudioSink::start(QIODevice*). Qt6 pulls data from the buffer
as the platform audio backend needs it. IdleState in pull mode
correctly signals buffer exhaustion (genuine playback complete).

**Platform backend selection in main.cpp:**
- Windows: QT_MULTIMEDIA_PREFERRED_PLUGINS=windowsmediafoundation
- Raspberry Pi (ARM Linux): QT_MULTIMEDIA_PREFERRED_PLUGINS=alsa
- Linux desktop: Qt auto-selects PulseAudio or PipeWire

**Reasoning:** Push mode write() on Windows WASAPI silently
discards audio beyond the backend's internal buffer size (~500ms),
causing only 500ms of a 3.84 second transmission to play. Pull
mode is the correct Qt6 approach for raw PCM playback — Qt handles
the backend-specific chunking internally. The QByteArray and QBuffer
are members (not locals) ensuring they remain valid for the entire
playback duration. Platform backend selection ensures each OS uses
its most capable audio subsystem without any platform-specific
code in AudioEngine itself.

## ADR-072 — REVISED: AudioEngine TX uses QMediaPlayer with in-memory WAV

**Status:** Decided (supersedes previous ADR-072)
**Date:** June 2026

**Decision:** AudioEngine TX uses QMediaPlayer with QAudioOutput
to play pre-computed audio. PCM samples are converted to a valid
WAV file in memory (QByteArray with 44-byte header + int16 PCM),
wrapped in a QBuffer, and played via QMediaPlayer::setSourceDevice().
Completion is signaled by QMediaPlayer::playbackStateChanged(
StoppedState) which fires when playback genuinely finishes.

**Reasoning:** QAudioSink proved unsuitable for one-shot playback
of a pre-computed buffer. Qt6.11 uses the FFmpeg multimedia backend
on Windows which signals IdleState immediately after handing data
to the hardware driver regardless of pull/push mode or buffer size.
Duration timers are fragile and platform-dependent. QMediaPlayer
is the correct Qt6 API for playing a complete audio buffer — it
handles all platform audio complexity internally and provides a
reliable StoppedState signal when playback genuinely finishes.
WAV format is natively supported by Qt6 on all platforms with no
additional dependencies.

## ADR-059 — Radio menu is a direct action, not a submenu

**Status:** Decided
**Date:** June 2026

**Decision:** Clicking "Radio" on the main menu bar immediately
opens RadioConfigDialog. No submenu. Connect and Disconnect
buttons live inside the dialog alongside method selector,
connection fields, and TX sequencing timing controls.

**Reasoning:** First-run testing revealed Radio → Configure...
required two clicks. Direct menu bar action is one click faster
and more intuitive for field operators.

## ADR-060 — POTA settings box compact with no instructional text

**Status:** Decided
**Date:** June 2026

**Decision:** POTA References group in Settings → Station Info
shows only list widget, entry field, and Add/Remove buttons.
No instructional text. List starts at minimum height (40px)
and expands up to 200px as references are added.

**Reasoning:** Instructional text is unnecessary hand-holding.
Operators know what to do. Clean UI reduces visual noise.

## ADR-061 — State and County added to station information

**Status:** Decided
**Date:** June 2026

**Decision:** State/Province and County fields added to Station
Information. Stored in QSettings, snapshotted per QSO in log
database, exported as MY_STATE (standard ADIF) and
APP_HAVEN_MY_COUNTY (custom APP_ field). Available as
<myState> and <myCounty> macro tags. SQLite migration uses
ALTER TABLE ADD COLUMN which fails silently if column exists.

## ADR-062 — Log entry delete with confirmation, default No

**Status:** Decided
**Date:** June 2026

**Decision:** Delete button visible only in edit mode (after
double-clicking a completed contact row). Confirmation dialog
shows contacted station callsign. Default button is No.
Database DELETE uses db_id to target exact record.

**Reasoning:** Deletion is irreversible. Default No prevents
accidental deletion via Enter key. Hidden in normal mode
prevents accidental activation during operating.

## ADR-063 — RS label-field pairs use fixed widths

**Status:** Decided
**Date:** June 2026

**Decision:** RS-R and RS-S labels and fields use setFixedWidth()
on fields and QSizePolicy::Fixed on labels, with addSpacing()
between groups. Only Parks field receives stretch factor 1.

**Reasoning:** Qt layout engine expands widgets to fill space
unless explicitly constrained. Fixed widths prevent label/field
separation regardless of available space.

## ADR-064 — TX messages shown in RX display in amber

**Status:** Decided
**Date:** June 2026

**Decision:** Transmitted messages appear in RxDisplay in amber
(#C8860A) with [TX] prefix and UTC timestamp. Signal emitted
from DspPipeline::transmit() before frame assembly.

**Reasoning:** Operators need conversation continuity — referring
back to sent messages is essential when a contact asks for
clarification. Amber matches application accent color.

## ADR-067 — TCI m_ready flag set when ready message received

**Status:** Decided
**Date:** June 2026

**Decision:** TCIClient parseTCIMessage() sets m_ready = true
when the "ready" message is received from Thetis. m_ready is
reset to false on disconnect so re-handshake is required.
setPTT() and sendTCI() guard on both m_connected and m_ready.

**Reasoning:** m_ready was never set to true — every setPTT call
was silently blocked. Root cause identified via debug console
output showing "connected=true ready=false" on every TX attempt.

## ADR-068 — AudioEngine stopTx() disconnects signals before destroy

**Status:** Decided
**Date:** June 2026

**Decision:** stopTx() calls disconnect(sink, nullptr, this, nullptr)
before stopping and resetting QAudioSink/QMediaPlayer, preventing
stale state change signals from firing during teardown.

## ADR-069 — onSettingsChanged() does not restart radio

**Status:** Decided
**Date:** June 2026

**Decision:** onSettingsChanged() restarts audio only. It does
not call startRadio(). Radio reconnects only when the operator
explicitly uses Radio → Configure... → Connect.

**Reasoning:** Every settings save was disconnecting TCI causing
10-second reconnect delays and WebSocket errors in the log.
Settings dialog has no radio tab — calling startRadio() on
station info or audio changes was always wrong.

## ADR-073 — RX audio source destroyed before TX starts

**Status:** Decided
**Date:** June 2026

**Decision:** AudioEngine::stopRx() destroys QAudioSource
(calls reset()) rather than just stopping it, fully releasing
the audio device before TX begins. TX therefore always finds
the device available.

**Reasoning:** RX QAudioSource holding the VAC device while TX
attempted to use the same device caused audio interference and
clicking/pulsing artifacts in transmitted signal.

## ADR-074 — Modulator uses continuous phase accumulator (CPFSK)

**Status:** Decided
**Date:** June 2026

**Decision:** Modulator::symbolToSamples() replaced the pre-built
tone table lookup with a continuous phase accumulator (m_txPhase)
that carries phase across all symbol boundaries. Each sample is
computed as sin(m_txPhase) where m_txPhase advances by 2π×f/Fs
per sample and wraps to [-π, π] to prevent float drift over long
transmissions. Phase is reset to 0.0 via resetPhase() at the
start of each Frame::assemble() call (also reset by the Modulator
constructor which is called fresh per transmission).

**Reasoning:** The pre-built tone table always started at t=0
(phase=0) for each symbol. When the previous symbol ended at
phase X and the next symbol started at phase 0, the discontinuity
produced an audible click at every symbol boundary (31.25 per
second at the symbol rate). The raised cosine amplitude ramps
reduced but did not eliminate the clicks. Continuous phase FSK
(CPFSK) is the correct implementation — frequency changes at
symbol boundaries but phase continues uninterrupted, producing
a clean signal with no discontinuities. The demodulator is
unaffected as FFT energy detection is phase-independent. The
single Modulator instance in Frame::assemble() carries phase
continuously across header, CRC, and payload sections.

## ADR-075 — Preamble CPFSK + phase seeded to Modulator at preamble→header boundary

**Status:** Decided
**Date:** June 2026

**Decision:** Preamble::generate() uses a continuous phase
accumulator (identical to Modulator fix in ADR-074). The final
phase value is stored in m_finalPhase and exposed via finalPhase().
Frame::assemble() calls mod.setPhase(preamble.finalPhase()) before
the first mod.modulate() call, ensuring the header section starts
with continuous phase from the end of the preamble.

**Result:** The complete transmission — preamble through payload —
is phase-continuous with zero discontinuities. The transmitted
signal is clean CPFSK throughout.

**Reasoning:** The Modulator CPFSK fix (ADR-074) eliminated phase
resets in the header/CRC/payload sections but left two remaining
discontinuities: within the preamble (16 resets at symbol rate)
and at the preamble→header boundary. This fix eliminates all
remaining discontinuities by applying the same CPFSK approach to
the preamble and propagating its final phase to the Modulator.

## ADR-070 — AudioEngine TX uses QMediaPlayer with in-memory WAV

**Status:** Decided
**Date:** June 2026

**Decision:** AudioEngine TX uses QMediaPlayer with QAudioOutput
to play pre-computed audio. PCM samples converted to WAV file
in memory (44-byte header + int16 PCM), wrapped in QBuffer,
played via QMediaPlayer::setSourceDevice(). Completion signaled
by playbackStateChanged(StoppedState).

**Memory impact:** ~361 KB peak per transmission, freed after TX.

**Reasoning:** QAudioSink proved unsuitable for one-shot playback.
Qt6.11 uses FFmpeg multimedia backend on Windows which signals
IdleState immediately after handing data to hardware driver
regardless of pull/push mode or buffer size. QMediaPlayer is
the correct Qt6 API for playing a complete pre-computed audio
buffer — same API used by music players on all platforms.

## ADR-076 — Raised cosine amplitude ramps removed from CPFSK

**Status:** Decided
**Date:** June 2026

**Decision:** Raised cosine amplitude ramps (153 samples = 3.2ms)
removed from both Modulator::symbolToSamples() and
Preamble::generate(). The quarter-sine ramp tables are retained
in the code but no longer applied.

**Root cause of final artifact:** With continuous phase the ramps
had no phase discontinuities to soften. Instead they created a
guaranteed amplitude dip at every symbol boundary — the end-ramp
of symbol N and start-ramp of symbol N+1 both applied at the
same point, reducing amplitude across 20% of each 32ms symbol
period. At 31.25 symbols/second this produced exactly the
pulsing/buzzing artifact heard in testing.

**Verified:** Audacity waveform analysis confirmed clean
constant-envelope continuous sinusoidal waveform after ramp
removal. Audio described as smooth, clean tones.

**Note:** Ramps only have value when phase discontinuities exist.
With CPFSK they are harmful, not helpful. They should be removed
permanently rather than retained as dead code if a future cleanup
pass is made.

## ADR-077 — Radio frequency queried immediately on connection

**Status:** Decided
**Date:** June 2026

**Decision:** RadioInterface gains a virtual requestFrequency()
method (default no-op). TCIClient and RigctldClient implement it.
MainWindow::onRadioConnected() calls requestFrequency() after a
200ms settling delay. RigctldClient also calls requestFrequency()
directly in onConnected() before the first poll timer fires.

**Reasoning:** First-run testing showed frequency display remained
blank at startup until the operator manually tuned the radio.
TCI was discarding vfo: messages received during handshake
(fixed separately). rigctld poll timer interval (2 seconds) caused
a delay before first frequency update. Immediate query on connection
ensures the FrequencyControl widget is populated as soon as the
rig control link is established, regardless of connection method.

## ADR-079 — LED meter panel with console-style faders in bottom left

**Status:** Decided
**Date:** June 2026

**Decision:** LevelPanel widget placed in bottom left of main window
as a fixed-size non-resizable panel. Contains TX and RX channel
strips with 24-LED VU meters (8px round, dBu scale, 0dBu = -6dBFS)
and console-style vertical faders. Fader travel = LED strip height
exactly for 1:1 position-to-level mapping. dBu readout always grey.

Color zones: green -34 to -6dBu, yellow -6 to 0dBu, red 0 to +6dBu.
TX fader controls QAudioOutput volume in real time.
RX fader applies linear gain multiplier before AFC and demodulation.
TX meter shows peak of generated audio. RX meter shows incoming chunk peak.

**Reasoning:** Real-time level control accessible during live
transmission without opening any dialog. Console-style LED meters
with faders are immediately intuitive to operators with audio
backgrounds. Fixed panel preserves QSplitter resizability above.

## ADR-080 — TX input is multi-line QTextEdit

**Status:** Decided
**Date:** June 2026

**Decision:** TX input replaced with QTextEdit (multi-line,
auto-wrap, scrollable, max height 120px). Plain Enter adds newline.
Ctrl+Enter transmits.

**Reasoning:** Station info macros with NAME:/GRID:/POTA: fields
exceed a single line. Multi-line with scroll lets the operator see
the entire message before transmitting.

## ADR-081 — Macro panel shows both banks simultaneously

**Status:** Decided
**Date:** June 2026

**Decision:** MacroPanel displays both Bank A and Bank B as two
always-visible rows rather than a switchable single row. Bank
label (A/B) on left of each row identifies the bank. No toggle
button needed. 16 macro buttons always accessible.

**Reasoning:** First-run testing showed large unused space above
TX textarea. Showing both banks eliminates wasted space and removes
the need to switch banks — all macros visible simultaneously.

## ADR-082 — Macro buttons fixed width, left-aligned

**Status:** Decided
**Date:** June 2026

**Decision:** Macro buttons are fixed width (100px) with 4px
spacing, left-aligned with a stretch at the end. Button positions
are consistent regardless of window width.

**Reasoning:** Stretching buttons to fill window width forces
operators to hunt for button positions when the window is resized.
Fixed positions build muscle memory — operators know where CQ POTA
always is without looking.

## ADR-083 — Multi-line message support via newline preservation

**Status:** Decided
**Date:** June 2026

**Decision:** HAVEN-FSK transmits and displays multi-line messages.
The TX QTextEdit allows newlines (Enter key). The frame encoder
transmits \n (0x0A) as a valid byte — it is within the 7-bit ASCII
range and handled correctly by LDPC encoding. RxDisplay converts
\n bytes in decoded text to HTML <br> tags for display. TX echo
in amber also renders \n as line breaks.

**Reasoning:** Operators requested ability to send formatted
multi-line messages — station info exchanges, structured field
tag groups, and readback of longer messages all benefit from
line breaks. The HAVEN-FSK frame format supports arbitrary byte
content within the payload so no protocol change is needed.

## ADR-084 — TX gain via GainedAudioDevice for real-time control

**Status:** Decided
**Date:** June 2026

**Decision:** TX level control uses GainedAudioDevice — a custom
QIODevice that wraps the WAV buffer and applies an atomic float
gain multiplier to int16 PCM samples as QMediaPlayer reads them.

The WAV header (44 bytes) passes through unchanged. PCM data
has gain applied per-sample on each read cycle. The gain is
std::atomic<float> — safe to update from the main thread while
QMediaPlayer reads from its audio rendering thread. Changes take
effect within one read cycle (~100-200ms).

QAudioOutput::setVolume() is fixed at 1.0. QMediaPlayer receives
GainedAudioDevice via setSourceDevice() instead of a plain QBuffer.

**Real-time update path:**
LevelPanel::txFaderChanged → MainWindow lambda →
AudioEngine::setTxGain() → GainedAudioDevice::setGain() →
atomic store → next PCM read applies new gain

**Reasoning:** QAudioOutput::setVolume() is unreliable on the
Windows FFmpeg backend. Pre-baking gain into PCM prevents
mid-transmission adjustment. GainedAudioDevice applies gain at
read time — the audio changes within ~200ms of a fader move,
giving genuine real-time level control during live transmission.

## ADR-085 — Modulator output normalized to 0dBFS

**Status:** Decided
**Date:** June 2026

**Decision:** TX_AMPLITUDE constant changed from 0.25 to 1.0 in
Constants.h. CPFSK is constant-envelope — peak is always ±1.0,
normalization to 0.25 was unnecessary attenuation baking -12dBFS
into the signal before GainedAudioDevice. buildWav() now encodes
at full scale (×32767). GainedAudioDevice is the sole gain control
in the TX chain. TX fader default changed from -18dBu to -6dBu.

**Reasoning:** With TX_AMPLITUDE=0.25, Thetis VAC1 TX Gain
required +40dB compensation just to produce 2W from a 5W radio.
This inverted correct gain structure — attenuation before the
fader rather than after. With 0dBFS Modulator output and Thetis
VAC1 TX Gain at 0dB, the HAVEN fader has full meaningful control
range.

## ADR-087 — PTTManager simplified, DCD advisory only

**Status:** Decided
**Date:** June 2026

**Decision:** PTTManager reduced to PTT sequencing only.
requestTX() keys PTT immediately with no backoff, no DCD check,
no operating mode logic. Only the 120-second watchdog remains as
a safety feature. DCD energy detector remains active but only
updates the visual indicator — never blocks or delays TX.

**Reasoning:** DCD-based TX blocking caused PTT to be denied while
audio played anyway — radio never keyed but audio was sent to VAC.
This is worse than no collision avoidance at all. For a
conversational HF mode used by licensed operators, operating
convention (listen before transmitting) is the correct collision
avoidance mechanism. Automatic blocking conflicts with weak signal
operation and creates unpredictable TX behavior.

## ADR-088 — Bottom section: meters + 18-button macro grid + TX textarea

**Status:** Decided
**Date:** June 2026

**Decision:** Bottom section of main window redesigned as a single
resizable container added to the main QSplitter as its 4th widget.
Left side: LevelPanel (fixed size, unchanged). Right side: MacroPanel
(6×3 grid, 18 buttons) above TX textarea. MacroPanel expanded from
16 buttons (2 banks × 8) to 18 buttons (single flat grid, no bank
switching). Buttons are 100px fixed width, left-aligned, never
stretch to fill window. The entire bottom container resizes with the
splitter. QSettings keys migrated from "macros/bank0/N/label" to
"macros/N/label".

**Reasoning:** Previous layout had unused space and the macro panel
was outside the splitter. Moving macros into the bottom container
eliminates wasted space and gives the operator all controls in one
resizable area. Single flat grid of 18 buttons removes the bank
A/B switching overhead — all macros always visible. Fixed button
widths prevent layout shift when window resizes.

## ADR-089 — DCD removed from status bar display

**Status:** Decided
**Date:** June 2026

**Decision:** DCD status indicator and RX state text (Idle/Searching/
Receiving) removed from status bar. DCD energy detector continues
running internally to gate the RX state machine (Idle→Searching→
Receiving transitions). Visual indicator removed because: operator
can see signal presence on waterfall; RX window shows when decoding
occurs; "DCD: ON" label implied active TX blocking which no longer
exists; cleaner status bar. Status bar now shows: frequency, rig
connection status, RX level bar, and operational status text.

## ADR-090 — Waterfall floor control and resolution improvement

**Status:** Decided
**Date:** June 2026

**Decision:** WaterfallWidget floor level adjustable via QSpinBox in
toolbar (-140 to 0 dBFS, default -60, step 5, persisted in QSettings).
dbToColor() uses m_floorDb / m_rangeDb (80 dB fixed range) instead of
compile-time DB_MIN/DB_MAX constants. FFT_SIZE increased from 2048 to
4096 with 50% overlap (HOP_SIZE=2048) — doubles frequency resolution
from 23.4 Hz/bin to 11.7 Hz/bin. computeFFT() drains all accumulated
hops per call (allocating one kiss_fftr_cfg), returning the most recent
frame. Linear interpolation between bins in addRow() already present.
All changes are display-only — decode path uses its own FFT.

**Reasoning:** Fixed floor at -60 dBFS made the waterfall too dim on
strong signals and too noisy on weak signal bands. Adjustable floor
lets the operator tune sensitivity to match band conditions. Larger FFT
improves ability to distinguish adjacent HAVEN-FSK signals and nearby
interference without changing scroll rate.

## ADR-091 — Waterfall FFT 4096 with 50% overlap and linear interpolation

**Status:** Decided
**Date:** June 2026

**Decision:** WaterfallWidget FFT_SIZE increased from 2048 to 4096 with
HOP_SIZE=2048 (50% overlap). One waterfall row generated per HOP_SIZE
samples — same scroll rate as before. Persistent kiss_fft_cfg allocated
once in constructor and freed in destructor (replaces per-call alloc/free).
Complex kiss_fft used with zero imaginary input. m_binWidth precomputed
as SAMPLE_RATE/FFT_SIZE. Linear interpolation between adjacent FFT bins
in addRow() uses m_binWidth for exact sub-bin positioning.

**Resolution improvement:**
- Before: 23.4 Hz/bin (2048 FFT), blocky pixel steps
- After:  11.7 Hz/bin (4096 FFT) + smooth interpolation

**CPU cost:** Negligible — KissFFT 4096-point at 48kHz is trivial on
any target hardware. Persistent cfg eliminates per-call alloc overhead.

**Reasoning:** 2048-point FFT produced 23.4Hz/bin width visible as
rectangular blocks on the display. Doubling FFT size halves bin width.
Linear interpolation smooths remaining steps. Both are display-only —
the demodulator uses its own independent FFT pipeline and is unaffected.

## ADR-092 — Macro behavioral tags <TX> and <clr>

**Status:** Decided
**Date:** June 2026

**Decision:** Two behavioral tags added to macro system.
<TX> causes auto-transmit after macro text is inserted (50ms
delay to allow UI update). <clr> clears TX input before
inserting macro text. Both tags are stripped before transmission.
Tags are case-insensitive and may appear anywhere in macro text.
MacroPanel::macroTriggered signal updated to carry clearFirst
and autoTx bool parameters. Default macros updated to use
<clr> and <TX>. macroTriggered connection in MainWindow
converted to lambda for inline handling.

**Reasoning:** Flat grid MacroPanel lost the auto-TX feature
from the previous bank-based implementation. Tag-based approach
is more flexible — operator controls which macros auto-transmit
by including or omitting <TX>. <clr> prevents accidental
concatenation of macro text onto existing content. Backwards
compatible — macros without tags append text and wait for
operator input.

## ADR-093 — FrequencyControl digit scroll tuning

**Status:** Decided
**Date:** June 2026

**Decision:** FrequencyControl QLineEdit supports mouse wheel digit
tuning. Hovering over a digit highlights it via setSelection() — dark
blue background, amber text, no cursor change or tooltip. Scrolling
applies the digit's step and zeros all lower digits via integer
division: newHz = (newHz / step) * step. 10MHz digit (index 0)
disabled to prevent coarse accidental changes. Frequency clamped to
1-30MHz. Event filter on m_freqEdit intercepts wheel and mouse events.
digitAtX() accounts for right-aligned text using contentsRect().

**Rounding examples:**
- Scroll 1kHz at 14.087.432: result 14.088.000 or 14.086.000
- Scroll 100Hz at 14.087.432: result 14.087.500 or 14.087.300
- Scroll 10Hz at 14.087.432: result 14.087.440 or 14.087.420

**Reasoning:** Digit scroll tuning is standard UX in SDR applications.
Operators familiar with SDR software expect this behavior. Rounding
mirrors mechanical VFO encoder behavior on traditional radios.

## ADR-094 — FrequencyControl replaced with custom DigitDisplay widget

**Status:** Decided
**Date:** June 2026

**Decision:** QLineEdit frequency display replaced with custom DigitDisplay
QWidget that paints its own text and digit highlight box using QPainter.
System cursor hidden (Qt::BlankCursor) when hovering a scrollable digit.
A painted rectangle highlights the digit under the cursor. Mouse wheel
applies step for that digit with automatic rounding of lower digits to
zero via integer division: newHz = (newHz / step) * step. 10MHz digit
(index 0) disabled. Frequency clamped to 1-30MHz.

**Why custom widget instead of QLineEdit + event filter:**
QLineEdit places its own text cursor on click, causing visual confusion
with digit-scroll tuning. QLineEdit's internal left margin and padding
are platform-dependent, making pixel-accurate hit testing unreliable.
The custom widget gives exact control over cursor appearance, highlight
box position, character metrics, and event handling.

**Digit layout (format "14.074000"):**
Index 0=10MHz(disabled) 1=1MHz 2=dot 3=100kHz 4=10kHz 5=1kHz
6=dot 7=100Hz 8=10Hz 9=1Hz

**Rounding:** newHz = (newHz/step)*step — zeroes all digits below the
scrolled digit in one integer operation.

## ADR-095 — Preamble detect() returns match offset

**Status:** Decided
**Date:** June 2026

**Decision:** `Preamble::detect()` now returns the offset within the search
window where the preamble was found via output parameter `matchOffset`.
`DspPipeline` uses this offset to calculate `frameStart = matchOffset +
PREAMBLE_LENGTH` and pre-loads any symbols already past the preamble end
into `m_symbolAccum` before entering `Receiving` state. `PREAMBLE_THRESHOLD`
raised from 4.0 to 6.0 (score threshold 0.375 instead of 0.25).

**Reasoning:** `detect()` was finding the preamble at non-zero offsets within
the search window but discarding that offset. This caused `DspPipeline` to
lose all header symbols already in the search window past the preamble end,
resulting in frame misalignment and "Unknown protocol version" parse failures
(preamble tones 0/15 landing in header symbol slots, decoded as byte 0x0F
instead of expected header byte 0x11). The threshold increase (0.25 → 0.375)
reduces false detections on noise while remaining reachable for legitimate
preamble receptions.

## ADR-096 — Buffered RX architecture replaces real-time preamble detection

**Status:** Decided
**Date:** June 2026

**Decision:** DspPipeline RX path changed to buffered architecture. Raw
audio samples accumulate in `m_rxBuffer` while DCD is active. When DCD
drops (transmission ends), `processRxBuffer()` runs: demodulates the
entire buffer to soft symbols, searches for the preamble using a
sliding-window hard-decision correlation across all symbols, computes
accurate AFC from the preamble region using the centroid method,
re-demodulates the full buffer with AFC correction applied, then decodes
the frame from the correct alignment point. `RxState` simplified to
`Idle` / `Buffering` / `Decoding`. `Preamble::correlate()` made public
so `processRxBuffer()` can drive its own search loop.

**Latency:** ~50-100ms after end of transmission. Imperceptible in
conversational QSO flow.

**Memory:** ~720 KB for a typical 18-character message at 48 kHz.
~11 MB absolute maximum (60-second safety limit). Negligible on all
target hardware including Raspberry Pi 4.

**Reasoning:** Real-time preamble detection failed because AFC was not
locked during preamble arrival, causing wrong symbol decisions and low
correlation scores. The complete transmission was detected only near its
end when random noise happened to score above threshold. Buffered decode
eliminates all timing pressure — the decoder has the complete
transmission and all the time it needs. Preamble offset is found
retrospectively with exact sample alignment. AFC is computed accurately
over the full 16-symbol preamble from known-content audio.

**Trade-off:** Real-time (symbol-by-symbol) processing path removed
entirely. No partial decoding during reception. This is acceptable for
HF conversational use where transmissions are short (typically 2-5 s)
and the decode latency is measured in milliseconds, not seconds.

## ADR-097 — Demodulator::demodulateToSoft accepts sample offset; timing sweep in preamble search

**Status:** Decided
**Date:** June 2026

**Decision:** `Demodulator::demodulateToSoft(audio, sampleOffset=0)` gains an
optional `sampleOffset` parameter that skips that many samples before slicing
the audio into symbol-sized FFT blocks. `DspPipeline::tryFindPreamble()` uses
this to run an 8-step timing sweep — trying sample offsets 0, 192, 384, 576,
768, 960, 1152, 1344 (each 1/8 of a symbol period) — and selects the offset
that yields the highest preamble correlation score.

**Reasoning:** `demodulateToSoft` always starts slicing from sample 0 of the
buffer. If the preamble begins mid-block (i.e., the actual symbol boundary
does not align to a multiple of SAMPLES_PER_SYMBOL from the buffer start),
every FFT window straddles two symbols. With 8× zero-padding and a Hann
window, the energy from both symbols mixes in the FFT output — producing wrong
hard decisions even on a clean signal. The timing sweep finds the alignment
where symbol boundaries match the FFT grid. Cost: 8 demodulations per preamble
scan, which is negligible at HAVEN-FSK baud rates.

**Coarse pre-filter:** Before running the 8-step sweep, a single demodulation
at offset 0 runs a quick preamble correlator. If the best score is below 0.18
(barely above random chance for 16-tone symbols), the sweep is skipped. This
keeps the scan cheap when no signal is present.

## ADR-098 — Preamble-triggered RX decode; DCD advisory only

**Status:** Decided
**Date:** June 2026

**Decision:** `DspPipeline` RX decode path is no longer triggered by DCD
drop. DCD is retained solely as a UI signal-present indicator (drives the
waterfall lamp and SNR meter). The decode path is now preamble-triggered:

1. DCD-on → enter `Searching` state, begin accumulating audio.
2. Every 4 audio chunks (~170 ms): run coarse+fine preamble scan on buffer.
3. Preamble found (score ≥ 0.375) → enter `Collecting` state; measure and
   store AFC offset from preamble soft symbols.
4. `tryCompleteFrame()` called each new chunk: decodes the double header once
   8 symbols are available (gets `nBlocks`), then waits until the exact number
   of symbols needed for the complete frame are present, then decodes.
5. After decode (success or failure): `resetRx()` → `Idle`.

**Timeouts:** 8-second search timeout (after DCD drops, give up if no
preamble found). 20-second collect timeout (safety valve if frame never
completes).

**RxState enum** changed from `Idle / Buffering / Decoding` to
`Idle / Searching / Collecting`.

**Reasoning:** The DCD-triggered architecture had two fatal failure modes:
(a) DCD fires on noise before the signal arrives → leading buffer garbage
pushes preamble toward the end of the buffer where there are too few trailing
symbols for the frame. (b) DCD drops due to brief signal dip during reception
→ buffer is truncated, frame is incomplete. By decoupling accumulation end
from DCD state, the buffer continues growing after DCD drops until the
complete frame arrives or the timeout fires.

**Method added:** `Frame::frameSymsNeeded(nBlocks)` — public static helper
that returns the total post-preamble symbol count for a given `nBlocks`,
allowing `DspPipeline` to know exactly when to decode without coupling to
Frame's private layout constants.

## ADR-100 — AudioEngine verifies negotiated format on RX start

**Status:** Decided
**Date:** June 2026

**Decision:** After `QAudioSource::start()`, `AudioEngine::startRx()` reads
back the format Qt actually negotiated and logs it unconditionally. If the
sample rate or channel count differs from the requested format, a warning is
logged and `audioError()` is emitted so the UI can alert the operator.

**What each mismatch does:**
- Wrong sample rate (e.g., 44100 Hz): shifts all HAVEN tone bins by the
  ratio, causing every tone hypothesis to miss its window. No decode is
  possible.
- Wrong channel count (e.g., stereo instead of mono): `pcmToFloat()`
  interprets interleaved L,R int16 pairs as sequential mono samples, halving
  the apparent sample rate and destroying all tone bin mapping. Root-caused
  in a real debugging session where a VAC was set to 2 ch and the preamble
  scanner produced only noise-level scores.

**Why log even when correct:** VAC drivers and some USB audio devices silently
deliver a different format than they advertise. Logging the actual negotiated
format unconditionally means a mismatch is immediately visible in the debug
log without needing to add print statements.

**What is NOT done:** The pipeline does not refuse to start on a format
mismatch. The operator receives a warning and may be able to correct the VAC
configuration without restarting. Refusing to start would require a restart
cycle and loses the ability to log the mismatch in context.

## ADR-099 — Frame header sent twice for HF fade resilience

**Status:** Decided
**Date:** June 2026

**Decision:** The 2-byte frame header (version/flags + nBlocks) is transmitted
twice — 8 symbols total instead of 4 — as consecutive, continuous-phase
symbols immediately after the preamble. The frame structure is now:

```
Preamble (16) | Header copy 1 (4) | Header copy 2 (4) | CRC (4) | Payload (nBlocks×48)
```

`PAYLOAD_START` updated from 8 to 12. `Frame::parse()` decodes both copies
and logs a warning if they differ; copy 1 is used in all cases (CRC catches
any remaining error).

**TX:** `Frame::assemble()` calls `mod.modulate(hdrVec)` twice; the Modulator
continuous-phase accumulator ensures no phase discontinuity between the two
copies or at the header→CRC boundary.

**RX:** `tryCompleteFrame()` peeks at both header copies before committing to
`nBlocks`. If both agree, `nBlocks` is reliable. If they differ, copy 1 is
used and the CRC provides the final integrity check.

**Cost:** 4 additional symbols = 128 ms at 31.25 baud. Invisible in
conversational HF use.

**Reasoning:** The header is the only field that tells the decoder how long
the frame is. If `nBlocks` is corrupted by a single HF fade (which can
span 100–500 ms = 3–15 symbols), the decoder attempts to read the wrong
number of payload symbols and the CRC catches it only after wasted effort.
At 31.25 baud, a 4-symbol header sits entirely within one typical fade
window. Sending it twice requires independent fades to corrupt both copies,
which is highly improbable given typical HF propagation. This change was
made together with ADR-098 since both require `PAYLOAD_START = 12`.

---

## ADR-101 — Project license corrected to GPLv3; ADR-004 rationale superseded

**Status:** Decided
**Date:** July 2026

**Decision:** HAVEN-FSK is licensed under GPLv3. `LICENSE` (repo root)
carries the full canonical GPLv3 text. `THIRD_PARTY_LICENSES.md` documents
third-party license obligations.

**Reasoning:** The project was intended to be GPLv3 from inception. ADR-004's
justification for choosing KissFFT — that FFTW's GPL variant was "incompatible
with potential commercial use" — reflected a drift from that original intent,
not a real constraint: HAVEN-FSK has no commercial-distribution goal that GPL
would block, and GPLv3's copyleft is in fact aligned with the goal of
preventing the mode from being locked into a closed-source commercial fork
(GPLv3 does not prohibit commercial use of the software itself; it prohibits
distributing it, or derivatives of it, without also conveying the same rights
and source access to recipients).

Concrete evidence the project was already implicitly GPL-obligated before this
correction: `CMakeLists.txt` links `Qt6::Charts`
(`target_link_libraries(HavenFSK PRIVATE ... Qt6::Charts ...)`). In Qt's
open-source distribution, the Charts module is available only under GPLv3 —
there is no LGPL option for it, unlike most other Qt6 modules used here. Any
build linking `Qt6::Charts` under the open-source Qt offering is therefore
already bound by GPLv3 terms for that dependency, regardless of what license
the rest of the project claimed.

**Effect on ADR-004:** ADR-004's actual *decision* — use KissFFT rather than
FFTW — is unaffected and remains correct: KissFFT's BSD-3-Clause license is
fully compatible with a GPLv3 project (permissive licenses may be incorporated
into GPL works; the restriction only runs the other direction). Only the
GPL-rejection *reasoning* given for that decision was incorrect and is
superseded by this entry.

**Practical effect going forward:** GPLv3 (or GPLv3-compatible, e.g.
permissive) source may now be vendored or adapted into the project — for
example, consulting fldigi (GPLv3, https://github.com/w1hkj/fldigi) for PSK31
and MFSK decoding techniques — following the existing vendoring pattern
established in ADR-005 (isolate under `src/third_party/` or an appropriately
namespaced subdirectory, preserve upstream copyright notices, document the
license and provenance in `THIRD_PARTY_LICENSES.md`).

---

## ADR-102 — Payload interleaving and 3x header majority vote (protocol v2)

**Status:** Decided
**Date:** July 2026

**Decision:** `Frame::PROTOCOL_VERSION` bumped from `0x01` to `0x02`,
bundling two wire-format changes:

1. **Payload interleaving.** A row/column block interleaver
   (`Interleaver::interleave`/`deinterleave`, `src/dsp/Interleaver.h`)
   spans all `nBlocks` LDPC blocks of a message: bits are written
   row-wise (one row per block, natural order) and read out
   column-wise. `Frame::assemble()` interleaves `FEC::encodeMessage()`'s
   BPSK output before packing to bytes/symbols; `Frame::parse()`
   deinterleaves the LLR array (post-`FEC::softToLLR()`) before
   `FEC::decodeMessage()`. No-op when `nBlocks <= 1`.
2. **3x header with bit-level majority vote.** The 2-byte header is now
   sent 3 times (`HEADER_COPIES = 3`, was 2 — see ADR-099) instead of the
   previous "send twice, always prefer copy 1" behavior. `Frame::parse()`
   decodes all 3 copies and takes, independently for each bit position,
   whichever value at least 2 of 3 copies agree on
   (`Frame::majorityVoteHeader()`). `PAYLOAD_START` moves from 12 to 16.
   `MfskModem::tryCompleteFrame()`'s early header decode (used for buffer
   sizing before the full frame arrives — a deliberate duplicate of
   `Frame::parse()`'s header logic per ADR-098) was updated to match.

**Reasoning:** Both address the same root cause — HF fading produces
*burst* errors (a fade lasting a fraction of a second corrupts several
consecutive transmitted symbols), and LDPC belief propagation, like most
block error-correcting codes, handles randomly-distributed bit errors far
better than a burst concentrated in one region of a block. Interleaving
spreads a burst across all blocks of a message so each individual block
sees only scattered single-bit errors after deinterleaving. The header
carries `nBlocks`, the one field that tells the receiver how long the
frame is — if it's corrupted, the decoder doesn't know how many payload
symbols to collect, and this is caught only after wasted collection
time. Two copies can't do genuine majority voting (a tie with no
tiebreak); three copies with independent per-bit voting means a fade
would need to corrupt the *same* bit position in at least 2 of 3
temporally-separated copies to still produce a wrong header, which is
substantially less likely than corrupting one full copy.

**Why bundle into one version bump:** both changes break wire
compatibility with existing (pre-v2) HAVEN-FSK stations. Landing them
as two separate breaking releases would mean two incompatible-version
windows instead of one; `Frame::parseHeader()`'s existing version check
(`version == PROTOCOL_VERSION`) already causes a v1-vs-v2 mismatch to be
rejected cleanly (`"Unknown protocol version"`) rather than silently
misdecoded, so no new gating logic was needed — only the version
constant itself changed meaning.

**Verification:** `FrameSelfTest.h`'s existing assemble/parse round-trip
test exercises the full v2 path (3x header + interleave/deinterleave)
automatically; confirmed passing via the app's debug log showing
`Frame::parse hdr[0]=0x21 ... expect hdr[0]=0x21` during self-test, with
the app proceeding normally into live audio RX afterward (a self-test
failure would `return 1` before reaching that point — see `main.cpp`).
**Not yet verified:** an actual over-the-air round trip on a real or
simulated fading HF channel to confirm the interleaver measurably
improves decode success versus the v1 baseline — the self-test only
proves the encode/decode path is bit-exact on a clean channel.

---

## ADR-103 — HamlibClient links libhamlib directly (extends ADR-017)

**Status:** Decided
**Date:** July 2026

**Decision:** `HamlibClient` links the Hamlib C library (`<hamlib/rig.h>`)
directly into HavenFSK's own process for native CAT control over a
USB/serial port (e.g. a Kenwood TS-590SG), rather than requiring a
separately-launched `rigctld` process. Gated by a new `HAVEN_ENABLE_HAMLIB`
CMake option (default `ON`) that **never hard-breaks the build** if the
Hamlib SDK isn't found — `HamlibClient` falls back to stub behavior
(matching its pre-existing unimplemented-stub state) and the rest of the
app is unaffected. Windows locates Hamlib via a `HAMLIB_DIR`-pointed
externally-installed SDK (mirroring how `QT_DIR` already works in
`build.bat`); Linux/Raspberry Pi via `pkg-config` against the
distribution's `libhamlib-dev` package. Linked dynamically, not
statically (see `THIRD_PARTY_LICENSES.md` for the LGPL v2.1 reasoning).

**This explicitly extends rather than supersedes ADR-017.** ADR-017's
reasoning for TCP-to-external-`rigctld` — "No Hamlib library dependency
in the HAVEN-FSK build" — was itself the tradeoff being reconsidered
here, not a mistake to correct. `RigctldClient` and `TCIClient` remain
fully intact and are still the right choice for remote/networked rig
control, or a radio already shared with other software (WSJT-X, fldigi,
JS8Call) via one running `rigctld` instance. `HamlibClient` is for the
locally-attached-serial-radio case those two don't cover well: it
matches this project's own "single executable deployment" rationale
(ADR-001) for leaving Python, and avoids a second external process being
a runtime dependency on low-budget/older machines and Raspberry Pi used
for field operation without internet connectivity — everything needed
must already be on the machine before heading out, and a second binary
is one more thing that can go missing.

**Rig list is never hand-maintained.** `HamlibClient::availableRigs()`
calls Hamlib's own `rig_load_all_backends()` + `rig_list_foreach()` at
runtime to enumerate every compiled-in rig model (confirmed present via
Hamlib's actual `include/hamlib/rig.h`, not assumed) — new radios appear
automatically whenever the linked Hamlib version is upgraded, with zero
HAVEN-FSK code changes.

**Considered and rejected: auto-managing `rigctld` as a background
subprocess** (spawn/monitor it internally, still connect via the
existing `RigctldClient` TCP code). Runtime CPU/RAM overhead between the
two approaches is negligible either way at HAVEN-FSK's rig-poll rate
(~0.5–1 Hz) — the DSP/audio/UI stack dominates resource use on a Pi
regardless. The deciding factor was deployment robustness for offline
field use, not performance: a subprocess is a second moving part
(external binary, port, path-finding) more likely to break quietly on
constrained hardware than to save meaningful cycles.

**Not vendored into git** (contrast KissFFT, ADR-005). KissFFT's
vendoring was justified by its small size (~1200 lines total); Hamlib's
rig-backend source tree is far larger, so that justification doesn't
transfer. Treated as an externally-installed dependency instead, the
same way Qt6 itself already is.

**Verification:** actually build-tested against the real Hamlib 4.7.2
w64 SDK (downloaded, inspected, and extracted to `C:\HamRadio\` —
`build.bat`'s `HAMLIB_DIR` default), not just compile-tested against the
stub fallback path. This caught four real bugs before/as they reached a
user — the app genuinely failed to launch twice in the field before all
four were found — all now fixed:

1. `HamlibClient.h`'s forward declaration guessed `struct rig` for
   Hamlib's opaque `RIG` type; the actual tag is `struct s_rig`
   (`typedef struct s_rig RIG;`), confirmed in `include/hamlib/rig.h`.
   Compile error until fixed.
2. `libhamlib-4.dll` depends on `libusb-1.0.dll` at load time — the
   `POST_BUILD` step only copied the Hamlib DLL itself; the app failed
   to start ("cannot open shared object file") until `libusb-1.0.dll`
   was also copied next to `HavenFSK.exe`.
3. **This entry originally claimed `libgcc_s_seh-1.dll`/`libwinpthread-1.dll`
   didn't need copying because Qt's `windeployqt`-bundled same-named
   DLLs would satisfy them — that assumption was wrong and caused a real
   user-visible launch failure** ("The code execution cannot proceed
   because (null).DLL was not found"). Qt's bundled copies (May 2023,
   ~53-109KB) and Hamlib's own (~325-955KB) are completely different
   MinGW-w64 builds, not interchangeable — confirmed via file
   size/hash comparison. Fix: copy Hamlib's own matching
   `libwinpthread-1.dll`/`libgcc_s_seh-1.dll` from its `bin/` alongside
   `libhamlib-4.dll`/`libusb-1.0.dll` (`cmake/FindHamlib.cmake`'s
   `HAMLIB_LIBWINPTHREAD_DLL`/`HAMLIB_LIBGCC_DLL`, `CMakeLists.txt`'s
   `foreach` over all four). General lesson: use the runtime a
   third-party binary actually shipped and was tested with, not
   whichever same-named DLL happens to already be in the output
   directory.
4. Fixing #3 didn't resolve the "(null).DLL" error — a second, unrelated
   bug was still present: `cmake/FindHamlib.cmake`'s `find_library()`
   picked `lib/gcc/libhamlib-4.lib` over `lib/gcc/libhamlib.dll.a`.
   Both exist in that directory, but only `.dll.a` is the proper
   GNU-ld-native import library — `.lib` is MSVC-style format despite
   living in the "gcc" folder. Linking against it "succeeded" with no
   error but produced a corrupted PE import table in `HavenFSK.exe`
   itself (a literal empty `DLL Name: (null)` entry, confirmed via
   `objdump -p HavenFSK.exe` — Hamlib wasn't even listed as a
   dependency). Fixed by switching to `find_file()` targeting
   `libhamlib.dll.a` explicitly rather than relying on
   `find_library()`'s NAMES-based suffix search order.

After all four fixes: clean build, all four DLLs copied and confirmed
via hash match, `objdump -p HavenFSK.exe` shows a clean import table
with `libhamlib-4.dll` properly listed, app launches and stays running
(confirmed via both automated polling and the user's own direct test).
Rig enumeration (`HamlibClient::availableRigs()`) and actual CAT control
against a live TS-590SG have not yet been exercised — that's the next
verification step.

---

## ADR-104 — PSK31 real-signal fixes: streaming display, squelch, TX preamble

**Status:** Decided
**Date:** July 2026

**Decision:** Four fixes, all found via real over-the-air PSK31 use
(live band signal, and interop testing against fldigi) rather than
self-test/loopback alone — the first time this codebase's PSK31 mode
was exercised against anything other than its own loopback.

**1. RX display was unreadable — one row per character.** `Psk31Modem`
emits one `ModemRxEvent` per decoded character (PSK31 is a continuous
stream, unlike MFSK's discrete framed messages), but `DspPipeline`/
`RxDisplay` treated every event as a complete framed message worth its
own timestamped row with `[CRC]`/`[NC]` badges — badges that don't even
apply to PSK31 (no CRC or FEC at all). Fixed by adding
`ModemRxEvent::isFramedMessage` (default `true`, so MFSK is untouched;
`Psk31Modem` sets it `false`), routing non-framed events through a new
`DspPipeline::textCharacterReceived()` signal instead of
`messageReceived()`, and a new `RxDisplay::appendStreamingText()` that
appends in place on the current line (one timestamp per stream, ended on
carrier drop/mode change/a real framed message arriving mid-stream via
`endStreamingLine()`).

**1b. Spaces and newlines were being silently dropped** even after the
above fix — `appendStreamingText()` used `QTextCursor::insertHtml()` per
character; HTML parsing collapses whitespace runs and treats a bare
`\n` as insignificant rather than a line break. Switched to
`insertText()` (plain-text cursor insertion, preserves both exactly and
creates a new block on `\n`); also normalize `\r` to `\n` since some
PSK31 stations send CR-only line endings.

**2. Squelch: needed, first attempt broke real RX entirely.**
`Psk31Demodulator` already computed a per-bit Costas-loop lock-quality
value (0.0-1.0) that nothing used. Added two-stage squelch in
`Psk31Modem::processAudioChunk()`: (a) DCD gate — no carrier present,
don't even run the demodulator, and reset decode state on the falling
edge so a real signal starts clean; (b) per-character average
lock-quality threshold. **First attempt used a fixed `constexpr 0.7`
threshold, validated only against a noiseless loopback test — it
silently suppressed all real over-the-air decoding** (confirmed: fldigi
decoded the same live signal fine, HAVEN's RX showed nothing). Real
signals have frequency drift/phase noise/timing jitter that legitimately
lowers lock quality even on correctly-decoded characters more than a
perfect loopback reveals. Fixed by making the threshold a runtime value
(`IModem::setSquelchThreshold()`/`squelchThreshold()`, default `0.0` =
off) exposed as a "Squelch:" spinbox in the status bar, persisted via
`QSettings`, and re-applied after a mode switch (which constructs a
fresh `IModem` instance that would otherwise silently reset it to
default). DCD gating remains the primary noise defense; lock-quality is
an operator-tuned secondary refinement, off by default until a working
baseline is confirmed.

**3. Missing opening characters — no TX preamble.** Interop testing
against fldigi (HAVEN TX -> fldigi RX) consistently dropped the first
few characters of every transmission. Confirmed by fetching and reading
fldigi's actual `src/psk/psk.cxx` (not assumed): its `tx_init()` sets
`preamble = dcdbits`, and for `MODE_PSK31` specifically `dcdbits = 32`
— i.e. fldigi sends 32 symbols of continuous phase-reversal (`bit=0`
repeated) before any real data, scaling to 64/128 for PSK63/125. This is
*not* unmodulated/silent carrier — it's active phase-reversal modulation,
which is what a receiving Costas loop/AGC/timing-recovery actually needs
to lock onto. HAVEN's TX had no preamble at all. Fixed by prepending
`psk31PreambleSymbols(baud)` `false` bits (matching fldigi's table
exactly) before the real Varicode-encoded data in
`Psk31Modem::modulateText()`. No RX-side change needed: the existing
Varicode decoder already treats runs of phase-reversal bits as harmless
idle (a `"00"` terminator with an empty accumulated codeword returns
`nullopt`), so the preamble is silently absorbed rather than producing
garbage characters.

**3b. Related latent bug found while implementing the above, fixed
alongside it:** `Psk31Modulator`'s differential BPSK reference point
(`m_prevI`/`m_prevQ`) persisted across separate `modulateText()` calls
rather than resetting per transmission. Differential BPSK only ever sits
at exactly `(1,0)` or `(-1,0)` (each bit multiplies by `+1` or `-1`), so
a second message sent later in the same session had a 50/50 chance of
starting from the wrong polarity relative to what a fresh receiver
decode always assumes — which, by the math of differential decoding,
only corrupts that transmission's *first* bit (subsequent bits are
self-correcting since the polarity flip cancels between consecutive
symbol comparisons), but that first bit sits inside the new preamble
fix's absorption window anyway, so this was effectively already masked
by fix #3 — fixed explicitly regardless, for correctness clarity rather
than relying on that interaction: `modulateBits()` now resets
`m_prevI=1.0, m_prevQ=0.0` at the start of every call (carrier phase is
*not* reset — continuous-phase discipline is unrelated and still
applies).

**Verification:** all four fixes tested via standalone Qt-free
compiles against the real `Psk31Modem` code path (not just
`Varicode`/`Psk31Modulator`/`Psk31Demodulator` in isolation, which
wouldn't exercise squelch or preamble at all): clean-signal loopback
still decodes perfectly; DCD-gated squelch reduced 5 seconds of loud
synthetic white noise from continuous garbage to 1 false character;
five consecutive transmissions from the same modem instance (including
a single-character message, the case most likely to expose the
differential-polarity bug) all round-trip correctly. **Not yet
re-verified against fldigi after these fixes** — the original interop
test that found the missing-preamble bug should be re-run to confirm
fldigi now decodes HAVEN's TX cleanly from the first character.

---

## ADR-105 — MFSK preamble sync rewritten as a continuous per-sample sliding DFT

**Status:** Decided
**Date:** July 2026

**Decision:** `MfskModem::tryFindPreamble()` — a discrete-offset search that
recomputed a block FFT (`Demodulator::demodulateToSoft`) at a small number of
candidate (frequency, timing) combinations — is replaced by a new
`PreambleSync` (`src/dsp/PreambleSync.{h,cpp}`) built on a new `SlidingDft`
(`src/dsp/SlidingDft.{h,cpp}`). `SlidingDft` maintains a bank of recursive
resonator bins (one complex accumulator per tracked bin, updated by a single
multiply-add every sample — the well-known "sliding DFT" recursion, see
Jacobsen & Lyons) covering the 16 HAVEN tone bins plus an AFC search margin.
Because bin energies refresh after *every* sample rather than only at a
handful of discrete block alignments, `PreambleSync::pushSample()` checks
every possible symbol-timing alignment continuously, at O(1) amortized cost
per sample, with no gap between tested alignments.

**Root cause being fixed:** the old coarse/fine split tried only 2 timing
offsets (`0`, `SAMPLES_PER_SYMBOL/2`) across 17 AFC hypotheses before a
`COARSE_PREAMBLE_THRESHOLD` gate decided whether to even attempt the full
16-step fine sweep. With no guard-bin tolerance for timing (unlike frequency),
a true symbol boundary landing near the worst-case ~25%-of-symbol midpoint
between those two coarse candidates could fail to clear even the generous
0.10 coarse threshold — meaning the fine sweep, which *would* have found the
real signal, never ran at all. This was a pure code/architecture bug,
reproducible on a clean, noiseless signal, independent of RF conditions —
confirmed by the operator's own observation that HAVEN's tones were clearly
visible on its own waterfall while decode still failed.

**Why this technique, not just a wider coarse grid:** investigated by reading
fldigi's actual MFSK/Olivia receiver source (`src/mfsk/mfsk.cxx`,
`src/filters/filters.cxx`'s `sfft` class — a GPLv3 codebase, same license as
HAVEN post-ADR-101) rather than assuming from general DSP knowledge. fldigi
sidesteps the discrete-offset-search problem entirely: `mfsk::rx_process()`
calls a per-sample sliding FFT and stores each result in a circular history
(`pipe[2*symlen]`); `mfsk::synchronize()` then scans that history for the
peak. This HAVEN implementation is written independently (own bin layout,
own preamble-correlation logic, own C++ style) — the fldigi source was the
reference point for the *technique*, not a code port.

**Simplification this enabled:** because `windowLen == SAMPLES_PER_SYMBOL`
(1536 samples) puts one sliding-DFT bin exactly on every HAVEN tone with
*zero* zero-padding needed (bin width = `SAMPLE_RATE/SAMPLES_PER_SYMBOL` =
31.25 Hz = `SYMBOL_RATE`, exactly), and because a lock reports the exact
sample index of the preamble's first symbol, `MfskModem::onPreambleLocked()`
(replacing `tryFindPreamble()`) no longer needs any post-lock timing search —
`m_timingOffset`/`m_preambleSymOff` are always `0` by construction, since
`m_rxBuffer` is sliced to start exactly there. The old 16-step fine timing
sweep and 17-hypothesis AFC grid are both gone; the block `Demodulator` is
now only used for the post-lock frequency-residual refinement
(`measureToneOffset`, unchanged) and for actual data-symbol decode in
`tryCompleteFrame()` (unchanged — once timing is known exactly, block FFT at
the single correct offset is the right tool).

**Peak-picking, not first-threshold-crossing:** an early implementation
fired the lock on the very first sample where the correlation score crossed
`SCORE_THRESHOLD` (0.45). Verified via a standalone diagnostic
(`diag_sync.cpp`, compiled directly against the Qt-free `SlidingDft`/
`PreambleSync`/`Preamble` sources) that the score curve, while trending
sharply upward toward the true alignment, is not perfectly monotonic
sample-to-sample — so this fired early on a mediocre ~0.45–0.66 score
instead of riding the climb to the true peak (which the same diagnostic
confirmed reaches exactly `1.0000` at the true preamble-start sample for a
clean signal). Fixed by tracking the best point across the entire
above-threshold run and only finalizing once the run ends (score drops back
below threshold, or a `MAX_RUN_SAMPLES` safety cap is hit) — the same
"scan for the true peak, don't jump at the first candidate" principle
fldigi's `synchronize()` uses.

**Continuous across RX state, not just Idle:** `PreambleSync::pushSample()`
is fed every sample regardless of `MfskModem`'s RX state (Idle or
Collecting) — lock results are only *acted on* while Idle. This avoids
reintroducing a version of the dead-zone bug fixed by the pre-refactor
`resetRx()` change (see prior RX-pipeline-overhaul commit): pausing the feed
during Collecting and resuming after would leave `PreambleSync`'s internal
ring buffer discontinuous (stitching pre-collection and post-collection
audio together as if adjacent), degrading detection for a window after every
decode. Feeding continuously costs nothing extra (the per-sample update is
already O(1)) and keeps history genuinely continuous.

**Verification:** new `runMfskLoopbackSelfTest()`
(`src/dsp/MfskLoopbackSelfTest.h`, wired into the `QT_DEBUG` self-test chain
in `main.cpp`) feeds `MfskModem::modulateText()`'s output through a second
`MfskModem` instance's `processAudioChunk()` in real `AUDIO_CHUNK_SAMPLES`
chunks — the same 2048-sample chunking `AudioEngine::onRxDataAvailable()`
uses — rather than calling `Frame::assemble()`/`parse()` directly the way
`FrameSelfTest.h` does (which bypasses `PreambleSync` and the chunked state
machine entirely; this gap is exactly the class of bug that also bit the
PSK31 squelch/preamble work in ADR-104's session, so closing it here was
deliberate). Confirmed passing: full round trip through the live chunked
pipeline decodes `"CQ POTA DE WD9N K-1234 K"` with correct text, CRC OK, and
FEC converged. **Not yet verified against a real over-the-air or live-VAC
signal** — this closes the specific reproducible-in-software bug (confirmed
via the standalone diagnostic reaching a perfect `1.0000` score at the exact
correct sample on a clean synthetic signal), but real-world SNR/frequency
drift/timing-drift behavior of the new continuous sync has not yet been
exercised against actual radio hardware.

---

## ADR-106 — Bounded connect retries + off-UI-thread Hamlib connect

**Status:** Decided
**Date:** July 2026

**Decision:** Two related fixes to `RadioInterface`'s connect/reconnect
handling, reported together from a real field failure (a misconfigured COM
port on a laptop running the TS-590SG/Hamlib setup — separate from the
Hermes/TCI development machine):

**1. Bounded retries for a connection that has never succeeded.**
`HamlibClient`, `RigctldClient`, and `TCIClient` all previously retried a
failed connection forever with exponential (or, for TCIClient, fixed-
interval) backoff, capped at a 30s delay but with no attempt limit — a
permanently wrong COM port or host/port setting retried in an endless loop
with no way for the operator to know it had given up, since the same
`rigError()` message just got overwritten every cycle. A new
`bool m_everConnected` flag (per client) distinguishes this from a
connection that *was* working and later dropped: only the "never
succeeded" case is bounded, via `MAX_INITIAL_CONNECT_ATTEMPTS = 5` (~31s of
backoff for Hamlib/rigctld, 5×10s=50s for TCI's fixed interval) before
emitting a new `RadioInterface::connectFailed(QString)` signal and stopping.
A connection that was genuinely live and dropped (radio power-cycled, USB
unplugged, rigctld restarted) keeps retrying indefinitely as before — that
case is a real, worth-recovering-from outage, not a configuration error.

**2. HamlibClient's connect attempts moved off the UI thread.** This is the
more serious half of the bug: `rig_init()`/`rig_open()` are blocking calls
(no async equivalent in Hamlib's API, per ADR-103), and `MainWindow`'s
constructor calls `startRadio()` *before* the window is shown
(`MainWindow.cpp`, end of constructor). On a wrong/nonexistent COM port,
the first blocking `rig_open()` call — and, previously, every subsequent
retry, since `onReconnectTimer()` called `connect()` synchronously too —
could freeze application startup itself, not just "retry in the
background" as the equivalent bug does for `RigctldClient`/`TCIClient`
(both already async via `QTcpSocket`/`QWebSocket`, so bounded retries alone
fully address the equivalent issue there). Fixed by dispatching the actual
`rig_init`/`rig_open` work to `QThreadPool::globalInstance()`, delivering
the result back to the main thread via
`QMetaObject::invokeMethod(this, lambda, Qt::QueuedConnection)` — the
context-object overload, which Qt guarantees silently drops the call
rather than touching a destroyed object, so this is safe even if
`disconnect()`/`~HamlibClient()` runs while an attempt is in flight. A
`std::shared_ptr<std::atomic<bool>>` cancellation flag additionally lets a
late-arriving *successful* `rig_open()` clean itself up on the worker
thread (rather than leaking the `RIG*` or, worse, trying to hand it to a
gone `this`) if the attempt was cancelled before it finished. Only the
connect/reconnect path was moved off-thread — `setPTT`/`getFrequency`/poll
calls remain on the UI thread, since those are fast and bounded on an
already-open connection; the "wrong port entirely" case this fixes doesn't
apply to them.

**UI wiring:** `connectFailed` was previously unhandled — nothing told the
operator a connection had given up versus "still trying". `MainWindow`
now shows a distinct red "Rig: connection failed" status (vs. orange for a
normal disconnect) so it reads as "needs action", not "will reconnect
itself". More importantly, `RadioConfigDialog` — the actual place an
operator is looking when fixing a bad port setting — previously only
flipped its Connect/Disconnect buttons optimistically on click, with zero
feedback about whether the attempt actually succeeded (the only error
output went to `MainWindow`'s status bar, hidden behind the modal dialog).
It now has its own status label and live-wires the active
`RadioInterface`'s `connected()`/`connectFailed()` signals for the
duration it's open (`MainWindow::onOpenRadioConfig()`), re-wiring after
every `startRadio()` call since that call destroys and rebuilds `m_radio`.

**Verification:** full rebuild clean; app launches and all `QT_DEBUG`
self-tests (including `runMfskLoopbackSelfTest`, unaffected by this
change) pass, confirming no regression. **Not yet verified against the
actual failure case** (a real wrong COM port on Hamlib hardware) — that
setup is on a separate machine from where this fix was developed. The
reasoning above (moving the specific blocking calls off-thread, bounding
retry count, the Qt-documented safety of the context-object
`invokeMethod` overload) is sound but the field scenario that reported
this bug hasn't been re-run yet to confirm the fix.

---

## ADR-107 — TX audio reverts to QAudioSink (pull mode) with computed-duration completion, replacing QMediaPlayer

**Status:** Decided
**Date:** July 2026

**Decision:** `AudioEngine::startTx()` plays TX audio via `QAudioSink::start(QIODevice*)`
in pull mode (reusing the existing `GainedAudioDevice` QIODevice, now with a
`headerBytes` parameter — `0` here, since `QAudioSink` gets its format from
the `QAudioFormat` passed to its constructor, not by parsing a container) —
replacing the `QMediaPlayer`-based approach from ADR-070/ADR-072-REVISED.
Completion is a computed-duration `QTimer` (`samples.size() / SAMPLE_RATE`
seconds + 150ms safety margin), not `QAudioSink::stateChanged()`/`IdleState`.
`floatToPcm16()` replaces `buildWav()` — raw PCM bytes, no WAV header needed.

**Root cause this fixes:** real over-the-air recordings (590SG → Hermes,
analyzed via a standalone diagnostic built directly against `Demodulator`/
`Frame`/`PreambleSync` — see session notes, not reproduced here) showed a
strikingly consistent, exactly-reproducible **~680ms of true silence** at
the start of every real transmission, before the actual modulated audio
began — confirmed by RMS envelope analysis (a sharp *step* from noise floor
to full signal level, not a gradual AGC ramp) and ruled out as PTT/relay/ALC
settling by a manual pre-keyed-radio test that showed the identical gap.
Since HAVEN's 16-symbol preamble is only 512ms, this ~680ms of dead air
consumed the *entire* preamble before real audio ever reached the radio —
explaining why real signals never produced a preamble lock (nothing to
lock onto) while the payload, arriving after the gap, decoded at ~98%
confidence. The gap was isolated to `AudioEngine::startTx()`'s `QMediaPlayer`
path specifically (manually pre-keying PTT — bypassing `pttLeadMs` — didn't
change it), consistent with `QMediaPlayer`'s heavier FFmpeg-backed demux/
decode/format-negotiation pipeline (real overhead even for a trivial raw
WAV) versus `QAudioSink`'s more direct PCM path.

**Why QAudioSink is safe to revisit despite two prior rejections
(ADR-072, ADR-072-REVISED):** both prior rejections were specifically about
*completion* detection — `IdleState` firing the instant data is handed to
the driver, not when playback genuinely finishes — never about *startup*
latency, and never about the underlying full-duration playback being
wrong. In fact pull mode was already established (in the original ADR-072,
before being revised) as the fix for a *different*, separate WASAPI
push-mode truncation bug, and that fix is reused unchanged here. This
decision combines "the part already proven to work" (pull-mode full-
duration playback) with a completion signal that doesn't depend on the
part that was broken (a computed timer instead of `IdleState`).

**Platform impact:** Windows is where the prior bug lived, but since this
avoids `IdleState` entirely, that specific bug is sidestepped rather than
fixed-in-place. Linux (PulseAudio/PipeWire) and Raspberry Pi (ALSA) have no
documented `QAudioSink` issues in this codebase's history; `QAudioSink` is
the more standard/native Qt6 Multimedia pattern on both, and avoiding
`QMediaPlayer`'s demux/decode overhead is a plausible small win on the
Pi's constrained CPU specifically.

**Risk accepted and how it's bounded:** a computed-duration timer assumes
playback finishes on schedule — if the OS output backend adds its own
buffering latency, the timer could fire slightly before the last symbol
has actually drained through hardware (same *shape* of risk as the
original `IdleState` bug, much smaller in practice). Mitigated by the
150ms margin added to the timer here, plus the pre-existing, unchanged
`TX_TAIL_MS` operator setting that adds further PTT-hold margin after
`txComplete()` fires — this decision only changes how "is playback done"
is detected, not the existing PTT-release safety logic built on top of it.

**Verification:** full rebuild clean, no new warnings; all `QT_DEBUG`
self-tests (FEC, Frame, MFSK chunked loopback, Audio) pass, confirming no
regression — these don't exercise real hardware TX playback timing though.
**Not yet verified:** actual measured TX startup latency on the real
590SG/Hermes hardware after this change — the next real-world test should
confirm the ~680ms gap is gone (or much smaller) and that the preamble
now survives intact.

---

## ADR-108 — Debug logging throttled and de-synced from the main thread's real-time audio path

**Status:** Decided
**Date:** July 2026

**Decision:** Two changes to reduce debug-logging overhead on the thread
that also drives real-time audio consumption:

1. `main.cpp`'s `messageHandler()` no longer calls `QFile::flush()` on
   every single `DEBUG`-level line — that forces a real disk sync per
   call. `WARN`/`CRIT`/`FATAL` still flush immediately (rare, worth
   persisting right away); `DEBUG` lines are flushed every 50th line
   instead, plus an unconditional flush wired to
   `QCoreApplication::aboutToQuit` so a clean shutdown never loses the
   last buffered lines.
2. `MfskModem::tryCompleteFrame()`'s two per-chunk progress lines
   (`"waiting for header"`, `"collecting X/Y"`) — which previously fired
   on nearly *every* audio chunk during frame collection, ~100+ times
   over a single message — are now throttled to once every 10 chunks
   (reusing the existing `m_collectTicks` counter, already incremented
   once per chunk for the collect-timeout check).

**Reasoning:** `AudioEngine`'s RX path (`onRxDataAvailable()`) has no
dedicated thread — it runs on the same main/GUI thread as everything
else, including `qDebug()`'s full cost (mutex, `QDateTime::currentDateTime()`,
file write + `flush()`, and an `fputs()` to the console). Reported symptom:
decode reliability degrading over the course of a session (clean early on,
progressively worse later — including manual Transmit-field entries, which
naturally happen later in a session than a quick macro-button test),
independent of measured SNR (verified same corruption at 3dB *and* 20dB
above noise floor — see prior session notes) and independent of RX/TX DSP
state (a dedicated soak test showed zero `PreambleSync` score degradation
over a full simulated hour; `Frame::assemble()` was verified to produce
byte-for-byte identical output across 50 repeated calls). That combination
pointed at something outside the DSP layer entirely and specific to
*session duration* — a live terminal's scrollback growing over a session
is a plausible reason `fputs()`/console rendering gets slower over time,
and disk `flush()` cost can similarly vary with log file size — either
of which, run synchronously ~20-30 times/second on the same thread
responsible for promptly draining the OS audio buffer, could cause the
main thread to fall behind by more than that buffer's depth, resulting in
genuine, silent sample loss at specific points in a transmission (not a
decode bug — audio that was never actually processed).

**Verification:** rebuild clean, self-tests pass (no DSP logic touched).
**Not yet verified:** whether this resolves the real-world degradation —
next live session should confirm decode reliability no longer declines
over time, including a manually-typed (not macro-button) message late in
a session.

---

## ADR-109 — Root-caused mid-transmission MFSK payload corruption to real-time RX sample loss; widened audio buffer, added gap detection

**Status:** Decided
**Date:** July 2026

**Decision:** Widened `AudioEngine`'s RX `QAudioSource` buffer from 4 audio
chunks (~170ms) to 16 chunks (~683ms); added wall-clock-vs-audio-time gap
detection in `onRxDataAvailable()` to directly log real-time sample loss
when it happens (`QAudio::UnderrunError` is deprecated and no longer
emitted as of Qt 6.11, so this can't be detected via `QAudioSource`'s own
error signal — confirmed by reading `qaudio.h` directly); made the "Record
RX Audio to WAV" debug tool's real-time capture path bulk-copy raw floats
instead of converting to int16 per-sample, deferring that conversion to
save time so the tool itself adds minimal main-thread cost while active.

**Investigation, in the order eliminated (each step used real evidence,
not assumption):**
1. *RX algorithm state degrading over a session* — ruled out by a
   dedicated soak test feeding `PreambleSync` a full simulated hour of
   noise with periodic clean-signal injections: score stayed at exactly
   1.0000 every time, zero degradation.
2. *TX audio generation corrupting on repeated calls* — ruled out by
   calling `Frame::assemble()` on an identical message 50 times and
   diffing every sample against the first call: byte-for-byte identical
   every time (`Frame::assemble()` constructs fresh local `Preamble`/
   `Modulator`/`FEC` instances per call — no possible cross-call state).
3. *Real RF-path fading* — ruled out because the test setup was dummy
   load to dummy load in the same room; no ionospheric path exists for
   fading to occur on.
4. *Debug logging competing with real-time audio consumption* (ADR-108's
   fix) — checked directly against a failing capture's log: the
   "collecting X/Y" lines were already spaced ~400ms apart (matching the
   throttle), confirming that build was in use, and it still failed —
   ruling logging out as the explanation for *that* failure, though the
   throttle is still good practice independent of this bug.

**The actual finding:** offline analysis of a WAV captured via the "Record
RX Audio to WAV" debug tool (added this session specifically so audio
could be inspected from the exact point `AudioEngine` delivers to the
modem, not a separately-recorded file that leaves open whether it saw the
same data) showed something decisive. Comparing every one of a message's
176 expected symbols against what the real `Demodulator` code actually
detected: symbols 0–119 (the full preamble, header, and 92 payload
symbols) were **all exactly correct**, high confidence throughout. Then,
at exactly symbol 120, detection flips to mostly wrong — but not randomly
wrong: `detected[120..124]` matched `expected[121..125]` exactly, i.e. the
entire symbol stream shifted by precisely one symbol-width at one sharp,
discrete instant, with detection confidence staying high on *both* sides
of the break. That signature — perfect, then an instantaneous whole-symbol
shift, not a gradual decline — is what a block of audio samples being
silently dropped (or duplicated) mid-stream produces; it is not consistent
with noise, weak SNR, or a demodulator bug (both of which degrade
gradually and reduce confidence, not shift the whole stream by exactly one
symbol while staying confident).

**Why this points at `AudioEngine` specifically:** this WAV was captured
via a signal tap on `AudioEngine::rxDataReady` itself — the same data
`DspPipeline`/`MfskModem` consume. A discrete sample-count anomaly present
in that exact stream means the loss happened at or before that point:
inside `AudioEngine`'s capture path (the `QAudioSource`/OS driver read
cycle), not in VAC, not in a separate recording tool, and not in DSP
decode logic (independently proven correct twice this session against
real captured audio). `startRx()`'s previous buffer size — 4 chunks,
~170ms — is not much headroom against *any* main-thread stall (a UI
repaint, a slow logging call, or similar) on the same thread responsible
for draining that buffer in time; once it's exceeded, the OS/driver layer
drops audio before Qt ever sees it, silently.

**A fair question raised mid-investigation, addressed directly:** could
the newly-added RX-recording tool itself have been perturbing the system
enough to help cause the very glitch it caught? Its real-time cost (a
per-sample clamp+scale+cast+`push_back` loop) was small in isolation
(microseconds against a ~42.6ms chunk period) but non-zero and new, so it
couldn't be ruled out cleanly. Addressed by making its real-time path a
single bulk float `insert()` with the int16 conversion deferred entirely
to save time — minimizing the tool's own footprint regardless of whether
it was actually a contributing factor, so future captures aren't
confounded by the act of capturing.

**What is NOT yet fixed, only mitigated:** the wider buffer and gap
logging don't address whatever *causes* the main thread to stall long
enough to matter — they make that stall survivable (more headroom) and
observable (a direct log line with estimated samples lost) if it still
happens. The root stall itself (waterfall repaint cost? something else?)
has not been identified.

**Verification:** rebuild clean, self-tests pass (no DSP logic touched).
**Not yet verified:** whether the wider buffer prevents recurrence in a
real session, and whether the new gap-detection log line actually fires
and correctly characterizes the gap size if/when this happens again —
both need a real live test to confirm.

---

## ADR-110 — Fixed RxDisplay callsign-link double-substitution corrupting decoded message display

**Status:** Decided
**Date:** July 2026

**Decision:** Removed a redundant `QString::replace()` call in
`RxDisplay::renderMessage()`'s sender-callsign highlighting.

**Bug:** the callsign-highlighting code ran two sequential replacements
against the same, progressively-modified string:
```cpp
processed.replace(senderCallsign.toHtmlEscaped(), callLink);
processed.replace(senderCallsign, callLink);
```
For a plain callsign like "N8SDR", `toHtmlEscaped()` is a no-op, so the
first call already inserts the full link HTML
(`<a href='haven://callsign/N8SDR' style='color:#4a9fd4;...'>N8SDR</a>`).
That HTML itself contains the literal text "N8SDR" twice — once in the
`href` URL, once in the visible link text. The second `replace()` call
then matches *those* occurrences and wraps them in the link HTML again,
nesting the markup inside itself and corrupting it. Visible symptom:
decoded messages displayed with raw HTML leaking into the text, e.g.
`N8SDR' style='color:#4a9fd4;text-decoration:none'>N8SDR US-1017 K`.

**Why it looked worse than it was:** this surfaced during the ADR-109
real-signal testing and initially looked like it might be a decode
failure. It was not — `MfskModem`'s own log line showed the actual decoded
text was byte-for-byte correct (`"CQ POTA DE N8SDR US-1017 K"`, CRC OK,
FEC converged). The corruption was purely in `RxDisplay`'s HTML rendering,
downstream of a correct decode. Worth remembering for future confusing
displays: check the raw `MfskModem: decoded message` log line before
assuming a garbled on-screen message means a DSP/decode bug.

**Fix:** keep only the single `processed.replace(senderCallsign, callLink)`
call.

**Verification:** rebuild clean, self-tests pass. Manually traced the
corrected logic against the exact message that exposed the bug
("CQ POTA DE N8SDR US-1017 K") — produces a single, correctly-nested
`<a>` tag with no residual duplicate replacement.

---

## ADR-111 — Removed reference-lifetime hazard in GainedAudioDevice (audit finding, pre-threading-refactor)

**Status:** Decided
**Date:** July 2026

**Decision:** `GainedAudioDevice::m_data` (`AudioEngine.h`) changed from
`const QByteArray& m_data` to an owned `QByteArray m_data`.

**Context:** found during a targeted audit of `AudioEngine`/`DspPipeline`/
`MainWindow`'s audio wiring/`WaterfallWidget`, done ahead of a planned
threading refactor to move real-time audio/DSP processing off the main
UI thread (see the waterfall per-chunk cost investigation this session).
`GainedAudioDevice` was holding a live reference to
`AudioEngine::m_txPcmData` rather than its own copy. That was only safe
because of hand-maintained destruction ordering — both
`onTxCompletionTimer()` and `stopTx()` always delete `m_txGainDevice`
before calling `m_txPcmData.clear()`. Nothing was broken today, but it's
exactly the kind of assumption a threading/ownership refactor could
silently violate (moving TX handling across a thread boundary, changing
teardown order, etc.), turning into a dangling-reference use-after-free
with no warning.

**Fix:** store an owned copy instead. `QByteArray` is copy-on-write, so
this costs a refcount bump, not a real data copy — no measurable
performance impact — while removing the ordering dependency entirely
rather than continuing to rely on it staying correct.

**Verification:** rebuild clean, self-tests pass. Only construction site
is `AudioEngine::startTx()`, which already passes a plain `QByteArray`
member — no other callers assumed reference semantics.

---

## ADR-112 — Moved AudioEngine + DspPipeline to a dedicated worker thread

**Status:** Decided (implemented, live hardware verification pending)
**Date:** July 2026

**Decision:** `AudioEngine` and `DspPipeline` now run on a dedicated worker
`QThread` (`MainWindow::m_dspThread`), created and moved into right after
their construction in `MainWindow`'s constructor, before any other setup.
Everything else (`MainWindow`, `WaterfallWidget`, `PTTManager`, radio-control
backends, `LogManager`) stays on the GUI thread as before.

**Why:** ADR-109 root-caused real mid-transmission sample loss to
`AudioEngine::onRxDataAvailable()` — which must keep draining the OS audio
buffer in real time — sharing the GUI thread with work that has no
real-time obligation, most notably `WaterfallWidget::pushChunk()` at "Fast"
speed (a full FFT + manual 120-row image scroll + repaint on every single
chunk). Widening the RX buffer (also ADR-109) made this survivable but
didn't address the structural cause. This finishes that fix properly, the
way fldigi and most real-time digital-mode software do it: isolate the
audio/modem loop on its own thread so GUI work can never block it again.

**Design, and why:** one worker thread owns both objects together, not
split, not two threads. `AudioEngine::rxDataReady -> DspPipeline::
onAudioChunk` is the hottest path in the app (every 42.6ms); same-thread
means Qt resolves it as a direct call with zero added latency. It also
means `DspPipeline::setMode()`'s `m_modem` pointer swap can never run
concurrently with `onAudioChunk()` — a single thread's event loop only
ever processes one queued call at a time, so this race (present before
this change, harmless only because everything was single-threaded) is
closed for free, with no lock needed.

Getting there required a preparatory pass (this session, same investigation
— see the plan doc referenced in git history) to make the surrounding code
safe for a thread it wasn't written for:
- `DspPipeline::m_rxGain` -> `std::atomic<float>` (written from a GUI
  slider, read both by `onAudioChunk()` on the worker thread and a
  GUI-thread level-meter lambda).
- `DspPipeline::m_rxCache` -> guarded by `std::mutex` (written by
  `onAudioChunk()`, read directly from the GUI thread on a callsign click);
  `getRxMeasurement()` now returns `std::optional<RxMeasurement>` by value
  instead of a pointer into the map, since a pointer wouldn't stay valid
  past the mutex being released.
- Every method with a synchronous return value that GUI-thread code
  depended on (`DspPipeline::transmit()`'s `bool`, `setMode()`'s
  immediately-read-back `passbandLowHz()/passbandHighHz()/modeName()`,
  `AudioEngine::startTx()`'s `bool`, `DspPipeline::generateToneSweepAudio()`'s
  return value) was redesigned to be signal-driven instead — new signals
  `transmitFailed`, `modeReady`, `toneSweepAudioReady`, plus reusing the
  existing `audioError`/`messageTransmitted`. `QMetaObject::invokeMethod`
  can't return a synchronous result, so none of these could stay
  return-value-driven once the call crosses a thread boundary.
- Every remaining direct GUI->worker call (`AudioEngine::startRx/stopRx/
  startTx/stop`, `DspPipeline::transmit/setMode/onTxComplete/
  setToneMonitor/setAfcEnabled/setSquelchThreshold/requestToneSweepAudio`)
  now goes through `QMetaObject::invokeMethod(target, &Class::method,
  Qt::AutoConnection, args...)` instead of a plain method call. Using
  `Qt::AutoConnection` (not hard-coded `Qt::QueuedConnection`) meant this
  conversion was itself a no-op behaviorally *before* the thread move (same
  thread still resolves direct) — the mechanical call-site conversion and
  the actual thread move landed as separable, independently-buildable
  changes rather than one big-bang commit.

**Teardown:** `AudioEngine`/`DspPipeline` are un-parented (`setParent
(nullptr)`) before `moveToThread()`, since a QObject with a parent can't
move threads — meaning they're no longer automatically deleted by
`MainWindow`'s destructor either. `~MainWindow()` now explicitly:
`QMetaObject::invokeMethod(m_audio, &AudioEngine::stop,
Qt::BlockingQueuedConnection)` (deliberately blocking, so the destructor
doesn't proceed until `stop()` has actually finished on the worker
thread), then `m_dspThread->quit(); m_dspThread->wait();`, then explicit
`delete` on both objects.

**What did NOT need to change:** `PTTManager`'s own `QTimer` watchdog,
radio-control backends (`RigctldClient`/`TCIClient`/`HamlibClient`), and
`LogManager` — none have a real-time obligation, so they stay on the GUI
thread untouched. TX/RX serialization (`MainWindow` already calls
`AudioEngine::stopRx()` before every TX) needed no new synchronization —
queuing both calls onto the same worker thread preserves the existing
ordering automatically, since that thread's event loop processes them
FIFO.

**Verification:** rebuild clean, self-tests pass (unaffected — they run
before `MainWindow` exists). Confirmed the app launches and stays stable
past self-test range with no crash. **Not yet verified: the actual,
direct empirical test this whole refactor exists for** — a real, extended
RX session at Fast waterfall speed, confirming zero `AudioEngine: RX
timing gap` warnings (ADR-109) where they were previously reproducible —
plus a full manual TX/RX/mode-switch/squelch/AFC/tone-sweep pass under
genuine multi-threading, including deliberately forcing a `startTx()`
failure to confirm the signal-driven cleanup path (the highest-complexity,
most novel piece of this change) recovers correctly. This requires real
hardware and hasn't been done yet.

---

## ADR-113 — Fixed O(N²) re-demodulation in MfskModem::tryCompleteFrame() — the actual remaining root cause of decode reliability degrading over time

**Status:** Decided
**Date:** July 2026

**Decision:** `MfskModem::tryCompleteFrame()` now demodulates only the
newly-arrived samples on each check and appends them to a persistent,
incrementally-growing cache (`m_cachedSoftSymbols`), instead of
re-demodulating the entire accumulated `m_rxBuffer` from scratch every
time.

**Context:** after ADR-112's worker-thread move (isolating `AudioEngine`/
`DspPipeline` from GUI-thread work), live testing still showed decode
reliability degrading after a couple of minutes — the exact symptom this
whole investigation started from, and one the threading fix alone did not
resolve. `haven_log.txt` from that session showed a stark pattern: the
first two (short, 128–176 total symbol) messages decoded perfectly with
only one small, FEC-recoverable timing gap. Starting with the third
message — longer, 320 total symbols — `AudioEngine`'s gap-detection
warning (ADR-109) fired *repeatedly within that single message's
collection*, each gap larger than the last (743ms -> 1053ms -> 1122ms ->
... -> 1561ms, samples lost climbing from ~2900 to ~42000), and the
message failed completely (`fecIter=1200` = 6 blocks x 200, total
non-convergence). Every subsequent message showed the identical escalating
pattern and failed the same way.

That signature — a stall that *worsens continuously within one message's
collection*, appearing only once messages cross a length threshold — means
something scales with message/buffer length inside the DSP processing
itself, not a one-off external stall. Reading `tryCompleteFrame()`
confirmed it: every check called
`m_demodulator.demodulateToSoft(m_rxBuffer, m_timingOffset,
m_demodBinOffset)` on the *entire* accumulated `m_rxBuffer`, and
`demodulateToSoft()` (`Demodulator.cpp`) runs an FFT-based `detectSymbol()`
call for every symbol from the given offset to the end of whatever buffer
it's handed. Since `m_rxBuffer` grows by one chunk on every call while
`tryCompleteFrame()` re-demodulates the *whole thing* each time (throttled
to roughly once per new symbol), the total cost of collecting an N-symbol
message was O(N²) FFT-based symbol detections instead of O(N) — fine for
short test messages, but exceeding the real-time budget once messages got
long enough, and by construction getting *worse* the longer collection
continued, exactly matching the observed pattern. This is a pure
computational-complexity bug in the DSP layer, independent of and
unaffected by ADR-112's threading work — no amount of thread isolation
fixes an algorithm that's inherently too expensive on its own dedicated
thread.

**Fix:** `tryCompleteFrame()` now computes `newSampleOffset = m_timingOffset
+ m_cachedSoftSymbols.size() * SAMPLES_PER_SYMBOL`, demodulates only that
new tail segment, and appends the result to `m_cachedSoftSymbols` (a new
`MfskModem` member). The local variable `softSymbols` becomes a reference
to this cache so the rest of the function's logic (header decode, frame
completion check, `frameSymbols` slice) is unchanged. Cache is cleared in
`resetRx()` (new message) and in `applyFineTimingCorrection()` if it
actually shifts `m_timingOffset` (everything cached under the old offset
would otherwise be invalid) — `applyFineTimingCorrection` itself is gated
behind `m_fineTimingEnabled` (false by default, experimental/unvalidated),
so this is a correctness-for-later concern, not an active one.

**Verification:** rebuild clean, self-tests pass (including the MFSK
loopback test, which exercises this exact path). Additionally verified
directly with a standalone timing diagnostic: fed a 56-character, 191-chunk
(~8 second) synthetic message — matching the real-world failure case's
approximate length — through the actual `MfskModem::processAudioChunk()`
in a chunked loop with per-chunk wall-clock timing. Result: the message
decoded correctly (`crcOk=1 converged=1`), and per-chunk cost during
collection stayed flat throughout (2.18ms at the start of collection to
2.42ms near the end, max 4.37ms) — comfortably under 6% of the 42.7ms
real-time budget per chunk, with no growth pattern at all. This directly
confirms the O(N²) -> O(N) fix in the actual production code path, not
just in theory.

**Not yet verified:** a real, extended live RX session to confirm this
resolves the field-observed "stops decoding after a couple minutes"
symptom end to end — the standalone timing test proves the mechanism is
fixed, but hasn't been confirmed against real hardware/signal conditions
yet.

---

## ADR-114 — Fixed stale high-water-mark in PreambleSync's peak-picking, causing preamble locks to freeze on an old position indefinitely

**Status:** Decided
**Date:** July 2026

**Decision:** `PreambleSync::pushSample()`'s peak-picking now resets its
high-water mark (`m_runBestScore`) only when a new run starts *and* enough
samples have passed since the last run ended (`RESET_GAP_SAMPLES`, one
full preamble length) to indicate this is a genuinely different signal —
not on every run-end unconditionally, and not never (the two things tried
and rejected before landing here — see below).

**Context:** after ADR-113's O(N²) fix, a live retest still showed the
original "decodes twice, then never again — not even the same message"
symptom. The fresh `haven_log.txt` showed something new: starting about a
minute after the last successful decode, the log filled with
`preamble lock offset -2140023 out of pretrigger range (size=144000) —
dropping`, and this offset grew *more negative over time*, in lockstep
with real elapsed time (roughly one sample rate's worth of drift per
second) — `-2.14M` -> `-3.55M` -> `-4.70M` -> `-5.74M` -> `-6.18M` samples
across the session. Every later preamble attempt got silently discarded
this way; nothing ever decoded again.

Tracing `offset = lock.sampleIndex - m_preTriggerDropped`
(`MfskModem.cpp`), the culprit was in `PreambleSync::pushSample()`
(`PreambleSync.cpp`): `m_runLength` resets when a new above-threshold run
starts, but `m_runBestScore` was never reset anywhere except at
construction. Once any high-scoring run occurred (e.g. ~0.99 for a real
preamble), `m_runBestScore` stayed at that value forever. Every later run
— including a genuinely new, real preamble from a subsequent transmission
— that scored even slightly lower than that stale watermark could never
update `m_runBestLock`, so every future lock kept re-emitting the *old*
run's position. As real time (and `m_preTriggerDropped`) kept advancing
while that stale `sampleIndex` stayed frozen, the reported offset grew
increasingly negative — exactly the observed pattern. This is very likely
the actual explanation for the "reliability degrades over a session"
complaint that predates this entire investigation.

**Getting the fix right took three iterations:**
1. First attempt: reset `m_runBestScore` on every run-end (both the
   natural end-of-run branch and the `MAX_RUN_SAMPLES` safety-cap branch).
   Broke `MfskLoopbackSelfTest` — a clean, single-message loopback started
   locking onto a marginal `~0.45` mid-cap point instead of the true
   `~0.99` peak. Root cause: `MAX_RUN_SAMPLES` exists to bound worst-case
   lock latency, not to mark a run as "truly over" — a single preamble's
   own correlation can legitimately still be climbing toward its true peak
   when the cap fires, and resetting there discarded that progress.
2. Second attempt: only reset on the natural end-of-run branch (leave
   `MAX_RUN_SAMPLES` alone). Still broke the same self-test, differently —
   two separate runs a few hundred ms apart (the preamble's own
   correlation genuinely dipping below threshold and recovering — the
   score curve isn't purely monotonic, per `pushSample()`'s existing
   comment) needed the *first* run's high score preserved into the
   *second* to find the true peak; resetting unconditionally on every
   natural end broke that too.
3. Final fix: track `m_lastRunEndSample` (samples since the last run
   ended, whichever branch ended it) and only reset `m_runBestScore` when
   a *new* run starts if the gap since then exceeds `RESET_GAP_SAMPLES`
   (one preamble length, ~512ms) — long enough to comfortably cover any
   single preamble's own multi-run detection wobble, far shorter than the
   seconds-to-minutes gap between genuinely separate messages.

Also extended `MfskLoopbackSelfTest.h`'s lead-in silence from 0.25s to 4s
(exceeding `PRE_TRIGGER_SAMPLES`'s 3s) — unrelated to the above, but
surfaced during this investigation: a real RX session runs far longer than
250ms before any real signal arrives in practice, and the self-test's
short lead-in could occasionally hit a separate, narrow, pre-existing
"cold start" edge case (`-286 out of pretrigger range` — present in this
project's very first-ever successful-decode log, long before today) where
the very first candidate lock lands before the pretrigger buffer has
enough history. That edge case isn't what this test exists to verify, and
was previously invisible in practice only because the staleness bug fixed
above accidentally "recycled" a good position into the very next attempt —
which stopped happening the moment that bug was fixed.

**Verification:** self-tests pass (rebuild clean, loopback test decodes
correctly). Directly verified the actual field scenario with a standalone
diagnostic: two identical messages separated by 3 minutes of noise (with
realistic spurious threshold-crossings in between) — both decoded
correctly (`crcOk=1 converged=1`), where before this fix the second would
have been discarded via a stale, frozen lock position from the first.

**Not yet verified:** real hardware/live RX session, to confirm this
resolves the field-observed symptom end to end.

## ADR-115 — Added a free-text QTH settings field; fixed two clickable-field bugs (MacroPanel macro tag, RxDisplay tag parsing)

**Status:** Decided
**Date:** July 2026

**Decision:** Added `StationInfo::qth` (`RadioSettings.h`), a plain
free-text field with no derived meaning, plus a matching Settings dialog
entry. "QTH" has no single fixed convention in amateur radio — grid,
state, county, or city are all common uses — so rather than guessing which
existing structured field `<myQTH>` should map to, the operator now
chooses what to type there themselves.

**Context:** `<myQTH>` was expanding to the operator's name instead of any
location data — `MacroPanel.cpp`'s `<myQTH>` replacement was a copy-paste
of the `<myName>` line above it (`result.replace("<myQTH>", info.opName,
...)`), and no QTH field existed in Settings at all to copy correctly from
in the first place.

Investigating the same "clickable field" system surfaced a second, unrelated
bug: `RxDisplay::renderMessage()`'s tag-value regex was
`[^\s][^N^Q^G^R^P^S^F]*?` — intended as "stop before hitting the next tag
name," but inside a character class only the *first* `^` negates; every
`^` after that excludes a literal character, not a fresh negation. This
silently truncated any NAME/QTH/GRID/POTA value containing the letters
N, Q, G, R, P, S, or F — e.g. "Springfield" or "US-1234" — the moment one
of those letters appeared. Fixed by removing the broken exclusion list and
relying on the existing lookahead assertion alone to bound each match.

**Verification:** rebuild clean, self-tests pass. `<myQTH>` confirmed
correct by the operator after the fix. The regex fix was confirmed by code
tracing (lookahead alone correctly bounds lazy `.*?`) and a clean rebuild;
`LogPanel::populateField()` was independently confirmed already correct
for every scheme (callsign, pota, sota, grid, rs, name, qth, fd), so this
one regex fix is expected to make all of them click-to-populate
consistently now.

## ADR-116 — FrequencyControl: fixed no-radio manual entry; added right-click direct numeric entry

**Status:** Decided
**Date:** July 2026

**Decision:** `DigitDisplay::mousePressEvent()` now bootstraps out of the
"Enter MHz" placeholder (hz==0) on a left click, seeding a default 14 MHz
and emitting `frequencyRequested` so the log panel (and radio, if
connected) picks it up — the same click also opens a `QInputDialog` for
direct numeric MHz entry on a *right* click, working from any state
including the placeholder.

**Context:** with no radio connected, `MainWindow` leaves the frequency
display at `hz==0`, and there was no way back out: `paintEvent()` never
draws digit positions while in placeholder state, `mouseMoveEvent()`
exits early on empty text (so no digit ever registers a hover target),
and `wheelEvent()` explicitly refuses to act when `m_hz==0` — three
independent dead ends with no path to enter a frequency for logging
purposes when operating without a connected rig.

Separately, per-digit scroll-tuning makes large frequency jumps (e.g.
3.500.000 -> 28.150.000) slow — many wheel clicks per digit. Right-click
now opens a direct "Frequency (MHz)" entry dialog instead, applying
through the same `frequencyRequested` path as scrolling.

**Verification:** rebuild clean, self-tests pass, confirmed visually via
screenshot and functionally by the operator.

## ADR-117 — Macro editor: clickable tag reference inserts at cursor instead of requiring manual typing

**Status:** Decided
**Date:** July 2026

**Decision:** In the Edit Macro dialog (`MacroPanel::onMacroRightClicked()`),
the tag reference list (`<myCall>`, `<myQTH>`, `<clr>`, `<TX>`, etc.) is now
a rich-text `QLabel` with each tag as a clickable link; clicking inserts
that tag into the Macro Text field at the current cursor position via a
fetched `QTextCursor` (written back with `setTextCursor()` so consecutive
clicks insert in sequence rather than all at the same position).

**Why:** avoids operator typos in tag names when building a macro by hand
— a mistyped tag silently fails to expand at send time with no feedback.

**Verification:** rebuild clean, self-tests pass, confirmed visually.

## ADR-118 — Preamble-sync-idle diagnostic gated behind an opt-in environment variable

**Status:** Decided
**Date:** July 2026

**Decision:** `MfskModem`'s periodic (~1/sec) "preamble sync idle — best
score" diagnostic is now off by default, gated behind
`qEnvironmentVariableIsSet("HAVEN_VERBOSE_SYNC")` (checked once, cached in
a static local).

**Why:** with the decode-reliability investigation (ADR-109 through
ADR-114) resolved and confirmed working, this heartbeat has no ongoing
value during normal UI-focused work and dominates the terminal during
routine listening. It's DSP/RX-path-specific — irrelevant to diagnosing UI
bugs — so safe to silence without losing the ability to bring it back
(set the env var before launch) if decode issues need investigating again.

## ADR-119 — Converted main window layout from a fixed QSplitter to movable/resizable QDockWidget panels

**Status:** Decided
**Date:** July 2026

**Decision:** `MainWindow`'s single vertical `QSplitter` (Waterfall / RX
display / Log / a combined Level+Macro+TX row) is replaced with five
independent `QDockWidget` panels — Waterfall, Received, Log, Levels, and
Transmit (which holds both the Macro Panel and TX input together, per
operator preference) — freely movable, resizable, and floatable within
the main window. `m_stationInfo` and the frequency/mode/squelch/rig/RX
status controls are pinned in a non-movable top `QToolBar`; only the
free-text status message remains in a bottom toolbar. Layout persists via
`QMainWindow::saveState()`/`restoreState()` (`QSettings` key
`ui/dockState1`, replacing the old `ui/splitterState4`).

**Context:** operator wanted screen-real-estate control matching typical
dockable-panel apps rather than a fixed top-to-bottom order. `QMainWindow`
(which `MainWindow` already inherits) supports this natively — no
third-party docking library needed.

**Three non-obvious pitfalls found during implementation, worth recording:**

1. **Dock widgets need `setObjectName()`.** `saveState()`/`restoreState()`
   identify docks and toolbars by object name, not window title. None had
   one set initially — persistence would have silently failed to restore
   correctly. All docks and both toolbars now have explicit object names.

2. **`QSizePolicy::Fixed` fights manual dock resize.** `LevelPanel` had
   `QSizePolicy::Fixed` on both axes — harmless in its old spot in a plain
   `QHBoxLayout` (never manually resized there), but inside a dock area
   Qt's layout kept snapping it back to its size hint, which looked to the
   operator exactly like "resizing doesn't stick" (the reported symptom).
   Fixed by changing to `QSizePolicy::Maximum` on both axes: shrinkable via
   manual drag-resize, but never stretched wider/taller than its natural
   content — `Preferred` was tried first and rejected, since it let the
   dock area grow the widget *past* its natural size too, spreading its
   internal meter strips apart with visible gaps instead of just leaving
   harmless empty margin.

3. **`splitDockWidget()` can leave a dock floating on first show.** After
   reworking the default dock arrangement (merging Macro Panel back into
   the Transmit dock), two panels intermittently rendered as separate
   floating windows instead of tiling into the main window on first
   launch — calling `splitDockWidget()` during construction, before the
   window has ever been shown/laid out, appears to be the trigger.
   Fixed defensively: explicit `setFloating(false)` on every panel
   immediately after the default-arrangement `splitDockWidget()` calls,
   only when no saved state is being restored.

Also, while in this area: increased `FrequencyControl`'s digit-display
font, width/height bounds, and step-button size by 50% (visual sizing
request, no behavioral change).

**Verification:** rebuild clean, self-tests pass. Verified visually via
screenshot for default arrangement, panel bundling, and the floating-dock
fix; drag/resize/float/restart-persistence verified interactively by the
operator (confirmed working after the `QSizePolicy` fix above).

## ADR-120 — LogPanel's Parks/SOTA entry fields no longer hidden based on the operator's own Settings

**Status:** Decided
**Date:** July 2026

**Decision:** `LogPanel::updateFieldVisibility()` previously hid the Parks
and SOTA entry fields entirely whenever the *local* operator had no POTA
reference (`StationInfo::potaRefs`) or SOTA reference (`StationInfo::sotaRef`)
configured in Settings, in addition to hiding them during Field Day mode.
That settings-based gating is removed — Parks and SOTA fields are now shown
whenever Field Day mode is off, full stop.

**Why:** the fields being hidden was tied to whether *I* am activating a
park/summit, not whether the contact I'm logging is. A POTA/SOTA *hunter*
— someone chasing activators without activating anything themselves — has
no ref configured in Settings, yet still needs to log the reference the
*other* station gave. The old logic made that impossible without first
lying to Settings about operating an activation. General principle
going forward: field visibility may depend on operating mode (Field Day
on/off), never on whether my own station happens to have a value configured
for that field.

**Verification:** rebuild clean, self-tests pass (process reaches the main
window, which only happens after `runFecSelfTest`/`runFrameSelfTest`/
`runAudioSelfTest` all return true in a Debug build).

## ADR-121 — RS-Sent made editable; decoded messages auto-populate the Log entry fields

**Status:** Decided
**Date:** July 2026

**Decision:** Two related logging-automation changes:

1. `LogPanel`'s RS-Sent field (`m_rsSent`) is no longer `setReadOnly(true)`.
   It's still auto-filled from the measured SNR when a callsign link is
   clicked (`MainWindow::onElementClicked` → `computeRS()` →
   `setRsSent()`), but the operator can now type over it directly.
2. New `LogPanel::autoPopulateFromMessage(senderCallsign, text)`, called
   from `MainWindow::onMessageReceived()` for every message that passes
   CRC. It fills the same `NAME:`/`QTH:`/`GRID:`/`RS:`/`POTA:`/`SOTA:`/`FD:`
   tags `RxDisplay::renderMessage()` already recognizes for clickable
   links, but directly into the log entry without requiring the operator
   to click each one.

   Two safety rules, both driven by an operator complaint scenario ("a QSO
   in progress shouldn't get wiped by someone else's transmission, but a
   dead/abandoned contact shouldn't block logging the next one either"):
   - **Never overwrite a field the operator already typed into** — only
     empty fields get auto-filled. POTA is separately merge-safe already
     (see `populateField()`).
   - **Ignore messages that aren't part of my exchange.** A decoded
     message only touches the entry if its sender matches whatever
     callsign is already entered (continuing the same contact), or the
     message text contains my own callsign as a whole word (someone
     addressing me directly). An unrelated station's traffic — someone
     else's QSO, a general CQ not meant for me — never touches the entry,
     whether it's currently blank or mid-QSO. If a *different* station
     addresses me directly while a stale contact still occupies the entry,
     the entry is cleared first, then populated fresh from the new message
     — no manual Clear click needed to move on from an abandoned QSO.

**Why:** the operator wants "always able to directly enter data in any
field" (motivated RS-Sent's read-only removal) plus less manual field-
clicking per contact — but blind auto-fill-on-every-decode risks either
interrupting/overwriting an in-progress entry, or (if gated too loosely)
polluting a blank entry with unrelated band traffic. The
same-sender-or-addressed-to-me test is the same signal a human operator
already uses to judge "is this transmission part of my QSO."

**Not done (deliberately out of scope this pass):** parsing bare callsign-
like words out of message body text for auto-population — `msg.
senderCallsign` (already parsed upstream in `DspPipeline`) is used
directly instead, since duplicating that extraction here risked
disagreeing with the upstream parse.

**Verification:** rebuild clean; self-tests pass (process reaches the main
window). Interactive over-the-air verification of the auto-populate
same-sender / addressed-to-me / unrelated-station branches still pending —
flag if real-world behavior doesn't match.

## ADR-122 — RS: tag value capped at 2 characters instead of running to the next tag or end of string

**Status:** Decided
**Date:** July 2026

**Decision:** In both `RxDisplay::renderMessage()`'s tag regex and
`LogPanel.cpp`'s `parseStructuredTags()` duplicate (see ADR-121), the
`RS:` tag now has its own dedicated branch, `RS:(?<rsval>\S{1,2})`,
capturing at most 2 non-space characters. Every other tag
(NAME/QTH/GRID/POTA/SOTA/FD) keeps the original lazy-to-next-tag-or-end
rule, since those legitimately vary in length.

**Why:** live testing found `RS:` swallowing trailing text it shouldn't —
a report is always exactly 2 characters, but it's typically the last
field before a literal "K" over-prosign (e.g. `"...RS:52 K"`), and "K"
isn't a recognized tag boundary, so the generic rule ran right through
the space and captured "52 K" as one value. Confirmed with a standalone
regex simulation against several shapes (trailing "K" with/without a
space, a blank report, a value at end of string, and a NAME value
containing the letters R/S to confirm no regression) before rebuilding.

**Verification:** rebuild clean; self-tests pass (process reaches the main
window).

## ADR-123 — RadioConfigDialog no longer runs off-screen on shorter displays

**Status:** Decided
**Date:** July 2026

**Decision:** `RadioConfigDialog` (opened from the top-level "Radio" menu
item) had five stacked `QGroupBox`es (method selector, rigctld, TCI,
Hamlib, TX timing) always simultaneously present in one `QVBoxLayout`
with no height constraint at all -- the three connection-method groups
were only ever `setEnabled(false)`, never hidden, regardless of which
method was actually selected. On a shorter/laptop display this pushed
the bottom of the dialog (including Connect/Save/Close) off-screen with
no way to reach it. Two changes:

1. `onMethodChanged()` now calls `setVisible()` instead of `setEnabled()`
   on the two inactive connection-method groups -- only one of
   rigctld/TCI/Hamlib is ever relevant at a time, and hiding the other
   two reclaims real vertical space rather than just greying it out.
2. The dialog's content is wrapped in a `QScrollArea`, and the
   constructor caps the window to 90% of `screen()->availableGeometry()`
   height, sizing to the content's natural height (whichever method's
   group is visible) up to that cap. Below the cap, the dialog simply
   renders at its natural size with no scrolling; above it, the
   `QScrollArea` takes over so the bottom (buttons) is always reachable
   via scroll instead of clipped off-screen.

**Why:** the operator reported not being able to fit the dialog on a
laptop screen. `setVisible()` directly addresses "split into smaller
sections" (the common case needs far less height once irrelevant method
settings aren't taking up space at all), and the screen-height cap +
scroll area directly addresses "size dynamically with the display" as a
safety net for any display/DPI-scaling combination, current or future.

**Non-obvious pitfall found while implementing:** once content lives
inside a `QScrollArea`, the dialog's own `sizeHint()` no longer reflects
the true content height -- `QScrollArea` deliberately does not propagate
its viewport widget's full size demand upward, since decoupling from
content size is the whole point of being scrollable. Sizing the dialog
from `this->sizeHint()` after wrapping produced an arbitrary, too-small
window that needed scrolling even when the screen had plenty of room.
Fixed by sizing from the *inner content widget's* `sizeHint()` instead,
kept via a new `m_scrollContent` member, queried after `loadSettings()`
(so it reflects whichever method's group `onMethodChanged()` actually
left visible).

**Second pitfall, found by actually switching methods and
screenshotting, not assumed:** live-switching to a taller method's group
(e.g. Direct/Hamlib, which has the most fields) after the dialog was
already sized for a shorter one (e.g. TCI) triggers a vertical
scrollbar, which steals ~20px from the viewport width -- with the
dialog's minimum width left at its original value and the horizontal
scrollbar forced off, this silently clipped the Refresh and Close
buttons at the right edge instead of showing them. Fixed by widening
`setMinimumWidth` (440 → 470) to leave headroom for the scrollbar, and
leaving the horizontal scrollbar policy at its default (`AsNeeded`)
rather than forcing it off, as a fallback in case some future field
combination is still too wide.

**Verification:** rebuild clean, self-tests pass. Verified interactively
via screenshot: dialog opens at 456×711 for TCI (the saved method),
fully visible with no scrolling needed; switching live to Direct/Hamlib
correctly hides the TCI group, shows the Hamlib group, and (after the
width fix) shows Refresh/Save/Close fully un-clipped with a vertical
scrollbar handling the extra height. Not yet verified: an actual small
laptop display (this was tested via the screen-height-cap logic and a
large-monitor multi-monitor setup, not a real small-screen device).


---

## ADR-124 — Restored ADR-003's Qt-free DSP layer: DspLog shim; DspPipeline moved to src/pipeline/

**Status:** Decided
**Date:** July 2026

**Decision:** The "no Qt in `src/dsp/`" invariant (ADR-003) had drifted:
`Frame.cpp` included QDebug/QString, `MfskModem.cpp` carried the legacy
QDebug logging it inherited when it was split out of DspPipeline (a
deliberate, documented exception at the time), and `DspPipeline` — a
QObject with signals, QString, QMap, QDateTime by design — lived inside
`src/dsp/`. Three changes restore the invariant instead of re-scoping it:

1. **`DspLog.h/.cpp`** — a tiny Qt-free logging shim: printf-style
   `dspLog()`/`dspWarn()` feeding a `std::function` sink that defaults
   to no-op. `main.cpp` wires the sink to qDebug/qWarning at startup, so
   DSP log lines land in `haven_debug.log` exactly as before.
2. **`Frame.cpp` and `MfskModem.cpp` converted** to the shim (message
   text preserved; `qEnvironmentVariableIsSet` → `std::getenv`). The
   MfskModem exception existed only to avoid rewriting debug lines
   during the Phase-2 relocation; converting it now ends the exception.
3. **`DspPipeline` moved to `src/pipeline/`** (git mv, includes and
   CMakeLists updated). It is deliberately Qt-facing glue and can never
   satisfy ADR-003 — the honest fix is location, not exemption.

**Why:** CLAUDE.md calls the Qt-free DSP layer the most critical
constraint, and the project actively uses it (diag_sync.cpp, the PSK31
standalone verification builds in ADR-104). Documentation promising an
invariant the code doesn't keep is worse than either fixing the code or
the doc; fixing the code preserves the testing benefit.

**Verification:** grep confirms zero Qt includes/types in `src/dsp/`
(comments aside). Full Debug rebuild clean; all startup self-tests pass
(see ADR-126 — this session also made them actually run); MFSK loopback
self-test decodes end-to-end with DSP log lines flowing through the shim
into haven_debug.log.

---

## ADR-125 — July 2026 external review fixes: TX-gain use-after-free, PTT release failure alarm, end-of-TX amplitude ramp, ADIF mode mapping, rigctld reply framing

**Status:** Decided
**Date:** July 2026

**Decision:** An external code review of v0.3.0-beta flagged six issues;
five are fixed here (the sixth is ADR-124), plus its minor notes:

1. **TX gain fader use-after-free.** The GUI-thread fader lambda called
   `AudioEngine::setTxGain()` directly, which dereferenced
   `m_txGainDevice` — an object the DSP thread deletes at end of TX.
   The gain value now lives in AudioEngine itself
   (`std::atomic<float> m_txGain`); GainedAudioDevice holds a pointer to
   it. `setTxGain()` never touches the per-transmission device object,
   so the cross-thread call is safe and stays latency-free (no queued
   invoke needed). Extends ADR-111's audit of this class.
2. **A failed PTT-off no longer passes silently.** `PTTManager::txOff()`
   previously ignored `setPTT(false)`'s result and skipped the command
   entirely when `isConnected()` was false — a rig could stay keyed
   while the UI showed "Listening...". txOff() now gates on a new
   `m_pttKeyed` flag (not connection state, so a mid-TX disconnect still
   attempts the unkey and a racing auto-reconnect can deliver it),
   retries up to 3 attempts, and emits `pttReleaseFailed()` on failure —
   MainWindow raises a modal critical alarm ("YOUR RADIO MAY STILL BE
   TRANSMITTING"). `requestTX()` now treats a failed `setPTT(true)` as
   fatal and refuses to start TX audio. The 120s watchdog inherits all
   of this via txOff().
3. **End-of-transmission key click fixed.** The transmission ended
   mid-sine at full amplitude — an instantaneous step that splatters on
   adjacent frequencies, transmitted on-air since PTT is still held
   through the tail delay. `Frame::assemble()` now applies one
   raised-cosine envelope over the WHOLE assembled frame (RAMP_SAMPLES
   up at the very start, down at the very end). This is the correct
   re-introduction of the constant orphaned when the per-symbol ramps
   were removed (per-symbol ramps pulsed at 31.25 Hz — see CHANGELOG
   Phase 8; per-transmission edges cannot pulse).
4. **ADIF exports no longer emit MODE=DIGITAL** (not a valid ADIF mode —
   LoTW/QRZ/POTA reject or bucket it as unknown). The active modem's
   name is stamped into each contact (MainWindow → `modem_name` field);
   LogManager maps it to the proper pair (Haven MFSK → MODE=MFSK,
   SUBMODE=HAVEN-FSK; PSK31 → MODE=PSK, SUBMODE=PSK31) at insert, and
   AdifExporter maps legacy MODE=DIGITAL rows by submode at export. This
   also fixes PSK31 QSOs being logged as HAVEN-FSK contacts.
5. **rigctld reply framing.** `sendCommand()` took whatever bytes
   followed the first readyRead: replies split across TCP segments were
   truncated, and a reply arriving after a timeout sat in the buffer to
   be prepended to the NEXT command's response — so a stale "RPRT 0"
   from a frequency poll could read as a successful PTT command
   (compounding issue 2). sendCommand() now drains stale bytes before
   each write and reads until the reply is structurally complete
   (expectedLines newline-terminated lines, or a terminal "RPRT n"
   line), looping on waitForReadyRead within the timeout budget.

Minor notes from the same review, also fixed: FEC (whose constructor
runs Gaussian elimination) is now a `Frame` member instead of being
rebuilt in every assemble()/parse() call; MfskModem's decoded-message
log reports the real CRC outcome instead of unconditional "CRC OK";
Frame.cpp's stale "double header" comment now says 3x;
`LogManager::open()` no longer calls addDatabase() for an
already-registered connection name; PTTManager's destructor documents
that it only blocks on socket I/O when the rig is actually keyed.

**Verification:** full Debug rebuild clean; all startup self-tests pass,
including the MFSK loopback test decoding a complete frame assembled
WITH the new amplitude ramp (preamble lock score 0.999, CRC OK, FEC
converged). PTT failure paths and the modal alarm are code-reviewed but
not yet exercised against a real rigctld failure — worth a live test by
killing rigctld mid-TX.

---

## ADR-126 — Debug self-tests and haven_debug.log were silently non-functional under CMake

**Status:** Decided
**Date:** July 2026

**Decision:** Two pieces of verification infrastructure the project
believed it had were discovered dead while verifying ADR-124/125:

1. **The `#ifdef QT_DEBUG` self-test chain in main.cpp never ran.**
   `QT_DEBUG` is defined by qmake in debug builds; CMake defines only
   `QT_NO_DEBUG` (in release). No build configuration ever defined
   `QT_DEBUG`, so every "Debug build" compiled the self-tests out.
   CMakeLists.txt now adds `$<$<CONFIG:Debug>:QT_DEBUG>`.
2. **haven_debug.log was never written.** main.cpp resolved
   `QCoreApplication::applicationDirPath()` BEFORE constructing
   QApplication — that returns an empty string, pointing the log at the
   drive root, where open() fails silently. The log-file setup now runs
   right after QApplication construction, and a failed open() prints a
   stderr notice instead of failing silently.

**Why this matters beyond the fix:** several past ADRs record
"all QT_DEBUG self-tests pass" as verification. Those runs were
vacuous — the tests weren't compiled in. The underlying features were
verified by other means at the time (loopback captures, live QSOs), but
future verification claims should note WHICH mechanism actually ran.

**Verification:** Debug rebuild now emits self-test activity at startup
(Frame::parse header line, demod self-test PASS, full MFSK loopback
decode) and haven_debug.log is created next to the executable with all
DSP/Qt log lines present. A deliberately-broken self-test was not
exercised; the pass path is confirmed live.

---

## ADR-127 — Delete CPP_REWRITE.md; fold its rationale into this document

**Status:** Decided
**Date:** July 2026

**Decision:** `CPP_REWRITE.md` — the original plan/status document for the
Python→C++ rewrite — is deleted. The rewrite it planned is complete
(v0.3.0-beta shipped), and its useful content lives elsewhere: build
instructions in CLAUDE.md/README.md, architecture in CLAUDE.md, history in
CHANGELOG.md, decisions here.

**Reasoning:** The July 2026 audit found the document stale to the point
of being actively misleading: it claimed v0.2.0-beta status with "Phase 2
in progress" (contradicting its own completed-phases list), gave build
instructions for the removed `cpp/` subdirectory, repeated the pre-ADR-107
"TX uses QMediaPlayer" audio guidance, described the demodulator as
Hann-windowed (it is deliberately rectangular — see Demodulator.cpp),
referred readers to a Python implementation on `main` that was deleted in
commit 0be7261, and listed a "DCD + backoff" feature that was never built.
A third architecture document duplicating CLAUDE.md is exactly how it
rotted; deleting it removes the maintenance burden rather than
transferring it.

**Rationale preserved from the deleted document** (extends ADR-001, which
already records the deployment-complexity/GIL-jitter/tkinter reasons for
the rewrite):

- **Performance headroom:** the DSP workload itself is light (31.25
  symbols/sec), but C++ leaves headroom for the waterfall, spectrum
  analysis, and real-time signal monitoring without CPU concern on any
  target platform, including Raspberry Pi.
- **Maintainability/handoff:** a typed, compiled codebase with proper
  headers is easier to maintain, extend, and hand off to other
  contributors than the prototype's Python.

**Supersession note:** ADR-001's statement that "the Python prototype will
be preserved on `main` as a reference" no longer holds — the prototype was
removed entirely in commit 0be7261. `HAVEN-FSK_Specification.md` is the
interoperability reference; this C++ implementation is the sole
implementation.

---

## ADR-128 — Drop unused Qt6::Charts link; GPLv3 rationale unaffected

**Status:** Decided
**Date:** July 2026

**Decision:** `Qt6::Charts` is removed from `find_package()` and
`target_link_libraries()` in CMakeLists.txt. The July 2026 audit found no
QtCharts include or symbol anywhere in `src/` — the module was linked but
never used (a leftover from an early waterfall/spectrum-display plan that
was ultimately implemented with QPainter in WaterfallWidget instead).

**Effect on ADR-101:** none on the decision, one correction to the record.
ADR-101 cited the Charts link as "concrete evidence the project was already
implicitly GPL-obligated" — with the unused link gone, that evidence never
actually bound a shipped binary's license. ADR-101's primary rationale
(GPLv3 intended from inception; copyleft aligned with keeping the mode out
of closed-source forks) is independent of Qt Charts and stands unchanged.
THIRD_PARTY_LICENSES.md's Qt section is reworded accordingly: every Qt
module now linked is available under LGPLv3.

**Practical effect:** windeployqt no longer bundles Qt6Charts.dll, so the
next release ZIP shrinks slightly. Also noted during the same audit:
`Qt6::MultimediaWidgets` likewise has no users in `src/` (no QVideoWidget);
it is left in place pending its own check because Multimedia backend
loading was historically finicky (ADR-107) — remove it in a follow-up
only after a verified build+TX/RX smoke test without it.

## ADR-129 — Per-symbol impulse erasure/de-weighting and LLR scaling rejected: measured no gain

**Status:** Decided
**Date:** July 2026

**Context:** The weak-signal RX hardening plan (phases: erasure marking,
LLR confidence scaling, noise blanker, retry decoding) was gated on the
`--bench` harness (WeakSignalBench.h): calibrated AWGN referenced to
2500 Hz plus HF-realistic lightning crashes (multi-stroke, 300-600 ms
events, 25 dB above signal). v1 baseline, 10 trials/point:

    AWGN: 10/10 at -7 dB, 9/10 at -8 dB, 0/10 at -9 dB
    Crashes at -6 dB: 9/10 @ 120/min, 8/10 @ 240/min, 4/10 @ 480/min

**Decision:** Two proposed decoder-input improvements are rejected on
benchmark evidence and are not to be re-attempted without new information:

1. *Per-symbol impulse erasure/de-weighting* (flag crash-hit symbols via
   time-domain power vs. running median; flatten or discount their soft
   energies). Binary flattening was actively harmful (240 crashes/min:
   8/10 -> 2/10) — after the 2.5x-RMS clipper, a crash-hit symbol is
   degraded-but-informative and the LDPC decoder uses that partial
   information. Proportional de-weighting (q = median/power) was exactly
   neutral in a same-seed A/B at 120/240/480 crashes/min. Root cause:
   Demodulator::detectSymbol's square-then-normalize step is already an
   implicit per-symbol reliability weight — broadband crash energy raises
   all 16 tone bins, and normalization flattens that symbol's soft vector
   toward uniform on its own. Explicit marking is redundant.

2. *Noise-scaled LLRs* (scale FEC::softToLLR output by an estimated
   noise power so BP trusts strong symbols more). Void by architecture:
   FEC::decodeBlock is normalized MIN-SUM, which is invariant to any
   common scaling of its input LLRs — only relative per-symbol weights
   can change decisions, and (1) measured those as neutral. The textbook
   LLR-scaling gain applies to sum-product decoders only.

**Consequences:** v1's surprising crash robustness is now understood and
documented rather than accidental: clipper + interleaver + normalization
self-erasure. Remaining weak-signal candidates operate on different
mechanisms and stay open: a pre-demodulation time-domain noise blanker
(removes crash energy before the FFT), retry decoding (timing/AFC
perturbation on failure), and preamble-sync improvements — the bench's
lock-vs-decode split (added with this ADR) attributes failures to sync
or decode so future effort aims at the real bottleneck. Any change is
gated on moving the -8/-9 dB AWGN rows or the 240/480 crashes/min rows.

## ADR-130 — Failure attribution: preamble sync is the entire weak-signal cliff; noise blanker rejected

**Status:** Decided
**Date:** July 2026

**Context:** The bench's lock-vs-decode split (added with ADR-129)
attributes every failure at every tested operating point to PREAMBLE
SYNC, not the decoder:

    AWGN:    -8 dB: 9/10 locked -> 9/10 decoded
             -9 dB: 0/10 locked (decoder never ran)
    Crashes at -6 dB: 120/min 10 locked -> 9 decoded
             240/min 8 locked -> 8 decoded; 480/min 4 locked -> 4 decoded

Once PreambleSync locks, the LDPC+interleaver payload chain decodes in
essentially 100% of trials. Consequences drawn:

1. *Retry decoding rejected without implementation* — re-running a
   decode that succeeds whenever it runs cannot help.

2. *Noise blanker rejected on measurement.* Two variants were built to
   protect the preamble window from crashes, both A/B'd with same seeds:
   - Per-sample amplitude threshold (4x robust median, 2 ms hangover):
     catastrophic. In near-threshold noise, ~0.7% of legitimate samples
     exceed any usefully low amplitude threshold, and the hangover
     multiplied that into ~45% of ALL samples blanked — every AWGN row
     collapsed. Amplitude cannot separate crash from noise at low SNR:
     at -6 dB (2500 Hz ref) the full-band noise floor puts a
     25-dB-above-signal stroke peak at only ~2.9 sigma.
   - 1 ms window-energy threshold (2.5x median of recent window
     energies, crash-proof baseline): statistically clean — AWGN rows
     unchanged, fired only on crashes — and exactly NEUTRAL: decodes
     identical to blanker-off at 120/240/480 crashes/min (one extra
     lock at 240 that still failed CRC). Removing crash energy from
     PreambleSync's input does not recover the lost locks. It would
     also have needed a consecutive-blank cap to avoid erasing strong
     signals that legitimately exceed a quiet baseline (untestable in
     the bench's near-threshold scenarios).

**Decision:** No pre-demodulation blanking or post-demodulation symbol
treatment ships; the 2.5x-RMS clipper remains the only impulse defense,
now with evidence it is sufficient at realistic storm rates given the
interleaver and normalization self-erasure (ADR-129). The open, focused
weak-signal question is PreambleSync itself: its score threshold (0.45)
and correlation structure set both the -9 dB AWGN wall and the
crash-hit-preamble losses. Any future work targets sync sensitivity
directly and is gated on the bench's lock columns.

## ADR-131 — Adaptive preamble-sync threshold: resting 0.35 with false-lock defense

**Status:** Decided
**Date:** July 2026

**Decision:** PreambleSync's detection threshold drops from a fixed 0.45
to a resting 0.35, wrapped in the operator-proposed (WD9N) adaptive
defense: every false-lock symptom — header validation failure,
implausible nBlocks, or a frame that never completes — raises the live
threshold one 0.03 step (max 0.48); a CRC-verified decode snaps it back
to resting; ~10 s idle intervals decay it 0.005 toward resting.
Adjustments surface in the UI status bar with value and reason.

**Evidence (--bench-sync trade study, 10 trials/point):** locks/decodes
at -9 dB by threshold: 0.45: 4/4, 0.40: 8/7, 0.35: 10/8, 0.30: 10/6
(0.30 trades decode quality for marginal-alignment locks — 0.35 is the
knee). False locks in 300 s of pure noise per threshold: ZERO at every
threshold tested down to 0.30. Full-bench validation of the shipping
config vs the ADR-129 baseline: AWGN -9 dB 0/10 -> 8/10 decoded (0/10
-> 10/10 locked); crashes at -6 dB: 240/min 8/10 -> 10/10, 480/min
4/10 -> 8/10 (locks 10/10 everywhere — the decoder is now the limiter,
not sync). Roughly +1.5 dB sensitivity plus doubled survival in the
heaviest storm scenario, from a threshold constant.

**Caveat driving the adaptivity:** pure Gaussian noise is the easy
false-lock case; on-air interferers (FT8, CW, PSK31, splatter) correlate
better than noise and are not modeled in the bench. The adaptive raise
is the defense for that gap and needs on-air observation.

## ADR-132 — Message length open-ended up to the TX watchdog; RX listening window scales to the header

**Status:** Decided
**Date:** July 2026

**Decision (per WD9N, emcomm/HAVEN-E direction):** message length is
deliberately open-ended rather than capped at a "typical QSO" size. The
RX collect timeout is no longer a fixed 20 s — that constant silently
made any message beyond ~11 LDPC blocks undecodable — but is computed
from the validated header's nBlocks (frame duration + 3 s), so RX
listens exactly as long as the message needs and no longer. Before a
valid header, a 5 s header-arrival window applies (the header's position
is known exactly from the preamble). The plausibility bound on nBlocks
derives from the TX PTT watchdog: a compliant station cannot transmit
past 120 s, giving ~76 blocks / ~900 payload bytes per frame; the RX
buffer cap scales to 130 s to match. Anything longer (HAVEN-E forms)
belongs to application-layer multi-frame chunking with per-frame CRC and
selective resend, not longer single frames.

**Also bounds false-lock exposure:** the rare noise header that passes
validation (~1/500) can now only hold RX for the duration of the message
it claimed, and every such event raises the adaptive threshold (ADR-131).

**Context:** HAVEN-E — a contemplated emcomm application profile where
predefined forms are transmitted as field data only (both stations know
the form; RX refills it, can render PDF). Rides HAVEN v1's wire format
unchanged as a payload convention; plain stations still see readable
delimited text.
