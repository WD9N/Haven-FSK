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
