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

} // namespace HavenFSK
