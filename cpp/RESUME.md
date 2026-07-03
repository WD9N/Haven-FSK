# HAVEN-FSK Session Resume — 2026-07-02 (end of session)

## Branch

`cpp-rewrite`. Everything below is about to be **committed and pushed**
in this session's closing commit — working tree is clean except
pre-existing untracked scratch files (`PHASE*.md` notes, `Palettes.png`,
`UI layout change 1.png`, `Grok Suggested Improvements.txt`,
`gen_h_matrix.py` at repo root and in `cpp/`) which predate this session
and were deliberately left untouched, same as every prior session.

```
<this session's commit will land here — see `git log` for the actual hash>
686dad6 docs: correct HAVEN-FSK spec against actual current code, add Phase 9 changelog entry
56f583e Phase 9: GPLv3 correction, modem abstraction + PSK31 mode, protocol v2, rig control hardening
1c09a86 RX pipeline overhaul: 7 DSP bugs fixed, audio format validation, maxf diagnostic  (prior session)
```

## What Was Done This Session

Two major threads. Full detail in `DECISIONS.md` (ADR-103, ADR-104) and
`CHANGELOG.md`'s "Phase 10" entry — read those before re-deriving
context by grepping the diff.

### 1. Direct Hamlib linking (`HamlibClient`)

Requested because the user has a Kenwood TS-590SG on USB/CAT and wants
to control it without a separately-launched `rigctld` process — matters
most for field/Pi deployment with no runtime internet access. Additive:
`RigctldClient` (external rigctld) and `TCIClient` (SDR software) are
unchanged and still the right choice for their own use cases.

- New `cmake/FindHamlib.cmake` — Windows via `HAMLIB_DIR`-pointed
  external SDK (mirrors the existing `QT_DIR` pattern), Linux/Pi via
  pkg-config. Gracefully degrades (`HAVEN_ENABLE_HAMLIB` off, warning
  only) if not found — never breaks the build for
  `RigctldClient`/`TCIClient`-only users.
- `HamlibClient.h`/`.cpp` fully rewritten from stub to real
  implementation, gated by `#ifdef HAVEN_HAMLIB_ENABLED`. Dynamic rig
  enumeration via `rig_list_foreach()` — no hand-maintained radio list.
- **Real build-tested against the actual Hamlib 4.7.2 w64 SDK, and this
  found four real bugs — two of which made the app fail to launch
  entirely on a clean run, confirmed directly by the user trying it.**
  Full postmortem in `DECISIONS.md` ADR-103's Verification section; the
  short version, because the failure mode is easy to reintroduce if
  `cmake/FindHamlib.cmake` is touched again without reading it first:
  1. Wrong forward-declared struct tag (`struct rig` vs the real
     `struct s_rig`) — compile error, easy to catch.
  2. Missing `libusb-1.0.dll` at runtime — easy to catch (immediate
     crash on launch).
  3. **Assumed Qt's `windeployqt`-bundled `libwinpthread-1.dll`/
     `libgcc_s_seh-1.dll` (same filenames) would satisfy Hamlib's need
     for those — wrong.** Different MinGW builds, not interchangeable.
     This shipped as a real regression the user hit directly ("(null).DLL
     was not found"). Lesson: always copy a third-party binary's *own*
     runtime deps, never assume a same-named file already present will do.
  4. Even after #3, still broken — `find_library()` picked
     `lib/gcc/libhamlib-4.lib` (MSVC-format) over `lib/gcc/libhamlib.dll.a`
     (the correct GNU-ld-native import library). Linked with **zero
     errors** but produced a corrupted PE import table (`objdump -p
     HavenFSK.exe` showed a literal `DLL Name: (null)` entry). This is
     the nastiest of the four — no compile error, no link error, just a
     silently broken binary. Fixed via `find_file()` targeting the exact
     `.dll.a` filename instead of NAMES-based suffix search.
- **Not yet verified**: actual CAT control against the live TS-590SG
  (Connect/frequency/PTT/mode). The app now launches cleanly with Hamlib
  linked, which was the blocker up to this point — the next session
  should open Radio → Configure with the radio connected and confirm
  end-to-end.

### 2. PSK31 real-signal fixes

The user's own live-air testing (and, separately, interop testing
against fldigi) found four real problems that internal loopback
self-tests never would have caught — this is the first time PSK31 in
this codebase was exercised against anything other than itself.

1. **RX display unreadable** — one row per character with `[CRC]`/`[NC]`
   badges (meaningless for PSK31 — no CRC/FEC at all). Root cause:
   `Psk31Modem` emits one event per character, but `DspPipeline`/
   `RxDisplay` treated every event like a complete MFSK framed message.
   Fixed via `ModemRxEvent::isFramedMessage` (default `true`, MFSK
   untouched) routing to a new `DspPipeline::textCharacterReceived()` /
   `RxDisplay::appendStreamingText()` path for continuous-stream modes.
2. **Spaces/newlines silently dropped** — `appendStreamingText()` used
   `insertHtml()` per character; HTML collapses whitespace and ignores
   bare `\n`. Fixed by switching to `insertText()`.
3. **No squelch → noise decoded as constant garbage.** Added DCD gate +
   per-character Costas lock-quality average threshold. **First attempt
   used a fixed `0.7` threshold validated only against noiseless
   loopback — it silently broke ALL real RX** (fldigi decoded the same
   live signal fine, HAVEN showed nothing). Now a runtime, user-adjustable
   value (status-bar spinbox, `QSettings`-persisted, default `0.0` =
   off). **Lesson repeated from the Hamlib thread above: a threshold/
   config tuned only against a clean synthetic test is not validated for
   real-world use — say so explicitly rather than declaring success.**
4. **Missing opening characters on TX**, found via fldigi interop
   testing. Confirmed against fldigi's actual `src/psk/psk.cxx`: PSK31
   needs a leading preamble of 32 continuous phase-reversal symbols (at
   31.25 baud) for the receiving Costas loop to lock before real data
   arrives — HAVEN had none. Fixed in `Psk31Modem::modulateText()`. Found
   and fixed a related latent bug alongside it: `Psk31Modulator`'s
   differential reference point wasn't resetting between separate
   transmissions in the same session.
- **Not yet re-verified against fldigi** after fix #4 — the interop test
  that found the missing-preamble bug should be re-run to confirm fldigi
  now decodes HAVEN's TX cleanly from the first character.

## Current State

### Build
Clean build (rebuilt several times this session via both `build.bat`
and direct `ninja` invocation — see note below on `build.bat`
flakiness). `HavenFSK.exe` launches cleanly with real Hamlib linked
(`objdump -p` shows a clean import table, confirmed via direct user
testing after the four Hamlib bugs above were fixed).

### `build.bat` flakiness noted this session
Multiple times this session, running `build.bat` (which does CMake
configure + ninja build in one script invocation) produced
`Error: could not load cache` even though nothing was actually wrong —
re-running the *same* two steps directly (`cmake ..` then
`C:\Qt\Tools\Ninja\ninja.exe`, separately) always succeeded immediately
after. Root cause not investigated (not blocking, easy workaround). If
`build.bat` fails with that specific message, don't assume something is
actually broken — retry via direct `cmake`/`ninja` invocation first
before spending time debugging.

### Verification method note for future sessions (still holds)
`HavenFSK.exe` is a console-subsystem build — redirecting stdout
captures the Qt debug log directly. For testing Qt-free `src/dsp/` or
`src/dsp/psk/` files in isolation, compile a small standalone `.cpp`
directly against just those files + MinGW at
`C:\Qt\Tools\mingw1310_64\bin`. **Important refinement from this
session**: a standalone test that only compiles
`Varicode`/`Psk31Modulator`/`Psk31Demodulator` does NOT exercise
`Psk31Modem`'s squelch or preamble logic at all — those live in
`Psk31Modem.cpp` itself, which must be compiled into the test too, or
you'll get a false "PASS" that doesn't actually cover the real RX/TX
path a user hits.

### What has NOT been verified
- CAT control against the live TS-590SG (Connect/freq/PTT/mode) — app
  launches now, this is the next real check.
- fldigi interop after the preamble fix (#4 above) — the original test
  that found the bug should be re-run.
- MFSK protocol v2, transmit backoff — unchanged from last session, see
  prior notes below.

## Deferred / Not Yet Done (see DECISIONS.md / spec §7.2, §5's "5")

- **Transmit backoff / DCD-gated collision avoidance** — still
  documented as not-yet-implemented in the C++ version.
- **MFSK pulse shaping** — still deliberately unused (raised-cosine
  tried and reverted post-CPFSK, see Phase 8 changelog entry).
- **QPSK for PSK31** — still unimplemented, BPSK only.
- **DCD_RMS_THRESHOLD tuning** — `Psk31Modem.h`'s carrier-detect RMS
  threshold (`0.01f`) is still an untuned placeholder, same caveat as
  ever; now that the lock-quality squelch is user-adjustable, DCD is the
  remaining single point where real-world tuning might still be needed
  if noise continues to fals-decode even with the new squelch turned up.
- **Adaptive/configurable FEC rate, narrower "weak signal" MFSK variant**
  — unchanged, still deferred.

## Key Files for the New Architecture

- `src/dsp/IModem.h` — the interface everything else is built against.
  Gained `isFramedMessage` (RX event flag) and
  `setSquelchThreshold()`/`squelchThreshold()` (optional, default no-op)
  this session.
- `src/dsp/ModemFactory.cpp` — mode → concrete modem construction.
- `src/dsp/MfskModem.{h,cpp}` — MFSK, unaffected by this session's work.
- `src/dsp/psk/` — PSK31; `Psk31Modem.{h,cpp}` is where squelch and TX
  preamble logic live, `Psk31Constants.h` for the tunable values.
- `src/dsp/DspPipeline.{h,cpp}` — thin Qt-facing glue; gained
  `textCharacterReceived()` signal and squelch pass-through this
  session.
- `src/radio/HamlibClient.{h,cpp}` — real implementation now, not a
  stub; `cmake/FindHamlib.cmake` for the build-time discovery logic that
  had the four bugs described above.
- `src/ui/RxDisplay.{h,cpp}` — gained `appendStreamingText()`/
  `endStreamingLine()` for continuous-stream RX modes.

## Known Verified-Correct Items (do not re-investigate without new evidence)

Everything in the prior RESUME.md's list still holds. Additionally,
confirmed this session:
- Hamlib's real struct tag is `s_rig` (`typedef struct s_rig RIG;`), not
  `struct rig` — confirmed against the actual header, not assumed.
- fldigi's PSK31 TX preamble is exactly 32 symbols of continuous
  phase-reversal at 31.25 baud (`dcdbits` in `src/psk/psk.cxx`, scales
  to 64/128 for PSK63/125) — confirmed by fetching and reading that
  source directly, not assumed from general PSK31 knowledge.
- A `.lib` file living in a directory named `lib/gcc/` is not
  necessarily GNU-ld-native format — Hamlib's own SDK ships both a
  `.lib` (MSVC-format) and `.dll.a` (GNU-format) side by side there, and
  CMake's `find_library()` NAMES-based search can silently pick the
  wrong one with no error at link time.
