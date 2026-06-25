#pragma once
#include <cstdint>

namespace HavenFSK {

// ── Sample rate ────────────────────────────────────────────────
// Default 48000 Hz — matches modern USB radio audio interfaces.
// Configurable; all derived values recalculated when changed.
constexpr int    SAMPLE_RATE         = 48000;

// ── MFSK parameters ────────────────────────────────────────────
constexpr int    NUM_TONES           = 16;
constexpr double SYMBOL_RATE         = 31.25;     // Hz = baud
constexpr double BASE_FREQ           = 500.0;     // Hz, lowest tone
constexpr int    BITS_PER_SYMBOL     = 4;         // log2(NUM_TONES)

// ── Derived parameters ─────────────────────────────────────────
constexpr int    SAMPLES_PER_SYMBOL  = static_cast<int>(
                                       SAMPLE_RATE / SYMBOL_RATE);
                                       // = 1536 at 48000 Hz

// ── Tone frequencies ───────────────────────────────────────────
// Tone N is at BASE_FREQ + N * SYMBOL_RATE Hz
// Tone  0:  500.00 Hz
// Tone  1:  531.25 Hz
// ...
// Tone 15:  968.75 Hz

// ── Preamble ───────────────────────────────────────────────────
constexpr int PREAMBLE_LENGTH        = 16;
constexpr int PREAMBLE_SYMBOLS[16]   = {
    0,15,0,15,7,8,7,8,0,15,0,15,7,8,7,8
};
constexpr double PREAMBLE_THRESHOLD  = 6.0;  // minimum exact symbol matches (out of 16) → score >= 0.375

// ── FEC ────────────────────────────────────────────────────────
constexpr int LDPC_N                 = 192;  // coded bits
constexpr int LDPC_K                 = 96;   // data bits
constexpr int LDPC_M                 = LDPC_N - LDPC_K;  // 96 parity bits
constexpr int LDPC_BYTES_PER_BLOCK   = 12;   // LDPC_K / 8
constexpr int LDPC_PEG_SEED          = 1234;
constexpr int LDPC_MAX_ITERATIONS    = 200;
constexpr double LDPC_SCALE_FACTOR   = 0.75;

// ── CRC-16 ─────────────────────────────────────────────────────
constexpr uint16_t CRC16_POLY        = 0x1021;
constexpr uint16_t CRC16_INIT        = 0xFFFF;

// ── TX parameters ──────────────────────────────────────────────
constexpr double TX_AMPLITUDE        = 1.0;   // peak, 0 dBFS — GainedAudioDevice is sole gain control
constexpr int    PTT_WATCHDOG_SEC    = 120;   // max TX time

// ── DCD parameters ─────────────────────────────────────────────
constexpr double DCD_THRESHOLD_DB    = 12.0;
constexpr double DCD_FREQ_LOW        = 450.0;
constexpr double DCD_FREQ_HIGH       = 1050.0;
constexpr int    DCD_HOLDOFF_CHUNKS  = 4;

// ── FFT parameters ─────────────────────────────────────────────
// 8x zero-padding per spec §3.5
// FFT_SIZE = 1536 * 8 = 12288
// Bin width = SAMPLE_RATE / FFT_SIZE = 48000 / 12288 ≈ 3.906 Hz/bin
// Guard window ±3 bins = ±11.7 Hz — sufficient for HF drift tolerance
constexpr int FFT_ZERO_PAD_FACTOR    = 8;
constexpr int FFT_SIZE               = SAMPLES_PER_SYMBOL * FFT_ZERO_PAD_FACTOR;
constexpr int FFT_GUARD_BINS         = 3;

// ── Raised cosine shaping ──────────────────────────────────────
// 10% of symbol duration, minimum 4 samples
constexpr int RAMP_SAMPLES           = (SAMPLES_PER_SYMBOL / 10 < 4)
                                       ? 4 : SAMPLES_PER_SYMBOL / 10;
// = 153 at 48000 Hz

// ── Audio ──────────────────────────────────────────────────────
constexpr int    AUDIO_CHUNK_SAMPLES = 2048;
constexpr int    AUDIO_CHANNELS      = 1;    // mono

// ── Application ────────────────────────────────────────────────
constexpr const char* APP_NAME       = "HAVEN-FSK";
constexpr const char* APP_VERSION    = "0.2.0-beta";
constexpr const char* EMISSION_DESIG = "500HJ2D";
constexpr const char* SPEC_URL       =
    "https://github.com/WD9N/Haven-FSK";

} // namespace HavenFSK
