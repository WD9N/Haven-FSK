# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

HAVEN-FSK is a 16-tone MFSK HF digital mode application for amateur radio. The C++ rewrite (this branch) replaces a Python prototype. It is a single Qt6 executable targeting Windows, Linux, and Raspberry Pi.

## Build

**Windows (primary):**
```bat
build.bat
```
or manually:
```bat
set QT_DIR=C:\Qt\6.11.1\mingw_64
cd build
cmake .. -G Ninja -DCMAKE_PREFIX_PATH=%QT_DIR% -DCMAKE_MAKE_PROGRAM=C:\Qt\Tools\Ninja\ninja.exe
cmake --build . --parallel
```

**Output:** `build\HavenFSK.exe`

**Linux:**
```sh
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/qt6
make -j$(nproc)
```

Qt 6.11.1 minimum. Components used: Core, Widgets, Network, WebSockets, SerialPort, Multimedia, MultimediaWidgets, Charts, Sql. KissFFT is vendored in `src/third_party/kissfft/`.

## Self-Tests

No external test framework. Tests run automatically in Debug builds via `main.cpp`:

```cpp
#ifdef QT_DEBUG
    HavenFSK::runFecSelfTest();
    HavenFSK::runFrameSelfTest();
    HavenFSK::runAudioSelfTest();
#endif
```

Test headers: `src/dsp/FecSelfTest.h`, `src/dsp/FrameSelfTest.h`, `src/audio/AudioSelfTest.h`. Output goes to stdout and `build/selftest_out.txt`.

To run tests: build in Debug mode and launch the executable. Non-zero exit = test failure.

## Architecture

Six subsystems with strict layering:

### DSP Layer (`src/dsp/`) — Qt-free C++17
The most critical constraint: **no Qt types in `src/dsp/`**. All interfaces use `std::vector`, `std::string`, `uint8_t`, etc. This allows compilation and testing without Qt. Conversion to/from Qt types happens only at the `AudioEngine` boundary.

- **Constants.h** — All protocol parameters as `constexpr`. SAMPLE_RATE=48000, SYMBOL_RATE=31.25 Hz, NUM_TONES=16, FFT_SIZE=12288 (8× zero-padded). Change protocol parameters here only.
- **Modulator** — Continuous Phase FSK (CPFSK); phase accumulator carries across all symbol boundaries, never reset.
- **Demodulator** — FFT-based soft symbol detection with 8× zero-padding and ±3 bin guard window.
- **DCD** — Carrier detect via SNR: signal band 450–1050 Hz vs noise reference 150–400 Hz, threshold 12 dB.
- **Preamble** — 16-symbol sequence `{0,15,0,15,7,8,7,8,...}`, soft correlation detection, threshold score ≥ 6.0.
- **Frame** — Assembly: preamble + header + CRC-16/CCITT-FALSE + payload. `Frame.h` defines the wire format.
- **FEC** — LDPC(192,96) with Belief Propagation (200 iterations max). Parity check matrix is hard-coded from the Python reference (ADR-012) — do not regenerate without verifying interoperability.
- **DspPipeline** — State machine orchestrating the full RX path (Idle → preamble scan → frame collect → FEC decode → emit) and TX path (text → frame → modulate → audio). Also contains AFC (±75 Hz max tracking) and RX measurement cache.

### Audio (`src/audio/`)
- **AudioEngine** — Wraps Qt6 `QAudioSource` (RX) and `QMediaPlayer` (TX). TX uses QMediaPlayer with in-memory WAV; do not use QAudioSink for TX (fires IdleState before hardware drains).
- Converts int16 PCM ↔ float32 at the boundary to DSP.
- **GainedAudioDevice** — Inline QIODevice with `std::atomic<float>` gain; allows real-time TX level control without thread locks.

### Radio Control (`src/radio/`)
- **RadioInterface** — Pure virtual base; all rig control code depends only on this interface.
- **RigctldClient** — TCP to rigctld (Hamlib bridge, default port 4532).
- **TCIClient** — WebSocket to TCI servers (Thetis/ExpertSDR, default port 50001).
- **PTTManager** — Wraps PTT with 120-second FCC Part 97 watchdog.
- `HamlibClient` is a stub; direct Hamlib linking is deferred.

### UI (`src/ui/`)
- **MainWindow** — Central integration point; connects signals/slots between all panels and the DSP/audio/radio layers.
- **WaterfallWidget** — 4096-bin FFT spectrogram, 50% overlap.
- **FrequencyControl** — Custom digit-scroll frequency entry (fully inline in header).
- **MacroPanel** — 18-button grid; tags `<myCall>`, `<theirCall>`, `<myParks>`, `<mySOTA>`, `<myGrid>`, `<myName>`, `<myFD>`, `<TX>` expanded at send time.
- **RxDisplay** — Decoded messages with clickable structured data (callsigns, RST, POTA refs, grid squares).

### Logging (`src/log/`)
- **LogManager** — SQLite3 QSO database; path is platform-specific (`%APPDATA%` / `~/.local/share` / `~/Library`).
- **AdifExporter** — Generates standard ADIF or activity-aware variants (per-park POTA files, SOTA variants) based on log content.

## Key Invariants

- **48000 Hz is the only valid sample rate.** Other rates produce non-integer samples-per-symbol and break the protocol math. The CMakeLists.txt has `HAVEN_PLATFORM_WINDOWS/LINUX/MACOS` defines; do not add `HAVEN_PLATFORM_GENERIC`.
- **DSP layer has no Qt dependencies.** If you add a file to `src/dsp/`, it must not `#include` any Qt header.
- **Phase accumulator never resets in Modulator.** Resetting it introduces phase discontinuities that degrade the signal.
- **LDPC matrix is fixed.** The parity check matrix in `FEC.cpp` must match the Python reference implementation exactly for interoperability between HAVEN-FSK stations.
- **CRC-16/CCITT-FALSE** (poly 0x1021, init 0xFFFF). Any change breaks compatibility with all existing stations.

## Architecture Decisions

`DECISIONS.md` (ADR-001 through ADR-076) is the authoritative record. Consult it before changing any of the above invariants. Status "Decided" means the decision is not open for re-discussion without new information. Status "Revisable" means reasonable to reconsider.
