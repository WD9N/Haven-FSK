# HAVEN-FSK C++ Rewrite

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
    DspPipeline.h/cpp — RX/TX orchestration
  audio/            — audio I/O (Qt6 Multimedia)
    AudioEngine.h/cpp
    AudioSettings.h
  radio/            — radio control
    RadioInterface.h
    RigctldClient.h/cpp — TCP to rigctld (Hamlib universal server)
    TCIClient.h/cpp     — TCI WebSocket (Thetis/HPSDR)
    HamlibClient.h/cpp  — direct Hamlib stub (future phase)
    PTTManager.h/cpp
    RadioSettings.h
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

### Phase 2 — DSP Layer ✅ Complete

- KissFFT vendored into src/third_party/kissfft/
- Modulator — tone table, raised cosine shaping, byte-to-symbol encoding
- Demodulator — 8× zero-padded FFT, Hann window, non-coherent detection,
  soft symbol output for FEC
- DCD — band energy monitoring, 12 dB threshold, 4-chunk holdoff
- Preamble — generation and correlation-based detection

### Phase 3 — FEC and Framing ✅ Complete

- LDPC(192,96) encoder — Progressive Edge Growth matrix, systematic encoding
- LDPC decoder — belief propagation, min-sum approximation, 200 iterations
- Frame assembly — preamble + header + CRC-16 + FEC payload
- CRC-16/CCITT-FALSE
- Frame parser — sync, header decode, CRC validation, FEC decode

### Phase 4 — Audio Engine ✅ Complete

- Qt6 QAudioSource / QAudioSink integration
- Device enumeration and selection
- Int16 PCM ↔ float32 conversion at DSP boundary
- QSettings device persistence

### Phase 5 — Radio Control and Pipeline ✅ Complete

- DspPipeline — RX state machine, TX path, DCD integration (5A)
- TCI WebSocket client for Thetis/HPSDR (5B)
- rigctld TCP client for all Hamlib-supported radios (5B)
- PTTManager — three-tier backoff, 120-second watchdog (5B)
- Basic operator UI — device selection, RX display, TX input (5A)

### Phase 5C — Full UI (In Progress)

- Settings dialog (radio, audio, station information)
- Macro buttons with `<TX>` auto-transmit tag
- Click-to-populate from decoded RX messages
- Station information panel
- Waterfall display

### Phase 6 — Logging

- Contact log with timestamp, callsign, frequency, band
- POTA/SOTA/Field Day logging fields
- ADIF export (one file per park for POTA, general ADIF always)
- Station info snapshot per QSO

### Phase 7 — Testing and Release

- Loopback tests (modulate → demodulate, verify round-trip)
- On-air testing at 14.075 MHz DIGU
- Performance benchmarking
- Installer packaging (Windows NSIS, Linux AppImage, Pi .deb)

---

## Contributing

The C++ rewrite is on the `cpp-rewrite` branch. Please base all C++ work
on that branch, not `main`.

The Python implementation on `main` is the reference specification.
If you find a discrepancy between the C++ behavior and the specification
document, the specification is authoritative — not the Python code.

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
