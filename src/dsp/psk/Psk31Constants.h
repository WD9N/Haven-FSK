#pragma once

// PSK31 mode parameters. Qt-free, std:: only (ADR-003). Kept separate
// from MfskConstants.h so the two modes can never accidentally share a
// constant that happens to have the same name but a different meaning.

namespace HavenFSK {

// ── Baud rate variants ──────────────────────────────────────────
// PSK31/63/125 by amateur convention — same modem, different symbol rate.
constexpr double PSK31_BAUD_31  = 31.25;
constexpr double PSK31_BAUD_63  = 62.5;
constexpr double PSK31_BAUD_125 = 125.0;
constexpr double PSK31_DEFAULT_BAUD = PSK31_BAUD_31;

// ── Carrier ────────────────────────────────────────────────────
// Default audio carrier center frequency. Operator-tunable in principle;
// fixed here for the initial implementation (matches common PSK31
// waterfall convention of tuning near 1 kHz audio).
constexpr double PSK31_CARRIER_HZ = 1000.0;

// ── Costas loop (carrier phase/frequency tracking) ────────────────
// Decision-directed BPSK Costas loop gains, applied once per SYMBOL to
// a magnitude-normalized error (~sin of the phase error, level-
// independent — see Psk31Demodulator::applyCostasCorrection). Still a
// deliberately slow loop by classic-PLL standards — PSK31's narrow
// bandwidth tolerates it and a slow loop resists false-locking on
// noise — but fast enough to converge within the 32-symbol preamble
// (the old unnormalized 0.002/0.00002 moved microradians per symbol at
// real signal levels: effectively frozen).
// BETA acts on the NCO's per-SAMPLE increment once per symbol, so its
// scale is alpha^2/4 (critically damped, per-symbol units) divided by
// samples-per-symbol: (0.05^2/4)/1536 ≈ 4e-7. Rounded up a little for
// livelier drift tracking; each unit here is ~7.6 kHz/symbol of slew,
// so 1e-6 ≈ 8 mHz/symbol ≈ 0.24 Hz/s — ample for HF oscillator drift.
constexpr double PSK31_COSTAS_ALPHA = 0.05;   // phase gain, rad/symbol per unit error
constexpr double PSK31_COSTAS_BETA  = 1e-6;   // frequency gain, rad/sample per symbol

// ── Symbol timing recovery ─────────────────────────────────────
// Gardner timing-error-detector loop gain.
constexpr double PSK31_TIMING_GAIN = 0.01;

// ── Matched filter ─────────────────────────────────────────────
// Raised-cosine roll-off factor for the RX matched filter / integrate
// window shape — mirrors the TX raised-cosine constellation-transition
// shaping (see Psk31Modulator), which is itself the correct matched
// filter for this pulse shape.
constexpr double PSK31_RRC_ROLLOFF = 0.35;

// ── Passband ───────────────────────────────────────────────────
// PSK31's occupied bandwidth is roughly 1.5x the baud rate (narrowband).
constexpr double psk31PassbandHalfWidthHz(double baud) {
    return baud * 1.5;
}

// ── TX preamble ────────────────────────────────────────────────
// Symbol count for the leading phase-reversal preamble sent before any
// real data — matches fldigi's PSK31/63/125 "dcdbits" table exactly
// (src/psk/psk.cxx: MODE_PSK31->32, MODE_PSK63->64, MODE_PSK125->128),
// confirmed by fetching and reading that source this session. This is
// NOT unmodulated/silent carrier: it's dcdbits repetitions of bit=0
// (continuous 180-degree phase reversals — the standard PSK31
// convention), which is what gives a receiving station's Costas loop,
// AGC, and bit/symbol timing recovery something to lock onto before
// real data arrives. Without it, the first several characters of a
// transmission are typically lost — confirmed via real interop testing
// against fldigi (HAVEN TX -> fldigi RX dropped the opening characters
// until this was added).
constexpr int psk31PreambleSymbols(double baud) {
    return (baud >= PSK31_BAUD_125) ? 128
         : (baud >= PSK31_BAUD_63)  ? 64
         : 32;
}

// ── Squelch ────────────────────────────────────────────────────
// Runtime-adjustable, not a fixed constant here — see
// Psk31Modem::setSquelchThreshold() / IModem::setSquelchThreshold().
// A decoded character is only surfaced to the UI if the average lock
// quality (Psk31Demodulator::Result::lockQuality, 0..1) across the bits
// that made it up meets the configured threshold. The metric is the
// smoothed doubled-differential-phase coherence: near 1.0 on a locked
// signal regardless of level, random-walking near ~0.2 on noise, so
// 0.5 (the default) separates them with margin on both sides. History:
// the original per-bit |dI|/mag metric sat at ~0.64 in expectation on
// pure noise — no threshold could pass real drifting signals while
// rejecting noise, which is why the default was once 0.0 (off) and an
// early 0.7 default silently suppressed real over-the-air decodes that
// fldigi handled fine. The narrowband DCD remains the primary noise
// defense; this is the per-character refinement behind it.

} // namespace HavenFSK
