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

**Hamlib (optional, for direct CAT rig control — see ADR-103):** not required to build; `HAVEN_ENABLE_HAMLIB` gracefully disables itself if not found, and `RigctldClient`/`TCIClient` are unaffected either way.
- **Windows:** download an official Hamlib release SDK (e.g. `hamlib-w64-4.7.2.zip`) from https://github.com/Hamlib/Hamlib/releases, extract it, and set `HAMLIB_DIR` in `build.bat` to the extracted path.
- **Linux / Raspberry Pi:** `sudo apt install libhamlib-dev` (Debian/Raspberry Pi OS) before running `cmake`.

## Self-Tests

No external test framework. Tests run automatically in Debug builds via `main.cpp`:

```cpp
#ifdef QT_DEBUG
    HavenFSK::runFecSelfTest();
    HavenFSK::runFrameSelfTest();
    HavenFSK::runMfskLoopbackSelfTest();
    HavenFSK::runAudioSelfTest();
#endif
```

`QT_DEBUG` is defined for Debug configs by CMakeLists.txt — it is a qmake convention CMake doesn't provide, and without that define the self-tests silently compile out (ADR-126).

Test headers: `src/dsp/FecSelfTest.h`, `src/dsp/FrameSelfTest.h`, `src/dsp/MfskLoopbackSelfTest.h`, `src/audio/AudioSelfTest.h`. Output goes to stdout and (for DSP/Qt log lines) `haven_debug.log` next to the executable.

To run tests: build in Debug mode and launch the executable. Non-zero exit = test failure; if the GUI appears, all self-tests passed.

## Architecture

Seven subsystems with strict layering:

### DSP Layer (`src/dsp/`) — Qt-free C++17
The most critical constraint: **no Qt types in `src/dsp/`**. All interfaces use `std::vector`, `std::string`, `uint8_t`, etc. This allows compilation and testing without Qt. Conversion to/from Qt types happens only at the `AudioEngine` boundary. Diagnostic logging goes through the `DspLog` shim (`dspLog()`/`dspWarn()`, printf-style), whose sink `main.cpp` wires to qDebug/qWarning — never `#include <QDebug>` in this layer (ADR-124).

- **Constants.h** — All protocol parameters as `constexpr`. SAMPLE_RATE=48000, SYMBOL_RATE=31.25 Hz, NUM_TONES=16, FFT_SIZE=12288 (8× zero-padded). Change protocol parameters here only.
- **Modulator** — Continuous Phase FSK (CPFSK); phase accumulator carries across all symbol boundaries, never reset.
- **Demodulator** — FFT-based soft symbol detection with 8× zero-padding and ±3 bin guard window.
- **DCD** — Carrier detect via SNR: signal band 450–1050 Hz vs noise reference 150–400 Hz, threshold 12 dB.
- **Preamble** — 16-symbol sequence `{0,15,0,15,7,8,7,8,...}`. Live RX detection is **PreambleSync** (per-sample sliding DFT, ADR-105): soft correlation score in [0,1], threshold 0.45 (`PreambleSync::SCORE_THRESHOLD`). The legacy block detector in `Preamble` (`PREAMBLE_THRESHOLD` = 6.0 matches of 16) is used only by self-tests/diagnostics, not the RX path.
- **Frame** — Assembly: preamble + header + CRC-16/CCITT-FALSE + payload. `Frame.h` defines the wire format.
- **FEC** — LDPC(192,96) with Belief Propagation (200 iterations max). Parity check matrix is hard-coded from the Python reference (ADR-012) — do not regenerate without verifying interoperability.
- **DspLog** — printf-style logging shim (`dspLog`/`dspWarn`); no-op until `main.cpp` installs the qDebug/qWarning sink.

### Pipeline (`src/pipeline/`)
- **DspPipeline** — Qt-facing glue (QObject) between AudioEngine and the active IModem: orchestrates the RX path (Idle → preamble scan → frame collect → FEC decode → emit) and TX path (text → frame → modulate → audio), plus AFC tracking and the RX measurement cache. Lives outside `src/dsp/` because it is deliberately Qt-dependent (signals, QString) — moved from `src/dsp/` in ADR-124.

### Audio (`src/audio/`)
- **AudioEngine** — Wraps Qt6 `QAudioSource` (RX) and `QAudioSink` in pull mode (TX, raw int16 PCM — no WAV container; see ADR-107). TX completion is a computed-duration QTimer, NOT `QAudioSink::stateChanged()`/IdleState — that signal fires when data is handed to the driver, not when playback finishes.
- Converts int16 PCM ↔ float32 at the boundary to DSP.
- **GainedAudioDevice** — Inline QIODevice applying real-time TX gain per read; reads AudioEngine's `std::atomic<float>` (the atomic outlives the per-transmission device), so the GUI thread can adjust level without locks.

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

`DECISIONS.md` (ADR-001 through ADR-126 and growing) is the authoritative record. Consult it before changing any of the above invariants. Status "Decided" means the decision is not open for re-discussion without new information. Status "Revisable" means reasonable to reconsider.
