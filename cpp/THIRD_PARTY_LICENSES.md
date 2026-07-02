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
MultimediaWidgets, Charts, Sql.

**Important:** In Qt's open-source distribution, the **Qt Charts** module is
available **only under GPLv3** — it has no LGPL option. Since this project
links `Qt6::Charts`, the application's distribution is bound by GPLv3 terms
for that dependency regardless of any other module's licensing. This is the
concrete fact that motivated correcting the project's own license to GPLv3
(see `DECISIONS.md` ADR-101). All other linked Qt6 modules are available
under LGPLv3, which is also compatible with a GPLv3 application.

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
