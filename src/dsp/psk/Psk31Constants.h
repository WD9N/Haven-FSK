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
// Decision-directed BPSK Costas loop gains. Conservative (slow) loop —
// PSK31's narrow bandwidth and low baud rate tolerate a slow loop well,
// and a slow loop is less prone to false-locking on noise than a fast one.
constexpr double PSK31_COSTAS_ALPHA = 0.002;   // phase correction gain
constexpr double PSK31_COSTAS_BETA  = 0.00002; // frequency correction gain

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
// A decoded character is only surfaced to the UI if the average Costas-
// loop lock quality (Psk31Demodulator::Result::lockQuality, 0..1) across
// the bits that made it up meets the configured threshold. Defaults to
// 0.0 (off): a fixed compile-time default of 0.7 was tried first and
// silently suppressed real over-the-air decodes entirely (confirmed —
// fldigi decoded the same signal fine, HAVEN's RX showed nothing) since
// real-world frequency drift/phase noise/timing jitter legitimately
// lowers lock quality even on correctly decoded characters, more than a
// noiseless loopback test revealed. lockQuality on pure noise isn't
// reliably near 0 either (it's |cos(random phase)| in expectation), so
// DCD gating — not lockQuality alone — is the primary noise defense;
// the lock-quality threshold is a secondary, operator-tuned refinement.

} // namespace HavenFSK
