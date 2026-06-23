# HAVEN-FSK C++ Rewrite

## Current Status — June 2026

**Branch:** cpp-rewrite
**Version:** v0.2.0-beta (pending on-air verification)
**Build:** Clean, zero errors, all self-tests passing

### Completed Phases
- Phase 1: Project infrastructure, CMakeLists.txt
- Phase 2: DSP layer (Modulator, Demodulator, DCD, Preamble)
- Phase 3A: LDPC(192,96) FEC encoder and BP decoder
- Phase 3B: Frame assembly, parsing, CRC-16
- Phase 4: AudioEngine — Qt6 Multimedia RX/TX
- Phase 5A: DspPipeline — RX state machine, TX path
- Phase 5B: Radio control — RigctldClient, TCIClient, PTTManager
- Phase 5C: Settings dialog, StationInfoWidget, FCC guard
- Phase 5D: RxDisplay clickable elements, LogPanel, MacroPanel, RS measurement cache, AFC, WaterfallWidget placeholder
- Phase 6: SQLite log, LogManager, ADIF export, ExportDialog
- Phase 7: WaterfallWidget, AFC digital correction, FrequencyControl
- Phase 8: UI fixes, PTT wiring, audio engine, TCI debugging

### Pending On-Air Verification
- RX decode of real HAVEN-FSK signals
- Click-to-populate from decoded messages
- AFC locking on real signals
- Full QSO logging from live contact
- TCI frequency set to Thetis (fix implemented, needs test)
- Clean TX audio via QMediaPlayer (fix implemented, needs test)

### Next After On-Air Test
- Move icon files to resources/, update haven_fsk.qrc
- Restore WIN32_EXECUTABLE in CMakeLists.txt
- Write PHASE8B.md: Windows installer, Linux .desktop, Pi .deb
- Tag v0.2.0-beta

---

## Overview

HAVEN-FSK is being rewritten from Python to C++ with Qt6. The Python version
is a working prototype that validated the mode specification and on-air
performance. The C++ rewrite is the production implementation intended for
general release.

The rewrite is being developed on the `cpp-rewrite` branch. The Python
implementation remains on `main` and is not being modified.

---

## Why Rewrite in C++?

The Python prototype served its purpose — it proved the mode works, validated
the specification, and enabled on-air testing. However Python has practical
limitations for a production amateur radio application:

- **Deployment complexity:** Python requires a runtime, virtual environment,
  and multiple pip dependencies. Operators on Windows, Linux, and Raspberry Pi
  all have different setup experiences. A C++ build produces a single executable
  with no runtime dependency.

- **Audio latency:** Python's GIL and interpreter overhead introduce jitter in
  audio callbacks. C++ with Qt6 Multimedia gives deterministic low-latency
  audio I/O suitable for real-time DSP.

- **Cross-platform native UI:** Qt6 produces a native-feeling application on
  Windows, Linux, and Raspberry Pi without the limitations of tkinter.

- **Performance headroom:** The DSP workload is light (31.25 symbols/second),
  but C++ leaves headroom for a waterfall display, spectrum analyzer, and
  real-time signal monitoring without CPU concerns on any target platform.

- **Long-term maintainability:** A typed, compiled codebase with proper headers
  is easier to maintain, extend, and hand off to other contributors.

---

## Target Platforms

| Platform | Minimum | Notes |
|---|---|---|
| Windows | Windows 10 | Primary development platform |
| Linux | Ubuntu 22.04+ | Tested with GCC 11+ |
| Raspberry Pi | Pi 3B+ | Raspberry Pi OS Bullseye or newer, Qt6 required |

Qt6 is required. Qt5 is not supported.

---

## Build Instructions

### Windows

Requirements:
- Qt 6.11.1 or later, installed to `C:\Qt`
- MinGW 64-bit toolchain (installed with Qt)
- CMake 3.20 or later
- Ninja build system

```
cd C:\Haven\cpp
mkdir build
cd build
cmake .. -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64
cmake --build . --config Release
```

Or use the provided `build.bat` from the project root.

### Linux

Requirements:
- Qt6 development packages
- GCC 11+ or Clang 13+
- CMake 3.20+
- Ninja (recommended) or Make

```bash
sudo apt install qt6-base-dev qt6-multimedia-dev cmake ninja-build
cd /path/to/haven/cpp
mkdir build && cd build
cmake .. -G Ninja
cmake --build .
```

### Raspberry Pi

Same as Linux. Qt6 packages are available in Raspberry Pi OS Bullseye and
later via apt. Requires Pi 3B or better.

```bash
sudo apt install qt6-base-dev qt6-multimedia-dev cmake ninja-build
```

Build steps are identical to Linux above.

---

## Architecture

### Directory Structure

```
src/
  main.cpp
  dsp/              — DSP layer (modulator, demodulator, FEC, framing)
    Constants.h     — all protocol constants, single source of truth
    Modulator.h/cpp
    Demodulator.h/cpp
    DCD.h/cpp       — Data Carrier Detect
    Preamble.h/cpp
    FEC.h/cpp       — LDPC(192,96) encoder/decoder
    Frame.h/cpp     — frame assembly, CRC-16
  audio/            — audio I/O (Qt6 Multimedia)
    AudioEngine.h/cpp
  radio/            — radio control
    RadioInterface.h
    TCIClient.h/cpp — TCI WebSocket (Thetis/HPSDR)
    HamlibClient.h/cpp
    PTTManager.h/cpp
  ui/               — Qt6 user interface
    MainWindow.h/cpp
  log/              — contact logging, ADIF export
    LogEntry.h
    LogManager.h/cpp
  third_party/
    kissfft/        — vendored FFT library (BSD-3-Clause)
resources/
  haven_fsk.qrc
```

### Layer Boundaries

The codebase is divided into strict layers with controlled dependencies:

**DSP layer** (`src/dsp/`) — pure signal processing. Uses only C++ standard
library types (`std::vector`, `std::string`, etc.). No Qt types. No audio
device access. No UI. This layer is fully testable in isolation and portable
to any platform with a C++17 compiler.

**Audio layer** (`src/audio/`) — Qt6 Multimedia. Owns audio device I/O.
Converts between Qt audio buffers and the `std::vector<float>` that the
DSP layer expects. This is the only place where Qt types cross into DSP
territory.

**Radio layer** (`src/radio/`) — radio control. TCI WebSocket, Hamlib/rigctld,
PTT management. Qt Network and Qt WebSockets.

**UI layer** (`src/ui/`) — Qt Widgets. Receives decoded text and status from
the DSP and audio layers via Qt signals/slots. Sends user input to the
modulator and radio control layers.

**Log layer** (`src/log/`) — contact logging and ADIF export.

### Third-Party Dependencies

| Library | Version | License | Integration |
|---|---|---|---|
| Qt6 | 6.11.1+ | LGPL-3.0 | System install |
| KissFFT | latest | BSD-3-Clause | Vendored in src/third_party/kissfft/ |

See `THIRD_PARTY_LICENSES.md` for full license texts.

---

## Rewrite Phases

### Phase 1 — Project Infrastructure ✅ Complete

- CMakeLists.txt with Qt6 modules
- Directory structure and stub files for all classes
- Constants.h with all protocol parameters
- build.bat for Windows one-click builds
- Windows deployment (windeployqt)

### Phase 2 — DSP Layer 🔄 In Progress

- KissFFT vendored into src/third_party/kissfft/
- Modulator — tone table, raised cosine shaping, byte-to-symbol encoding
- Demodulator — 8× zero-padded FFT, Hann window, non-coherent detection,
  soft symbol output for FEC
- DCD — band energy monitoring, 12 dB threshold, 4-chunk holdoff
- Preamble — generation and correlation-based detection

### Phase 3 — FEC and Framing

- LDPC(192,96) encoder — Progressive Edge Growth matrix, systematic encoding
- LDPC decoder — belief propagation, min-sum approximation, 200 iterations
- Frame assembly — preamble + header + CRC-16 + FEC payload
- CRC-16/CCITT-FALSE
- Frame parser — sync, header decode, CRC validation, FEC decode

### Phase 4 — Audio Engine

- Qt6 QAudioSource / QAudioSink integration
- Device enumeration and selection
- RX audio callback → Demodulator pipeline
- Modulator output → TX audio pipeline
- Sample rate handling

### Phase 5 — Radio Control

- TCI WebSocket client (Thetis/HPSDR) — PTT, frequency readback
- Hamlib/rigctld client — PTT, CAT control for conventional radios
- PTT manager — VOX fallback, 120-second watchdog timer
- DCD + backoff integration

### Phase 6 — User Interface

- Main window layout
- TX/RX text panels
- Audio device selectors
- Waterfall display
- Signal level indicators
- Station information bar (callsign, frequency, mode)

### Phase 7 — Logging

- Contact log with timestamp, callsign, frequency, band
- POTA/SOTA/Field Day logging fields
- ADIF export

### Phase 8 — Testing and Release

- Loopback tests (modulate → demodulate, verify round-trip)
- On-air testing
- Performance benchmarking
- Installer packaging (Windows NSIS, Linux AppImage, Pi .deb)

---

## Contributing

The C++ rewrite is on the `cpp-rewrite` branch. Please base all C++ work
on that branch, not `main`.

The Python implementation on `main` is the reference specification.
If you find a discrepancy between the C++ behavior and the specification
document (`HAVEN-FSK_Specification.md`), the specification is authoritative —
not the Python code.

Pull requests should:
- Build cleanly on at least one platform (Windows, Linux, or Pi)
- Not introduce Qt types into the DSP layer (`src/dsp/`)
- Not introduce new third-party dependencies without discussion
- Update `THIRD_PARTY_LICENSES.md` if new vendored dependencies are added

---

## License

Copyright (C) 2026 WD9N

This software is licensed under the GNU General Public License v3.
See the LICENSE file for full terms.

Third-party licenses: see THIRD_PARTY_LICENSES.md
