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

**Status:** Decided
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
