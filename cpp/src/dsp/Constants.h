#pragma once
#include <cstdint>

namespace HavenFSK {

// ── Sample rate ────────────────────────────────────────────────
// Default 48000 Hz — matches modern USB radio audio interfaces.
// The only valid rate; other rates break every mode's sample-per-symbol
// math (see CLAUDE.md "Key Invariants").
constexpr int    SAMPLE_RATE         = 48000;

// ── Audio ──────────────────────────────────────────────────────
constexpr int    AUDIO_CHUNK_SAMPLES = 2048;
constexpr int    AUDIO_CHANNELS      = 1;    // mono

// ── PTT ────────────────────────────────────────────────────────
// FCC Part 97 watchdog — not mode-specific, lives at the radio layer.
constexpr int    PTT_WATCHDOG_SEC    = 120;   // max TX time

// ── Application ────────────────────────────────────────────────
constexpr const char* APP_NAME       = "HAVEN-FSK";
constexpr const char* APP_VERSION    = "0.2.0-beta";
// MFSK-16's emission designator. Currently unreferenced elsewhere in the
// codebase; if ADIF export or similar later needs a per-mode designator
// (PSK31's would differ), source it from IModem rather than this constant.
constexpr const char* EMISSION_DESIG = "500HJ2D";
constexpr const char* SPEC_URL       =
    "https://github.com/WD9N/Haven-FSK";

} // namespace HavenFSK
