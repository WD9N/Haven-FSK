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
