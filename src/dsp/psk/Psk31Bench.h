#pragma once
#include "Psk31Modem.h"
#include "../Constants.h"
#include "../WeakSignalBench.h"   // reuse bench::logln / bench::addAwgn / bench::measureRms
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

// PSK31 weak-signal benchmark — the yardstick for the PSK31 RX hardening
// work (narrowband DCD, matched filter, normalized tracking loops,
// squelch metric). Same conventions as WeakSignalBench.h so numbers are
// comparable across modes:
//
//   * AWGN SNR referenced to a 2500 Hz bandwidth (amateur weak-signal
//     convention). For scale: mature PSK31 implementations decode
//     reliably down to roughly -10 dB on this scale; RTTY dies around
//     -5 dB.
//   * RX runs through Psk31Modem::processAudioChunk() in
//     AUDIO_CHUNK_SAMPLES chunks — the exact live path, DCD/squelch
//     gating included.
//
// Two measurements, matching the two field symptoms (2026-07-13):
//
//   1. CER vs SNR — Levenshtein distance between the decoded character
//      stream and the sent text, normalized by message length. Captures
//      missed characters AND garbage insertions during a real decode.
//   2. Noise-only garbage rate — characters emitted per minute of pure
//      band noise with no signal present. A correct DCD/squelch should
//      hold this at (or very near) zero.
//
// Every improvement phase must beat the previous baseline on these
// sweeps or it doesn't ship (same rule as the MFSK campaign, ADR-129/130).
// Invoke with:  HavenFSK.exe --bench-psk31 [trials]
// Results go to stdout AND psk31_bench.txt in the working directory.

namespace HavenFSK {
namespace psk31bench {

// Plain O(n*m) Levenshtein — messages are tens of characters, cost is nil.
inline int levenshtein(const std::string& a, const std::string& b) {
    const size_t n = a.size(), m = b.size();
    std::vector<int> prev(m + 1), cur(m + 1);
    for (size_t j = 0; j <= m; j++) prev[j] = (int)j;
    for (size_t i = 1; i <= n; i++) {
        cur[0] = (int)i;
        for (size_t j = 1; j <= m; j++) {
            int sub = prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, sub});
        }
        std::swap(prev, cur);
    }
    return prev[m];
}

// One TX->AWGN->RX round trip through the chunked live path.
// Returns the full decoded character stream (squelch left at the modem's
// default). `signalScale` sets the peak signal level fed to RX, exercising
// the level-dependence of the DCD and tracking loops (1.0 = modulator
// full scale, as a loud, well-adjusted RX chain would deliver).
inline std::string runTrial(const std::vector<float>& txAudio,
                            float snrDb, float signalScale, uint32_t seed)
{
    std::mt19937 rng(seed);

    std::vector<float> full((size_t)SAMPLE_RATE, 0.0f);   // 1 s noise lead-in
    full.insert(full.end(), txAudio.begin(), txAudio.end());
    full.resize(full.size() + (size_t)SAMPLE_RATE, 0.0f); // 1 s noise tail
    if (signalScale != 1.0f)
        for (float& s : full) s *= signalScale;

    float rms = bench::measureRms(txAudio) * signalScale;
    bench::addAwgn(full, rms, snrDb, rng);

    Psk31Modem rx;
    std::string decoded;
    for (size_t off = 0; off < full.size(); off += AUDIO_CHUNK_SAMPLES) {
        size_t end = std::min(full.size(), off + (size_t)AUDIO_CHUNK_SAMPLES);
        std::vector<float> chunk(full.begin() + off, full.begin() + end);
        chunk.resize(AUDIO_CHUNK_SAMPLES, 0.0f);
        for (const auto& ev : rx.processAudioChunk(chunk))
            if (ev.hasMessage) decoded += ev.text;
    }
    return decoded;
}

// TX audio with impairments a live station always has and plain loopback
// never does: a carrier that is NOT exactly at the demodulator's fixed
// 1000 Hz (operators tune by eye on a waterfall), and a TX sample clock
// that differs from RX by tens of ppm (two independent soundcards).
inline std::vector<float> makeImpairedTx(const std::string& msg,
                                         double carrierOffsetHz,
                                         double clockPpm)
{
    std::vector<bool> bits(
        static_cast<size_t>(psk31PreambleSymbols(PSK31_DEFAULT_BAUD)), false);
    auto textBits = Varicode::encode(msg);
    bits.insert(bits.end(), textBits.begin(), textBits.end());
    std::vector<bool> post(
        static_cast<size_t>(psk31PreambleSymbols(PSK31_DEFAULT_BAUD)), true);
    bits.insert(bits.end(), post.begin(), post.end());

    Psk31Modulator mod(PSK31_DEFAULT_BAUD, PSK31_CARRIER_HZ + carrierOffsetHz);
    auto audio = mod.modulateBits(bits);

    if (clockPpm != 0.0) {
        // Model TX/RX sample-clock skew by linear-interp resampling.
        double ratio = 1.0 + clockPpm * 1e-6;
        std::vector<float> out;
        out.reserve(audio.size());
        for (double pos = 0.0; pos + 1.0 < (double)audio.size(); pos += ratio) {
            size_t k = (size_t)pos;
            double frac = pos - (double)k;
            out.push_back((float)((1.0 - frac) * audio[k] + frac * audio[k + 1]));
        }
        return out;
    }
    return audio;
}

// Characters emitted across `seconds` of pure Gaussian noise (no signal).
// `noiseRms` models receiver band noise at typical audio drive — chosen
// well above Psk31Modem's DCD_RMS_THRESHOLD, as real SSB band noise is.
inline int noiseOnlyChars(float noiseRms, float seconds, uint32_t seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<float> gauss(0.0f, noiseRms);
    std::vector<float> buf((size_t)(seconds * SAMPLE_RATE));
    for (float& s : buf) s = gauss(rng);

    Psk31Modem rx;
    int chars = 0;
    for (size_t off = 0; off < buf.size(); off += AUDIO_CHUNK_SAMPLES) {
        size_t end = std::min(buf.size(), off + (size_t)AUDIO_CHUNK_SAMPLES);
        std::vector<float> chunk(buf.begin() + off, buf.begin() + end);
        chunk.resize(AUDIO_CHUNK_SAMPLES, 0.0f);
        for (const auto& ev : rx.processAudioChunk(chunk))
            if (ev.hasMessage) chars += (int)ev.text.size();
    }
    return chars;
}

} // namespace psk31bench

inline bool runPsk31Bench(int trialsPerPoint = 10) {
    using namespace psk31bench;
    using bench::logln;
    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();

    bench::g_out = fopen("psk31_bench.txt", "w");
    logln("=== HAVEN PSK31 Weak-Signal Benchmark ===");
    logln("trials per point: %d", trialsPerPoint);

    Psk31Modem tx;
    const std::string msg = "CQ CQ DE WD9N WD9N PSE K";
    auto txAudio = tx.modulateText(msg);
    if (txAudio.empty()) {
        logln("FAIL: modulateText returned empty audio");
        if (bench::g_out) { fclose(bench::g_out); bench::g_out = nullptr; }
        return false;
    }
    logln("frame: %.1f s of audio, message '%s'",
          txAudio.size() / (double)SAMPLE_RATE, msg.c_str());

    // Sanity: clean full-scale loopback must be near-perfect or nothing
    // below means anything.
    {
        std::string decoded = runTrial(txAudio, 99.0f, 1.0f, 1);
        if (decoded.find(msg) == std::string::npos) {
            logln("FAIL: clean loopback did not contain message (got \"%s\")",
                  decoded.c_str());
            if (bench::g_out) { fclose(bench::g_out); bench::g_out = nullptr; }
            return false;
        }
        logln("clean loopback: PASS");
    }

    // ── Sweep 1: CER vs SNR ─────────────────────────────────────────────
    // Two signal levels: full scale (1.0) and -20 dB (0.1) — the tracking
    // loops' error terms scale with amplitude, so weak-signal behavior at
    // realistic RX levels is part of what's under test.
    logln("");
    logln("-- AWGN (SNR in 2500 Hz ref BW), CER %% = Levenshtein/msgLen --");
    const float snrPoints[]  = {15.0f, 10.0f, 5.0f, 0.0f, -5.0f, -10.0f};
    const float sigLevels[]  = {1.0f, 0.1f};
    for (float lvl : sigLevels) {
        logln("  signal peak scale %.1f:", (double)lvl);
        for (float snr : snrPoints) {
            double cerSum = 0.0;
            int perfect = 0;
            for (int t = 0; t < trialsPerPoint; t++) {
                uint32_t seed = 2000u + (uint32_t)((snr + 20.0f) * 10.0f)
                              + (uint32_t)(lvl * 100.0f)
                              + (uint32_t)t * 7919u;
                std::string decoded = runTrial(txAudio, snr, lvl, seed);
                int dist = levenshtein(decoded, msg);
                double cer = (double)dist / (double)msg.size();
                cerSum += cer;
                if (dist == 0) perfect++;
                else if (snr >= 0.0f)   // high-SNR misses are bugs, not noise — show them
                    logln("    [t%02d dist %d] \"%s\"", t, dist, decoded.c_str());
            }
            logln("  SNR %+5.1f dB : CER %6.1f%%  (%d/%d perfect)",
                  snr, 100.0 * cerSum / trialsPerPoint,
                  perfect, trialsPerPoint);
        }
    }

    // ── Sweep 1b: real-world impairments at comfortable SNR ────────────
    // A live station is never exactly at the demodulator's fixed 1000 Hz
    // carrier, and its soundcard clock differs from ours. Both stress the
    // Costas pull-in and the +-1-sample timing nudge in ways plain
    // loopback never does — this sweep is the offline stand-in for the
    // "decodes unreliable on a clean on-air signal" field symptom.
    logln("");
    logln("-- Impairments at +10 dB SNR, full scale --");
    const float offsetsHz[] = {2.0f, 5.0f, 10.0f, 20.0f, 40.0f, -5.0f, -10.0f};
    const float offsetSnrs[] = {10.0f, -5.0f};   // strong AND weak off-tuned station
    for (float osnr : offsetSnrs) {
        for (float off : offsetsHz) {
            auto txImp = makeImpairedTx(msg, off, 0.0);
            double cerSum = 0.0; int perfect = 0;
            for (int t = 0; t < trialsPerPoint; t++) {
                std::string decoded = runTrial(txImp, osnr, 1.0f,
                                               9000u + (uint32_t)((off + 30.0f) * 10.0f)
                                                     + (uint32_t)((osnr + 20.0f) * 100.0f)
                                                     + (uint32_t)t * 7919u);
                int dist = levenshtein(decoded, msg);
                cerSum += (double)dist / (double)msg.size();
                if (dist == 0) perfect++;
            }
            logln("  offset %+6.1f Hz @ %+5.1f dB : CER %6.1f%%  (%d/%d perfect)",
                  off, osnr, 100.0 * cerSum / trialsPerPoint, perfect, trialsPerPoint);
        }
    }
    // Mid-carrier retune: the operator turns the dial 20 Hz while the
    // station keeps transmitting — DCD never drops, so there is no
    // fresh acquisition. The demodulator's re-lock path (lock collapse
    // + rolling carrier re-estimate) must pick the station back up.
    // Second copy's own 32-symbol preamble covers the ~0.4 s re-lock,
    // so near-full decode of BOTH copies is the pass criterion.
    {
        auto tx1 = makeImpairedTx(msg, 0.0, 0.0);
        auto tx2 = makeImpairedTx(msg, 20.0, 0.0);
        std::vector<float> spliced(tx1);
        spliced.insert(spliced.end(), tx2.begin(), tx2.end());
        const std::string both = msg + msg;
        double cerSum = 0.0; int perfect = 0;
        for (int t = 0; t < trialsPerPoint; t++) {
            std::string decoded = runTrial(spliced, 10.0f, 1.0f,
                                           13000u + (uint32_t)t * 7919u);
            int dist = levenshtein(decoded, both);
            cerSum += (double)dist / (double)both.size();
            if (dist == 0) perfect++;
        }
        logln("  retune 0->+20 Hz mid-carrier : CER %6.1f%%  (%d/%d perfect, both copies)",
              100.0 * cerSum / trialsPerPoint, perfect, trialsPerPoint);
    }

    const float skewsPpm[] = {50.0f, 100.0f, 200.0f, -100.0f};
    for (float ppm : skewsPpm) {
        auto txImp = makeImpairedTx(msg, 0.0, ppm);
        double cerSum = 0.0; int perfect = 0;
        for (int t = 0; t < trialsPerPoint; t++) {
            std::string decoded = runTrial(txImp, 10.0f, 1.0f,
                                           11000u + (uint32_t)(ppm + 500.0f)
                                                  + (uint32_t)t * 7919u);
            int dist = levenshtein(decoded, msg);
            cerSum += (double)dist / (double)msg.size();
            if (dist == 0) perfect++;
        }
        logln("  clock skew   %+6.0f ppm : CER %6.1f%%  (%d/%d perfect)",
              ppm, 100.0 * cerSum / trialsPerPoint, perfect, trialsPerPoint);
    }

    // ── Sweep 2: garbage characters on pure noise ───────────────────────
    // 60 s of band noise per trial at three levels spanning quiet to loud
    // audio drive. Target: ~0 chars/min once DCD/squelch are fixed.
    logln("");
    logln("-- Noise-only garbage (chars per 60 s, no signal present) --");
    const float noiseLevels[] = {0.02f, 0.05f, 0.15f};
    for (float nl : noiseLevels) {
        int total = 0;
        for (int t = 0; t < trialsPerPoint; t++)
            total += noiseOnlyChars(nl, 60.0f,
                                    6000u + (uint32_t)(nl * 1000.0f)
                                          + (uint32_t)t * 104729u);
        logln("noise RMS %.2f : %.1f chars/min avg",
              (double)nl, (double)total / trialsPerPoint);
    }

    double secs = std::chrono::duration<double>(clock::now() - t0).count();
    logln("");
    logln("=== PSK31 benchmark complete in %.0f s ===", secs);
    if (bench::g_out) { fclose(bench::g_out); bench::g_out = nullptr; }
    return true;
}

} // namespace HavenFSK
