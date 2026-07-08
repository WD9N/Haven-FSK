# Third-Party Licenses

HAVEN-FSK is licensed under the GNU General Public License v3.0. See
`LICENSE` in the repository root for the full text. This file documents
third-party code and libraries used by the project, and any license
obligations they carry.

---

## KissFFT

**Location:** `src/third_party/kissfft/`
**License:** BSD-3-Clause
**Copyright:** Copyright (c) 2003-2010, Mark Borgerding. All rights reserved.
**Upstream:** https://github.com/mborgerding/kissfft

KissFFT is vendored directly into the repository per ADR-005. The
BSD-3-Clause copyright headers in the vendored source files must not be
modified or removed. BSD-3-Clause is a permissive license fully compatible
with inclusion in a GPLv3 project — permissive-licensed code may be
incorporated into GPL works without conflict.

---

## Qt6

**License:** Qt is offered under a tri-license: GPLv3, LGPLv3, and a
commercial license, depending on module.
**Components used** (see `CMakeLists.txt`, `target_link_libraries(HavenFSK ...)`):
Core, Widgets, Network, WebSockets, SerialPort, Multimedia,
MultimediaWidgets, Sql.

All linked Qt6 modules are available under LGPLv3, which is compatible
with this GPLv3 application. (An earlier revision linked `Qt6::Charts` —
GPL-only in Qt's open-source distribution — and cited that link as
evidence the project was already GPL-bound; the module was never actually
used and was dropped in ADR-128. The project's GPLv3 license stands on
its own merits per ADR-101, independent of any Qt module's terms.)

---

## SQLite

Accessed via `Qt6::Sql`'s SQLite driver, not a vendored copy. SQLite itself
is released into the public domain by its authors; no additional obligation
beyond Qt's own licensing (above) applies.

---

## fldigi (reference material + transcribed varicode table)

**License:** GPLv3
**Upstream:** https://github.com/w1hkj/fldigi

fldigi's PSK31 and MFSK modem implementations were consulted as an
algorithmic and interoperability reference during development of HAVEN-FSK's
own PSK31 mode (ADR-101 permits this now that the project is GPLv3).
HAVEN-FSK's PSK31 modem (`src/dsp/psk/`) is a spec-driven reimplementation
rather than a direct port — fldigi's modem code is tightly coupled to its own
sound-buffer/`trx` framework, and HAVEN-FSK's `IModem`/chunk-based interface
is architecturally different — but two specific things were consulted
directly for correctness rather than reimplemented from general PSK31
knowledge, since getting them wrong would silently break interoperability
with real PSK31 stations:

- **Varicode table** (`src/dsp/psk/Psk31VaricodeTable.h`) — the 256-entry
  bit-pattern table is transcribed programmatically (via script, not by
  hand, to avoid transcription error) from fldigi's
  `src/psk/pskvaricode.cxx` (Copyright (C) 2006 Dave Freese, W1HKJ; "Adapted
  from code contained in gmfsk source code distribution", per that file's
  header). This is the fixed G3PLX PSK31 varicode standard — every
  interoperable PSK31 implementation must use the identical table, so this
  is transcription of an interoperability constant, not independent
  creative work.
- **Differential BPSK modulation convention** — confirmed against fldigi's
  `src/psk/psk.cxx` (`tx_bit`/`tx_carriers`/`tx_symbol`, and the
  `sym_vec_pos[]` constellation table) that bit value `0` produces a
  180-degree phase reversal and bit value `1` produces no phase change.
  `src/dsp/psk/Psk31Modulator.h`/`.cpp` document this and implement it as
  new code (a from-scratch continuous-phase raised-cosine-shaped generator),
  not a port of fldigi's multi-carrier TX path.

No other fldigi source was copied or adapted; `Psk31Demodulator` (Costas
loop, matched filter, timing recovery) is an independent implementation
using standard textbook BPSK receiver technique, not fldigi's generalized
multi-carrier correlator/FIR-filterbank approach.

---

## Hamlib

**License:** LGPL v2.1 (`COPYING.LIB` in Hamlib's own repository)
**Upstream:** https://github.com/Hamlib/Hamlib

`HamlibClient` (`src/radio/HamlibClient.h`/`.cpp`) links directly against
libhamlib for native CAT control over USB/serial (e.g. a Kenwood
TS-590SG), as an alternative to `RigctldClient`'s TCP connection to a
separately-launched `rigctld` process — see `DECISIONS.md` ADR-103 (which
extends, not supersedes, ADR-017's original rigctld-only decision).

**Linked dynamically, not statically** — the simpler LGPL v2.1 compliance
path, since it avoids the "provide relinkable object files" obligation
static linking carries, and matches HAVEN-FSK's existing pattern of
bundling DLLs alongside the executable (`windeployqt` already does this
for Qt/FFmpeg). No Hamlib source is modified or embedded in this
repository; `HamlibClient.cpp` is original code calling Hamlib's public
C API (`rig_init`, `rig_open`, `rig_set_freq`, `rig_set_ptt`,
`rig_set_mode`, `rig_set_split_vfo`, `rig_set_level`, `rig_close`,
`rig_cleanup`, `rig_list_foreach`, etc.) — confirmed against the actual
`include/hamlib/rig.h` header, not assumed from memory. LGPL v2.1 is
compatible with distribution alongside a GPLv3 application.

**Windows:** built against an externally-installed Hamlib SDK located via
the `HAMLIB_DIR` variable (see `build.bat`, `cmake/FindHamlib.cmake`) —
official pre-built release archives are available at
https://github.com/Hamlib/Hamlib/releases (e.g. `hamlib-w64-4.7.2.zip`).
The runtime DLL is copied next to `HavenFSK.exe` at build time, same
pattern as `windeployqt`'s Qt DLL bundling. **Not vendored into this
repository** — contrast KissFFT (`ADR-005`), whose vendoring was
justified specifically by its small size (~1200 lines total); that
reasoning doesn't transfer to Hamlib's much larger rig-backend source
tree, so it's treated as an external dependency the same way Qt6 itself
already is.

**Linux / Raspberry Pi:** located via `pkg-config` against the
distribution's `libhamlib-dev` package (not vendored).

**Optional dependency:** if Hamlib isn't found at build time (Windows:
`HAMLIB_DIR` unset/wrong; Linux/Pi: `libhamlib-dev` not installed), the
build proceeds without it (`HAVEN_ENABLE_HAMLIB` auto-disables) —
`HamlibClient` falls back to stub behavior and `RigctldClient`/
`TCIClient` are entirely unaffected.
