#pragma once
#include "MfskModem.h"
#include "Constants.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

// Weak-signal / impulse-noise benchmark — the yardstick for the RX
// hardening work (erasure marking, LLR scaling, noise blanker, retry
// decoding). Runs the same chunked live-RX path as
// MfskLoopbackSelfTest.h, but through calibrated impairments:
//
//   * AWGN at a specified SNR, referenced to a 2500 Hz bandwidth (the
//     amateur weak-signal convention, so numbers are comparable to
//     FT8/JS8 figures).
//   * Synthetic lightning crashes modeled on real HF static: an event
//     is 2-5 return strokes spread over 300-600 ms, each stroke a
//     20-60 ms burst of white noise with a 5-15 ms exponential decay,
//     20-30 dB above the signal. NOT tidy millisecond clicks — see the
//     2026-07-09 design discussion: distant-storm crashes as received
//     on HF are hundreds of ms end to end.
//
// Every improvement phase must beat the previous baseline on these
// sweeps or it doesn't ship. Invoke with:  HavenFSK.exe --bench [trials]
// Results go to stdout AND weak_signal_bench.txt in the working
// directory (the Release build is a GUI-subsystem exe — no console).
//
// Not part of the QT_DEBUG self-test battery: a full run is minutes of
// CPU, not something to pay on every Debug launch.

namespace HavenFSK {
namespace bench {

inline FILE* g_out = nullptr;

inline void logln(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("%s\n", buf);
    fflush(stdout);
    if (g_out) { fprintf(g_out, "%s\n", buf); fflush(g_out); }
}

struct Impairment {
    float snrDb          = 99.0f;  // SNR in 2500 Hz ref BW; 99 = no noise
    float crashesPerMin  = 0.0f;   // lightning events per minute
    float crashDb        = 25.0f;  // stroke peak above signal RMS
    uint32_t seed        = 1;
};

inline float measureRms(const std::vector<float>& v) {
    if (v.empty()) return 0.0f;
    double sum = 0.0;
    for (float s : v) sum += (double)s * s;
    return (float)std::sqrt(sum / v.size());
}

inline void addAwgn(std::vector<float>& buf, float signalRms,
                    float snrDb, std::mt19937& rng)
{
    if (snrDb >= 90.0f) return;
    // White noise spans the 24 kHz Nyquist band; only 2500/24000 of its
    // power lands in the reference bandwidth the SNR is defined against.
    float ps    = signalRms * signalRms;
    float sigma = std::sqrt(ps * (24000.0f / 2500.0f)
                            / std::pow(10.0f, snrDb / 10.0f));
    std::normal_distribution<float> gauss(0.0f, sigma);
    for (float& s : buf) s += gauss(rng);
}

inline void addCrashes(std::vector<float>& buf, float signalRms,
                       const Impairment& imp, std::mt19937& rng)
{
    if (imp.crashesPerMin <= 0.0f) return;
    double durSec = buf.size() / (double)SAMPLE_RATE;
    int nEvents = std::max(1, (int)std::lround(
        imp.crashesPerMin * durSec / 60.0));
    std::uniform_real_distribution<double> u(0.0, 1.0);
    std::normal_distribution<float> gauss(0.0f, 1.0f);
    float peak = signalRms * std::pow(10.0f, imp.crashDb / 20.0f);

    for (int e = 0; e < nEvents; e++) {
        double t0       = u(rng) * durSec;
        double span     = 0.3 + 0.3 * u(rng);       // event: 300-600 ms
        int    nStrokes = 2 + (int)(u(rng) * 4.0);  // 2-5 strokes
        for (int s = 0; s < nStrokes; s++) {
            double ts  = (s == 0) ? t0 : t0 + u(rng) * span;
            double tau = 0.005 + 0.010 * u(rng);    // decay: 5-15 ms
            double dur = 0.020 + 0.040 * u(rng);    // stroke: 20-60 ms
            size_t start = (size_t)(ts * SAMPLE_RATE);
            size_t n     = (size_t)(dur * SAMPLE_RATE);
            for (size_t i = 0; i < n && start + i < buf.size(); i++) {
                float env = peak * std::exp(
                    -(float)i / ((float)SAMPLE_RATE * (float)tau));
                buf[start + i] += env * gauss(rng);
            }
        }
    }
}

// One TX->impairment->RX round trip through the real chunked pipeline.
// Success = a CRC-verified decode whose text matches exactly.
// lockedOut (optional) reports whether preamble sync ever locked —
// distinguishing sync-bound failures (lock never happened; decoder
// improvements can't help) from decode-bound ones (locked but LDPC/CRC
// failed; decoder-side work is in play).
// syncThreshold 0 (default) = shipping configuration (adaptive threshold
// at its resting value); >0 pins a fixed threshold with adaptation OFF,
// for the --bench-sync trade study's stationary test points.
inline bool runTrial(const std::vector<float>& frameAudio,
                     const std::string& expectedText,
                     const Impairment& imp,
                     bool* lockedOut = nullptr,
                     float syncThreshold = 0.0f)
{
    std::mt19937 rng(imp.seed);

    // Lead-in long enough to fill the pretrigger buffer naturally (see
    // MfskLoopbackSelfTest.h) — and under impairment it carries noise,
    // as a real receive session would, so preamble sync is tested in
    // noise too, not just the payload.
    std::vector<float> full(SAMPLE_RATE * 4, 0.0f);
    full.insert(full.end(), frameAudio.begin(), frameAudio.end());
    full.resize(full.size() + SAMPLE_RATE, 0.0f);

    float rms = measureRms(frameAudio);
    addAwgn(full, rms, imp.snrDb, rng);
    addCrashes(full, rms, imp, rng);

    MfskModem rx;
    if (syncThreshold > 0.0f) {
        rx.setSyncAdaptation(false);
        rx.setSyncThreshold(syncThreshold);
    }
    bool decoded = false;
    for (size_t off = 0; off < full.size(); off += AUDIO_CHUNK_SAMPLES) {
        size_t end = std::min(full.size(), off + (size_t)AUDIO_CHUNK_SAMPLES);
        std::vector<float> chunk(full.begin() + off, full.begin() + end);
        chunk.resize(AUDIO_CHUNK_SAMPLES, 0.0f);
        for (const auto& ev : rx.processAudioChunk(chunk)) {
            if (ev.preambleDetected && lockedOut) *lockedOut = true;
            if (ev.hasMessage && ev.crcOk && ev.text == expectedText)
                decoded = true;
        }
    }
    return decoded;
}

// Pure-noise exposure: how many (false) preamble locks fire in `seconds`
// of Gaussian noise at the given sync threshold. Note a false lock puts
// the modem in Collecting (real behavior), where further locks are
// ignored until header rejection/timeout — so this measures operational
// false-lock impact, not raw correlator statistics.
inline int noiseOnlyLocks(float syncThreshold, float seconds, uint32_t seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<float> gauss(0.0f, 1.0f);
    std::vector<float> buf((size_t)(seconds * SAMPLE_RATE));
    for (float& s : buf) s = gauss(rng);

    MfskModem rx;
    rx.setSyncAdaptation(false);
    rx.setSyncThreshold(syncThreshold);
    int locks = 0;
    for (size_t off = 0; off < buf.size(); off += AUDIO_CHUNK_SAMPLES) {
        size_t end = std::min(buf.size(), off + (size_t)AUDIO_CHUNK_SAMPLES);
        std::vector<float> chunk(buf.begin() + off, buf.begin() + end);
        chunk.resize(AUDIO_CHUNK_SAMPLES, 0.0f);
        for (const auto& ev : rx.processAudioChunk(chunk))
            if (ev.preambleDetected) locks++;
    }
    return locks;
}

} // namespace bench

// Sensitivity vs false-lock trade study for PreambleSync's threshold —
// the input data for the adaptive-threshold design (task: the -9 dB
// wall; ADR-130 attributes every weak-signal failure to sync).
// Invoke with:  HavenFSK.exe --bench-sync [trials]
inline bool runSyncThresholdStudy(int trialsPerPoint = 10) {
    using namespace bench;
    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();

    g_out = fopen("weak_signal_bench.txt", "w");
    logln("=== HAVEN Sync Threshold Trade Study ===");
    logln("trials per point: %d", trialsPerPoint);

    MfskModem tx;
    const std::string msg = "CQ POTA DE WD9N K-1234 K";
    auto frameAudio = tx.modulateText(msg);

    const float thresholds[] = {0.45f, 0.40f, 0.35f, 0.30f};
    logln("");
    logln("threshold | -9 dB locks/dec | -10 dB locks/dec | false locks per 300 s noise");
    for (float thr : thresholds) {
        int l9 = 0, d9 = 0, l10 = 0, d10 = 0;
        for (int t = 0; t < trialsPerPoint; t++) {
            Impairment imp;
            imp.snrDb = -9.0f;
            imp.seed  = 9000u + (uint32_t)t * 7919u;
            bool locked = false;
            if (runTrial(frameAudio, msg, imp, &locked, thr)) d9++;
            if (locked) l9++;

            imp.snrDb = -10.0f;
            imp.seed  = 9500u + (uint32_t)t * 7919u;
            locked = false;
            if (runTrial(frameAudio, msg, imp, &locked, thr)) d10++;
            if (locked) l10++;
        }
        int falseLocks = 0;
        for (int t = 0; t < 10; t++)
            falseLocks += noiseOnlyLocks(thr, 30.0f, 7000u + (uint32_t)t * 104729u);

        logln("   %.2f   |     %2d / %2d     |      %2d / %2d     |   %d",
              thr, l9, d9, l10, d10, falseLocks);
    }

    double secs = std::chrono::duration<double>(clock::now() - t0).count();
    logln("");
    logln("=== Study complete in %.0f s ===", secs);
    if (g_out) { fclose(g_out); g_out = nullptr; }
    return true;
}

inline bool runWeakSignalBench(int trialsPerPoint = 10) {
    using namespace bench;
    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();

    g_out = fopen("weak_signal_bench.txt", "w");

    logln("=== HAVEN Weak-Signal Benchmark ===");
    logln("trials per point: %d", trialsPerPoint);

    MfskModem tx;
    const std::string msg = "CQ POTA DE WD9N K-1234 K";
    auto frameAudio = tx.modulateText(msg);
    if (frameAudio.empty()) {
        logln("FAIL: modulateText returned empty audio");
        return false;
    }
    logln("frame: %.1f s of audio, message '%s'",
          frameAudio.size() / (double)SAMPLE_RATE, msg.c_str());

    // Sanity: clean loopback must pass or nothing below means anything.
    {
        Impairment clean;
        if (!runTrial(frameAudio, msg, clean)) {
            logln("FAIL: clean (no-impairment) trial did not decode");
            return false;
        }
        logln("clean loopback: PASS");
    }

    // ── Sweep 1: AWGN threshold ──────────────────────────────────────────
    // First run (2026-07-09 baseline) showed 10/10 down to -6 dB and 0/10
    // at -9 dB — the cliff lives between them, hence the fine steps there.
    logln("");
    logln("-- AWGN (SNR in 2500 Hz ref BW) --");
    const float snrPoints[] = {0.0f, -3.0f, -6.0f, -7.0f, -8.0f, -9.0f};
    for (float snr : snrPoints) {
        int ok = 0, locks = 0;
        for (int t = 0; t < trialsPerPoint; t++) {
            Impairment imp;
            imp.snrDb = snr;
            imp.seed  = 1000u + (uint32_t)(snr * -10.0f) + (uint32_t)t * 7919u;
            bool locked = false;
            if (runTrial(frameAudio, msg, imp, &locked)) ok++;
            if (locked) locks++;
        }
        logln("SNR %+5.1f dB : %2d/%2d decoded  (%2d/%2d locked)",
              snr, ok, trialsPerPoint, locks, trialsPerPoint);
    }

    // ── Sweep 2: lightning crashes on a NEAR-THRESHOLD signal ────────────
    // Baseline calibration (2026-07-09/10): at +6 dB AND at -3 dB the
    // interleaver+clipper already shrug off up to 120 crashes/min
    // (9-10/10) — v1's crash story is genuinely strong. At -6 dB (2 dB of
    // margin): 9/10 @ 120, 8/10 @ 240, 4/10 @ 480 crashes/min. Per-symbol
    // erasure/de-weighting was tried against these numbers and REJECTED —
    // measured neutral at best, harmful when binary (see ADR-129): the
    // demodulator's energy normalization already self-erases crash-hit
    // symbols. The 480/min point stays as the stress case future work
    // (pre-demod blanker, retry decoding) must move.
    logln("");
    logln("-- Lightning crashes (SNR -6 dB, strokes %g dB above signal) --",
          (double)Impairment{}.crashDb);
    const float crashRates[] = {120.0f, 240.0f, 480.0f};
    for (float rate : crashRates) {
        int ok = 0, locks = 0;
        for (int t = 0; t < trialsPerPoint; t++) {
            Impairment imp;
            imp.snrDb         = -6.0f;
            imp.crashesPerMin = rate;
            imp.seed          = 5000u + (uint32_t)rate + (uint32_t)t * 104729u;
            bool locked = false;
            if (runTrial(frameAudio, msg, imp, &locked)) ok++;
            if (locked) locks++;
        }
        logln("%5.0f crashes/min : %2d/%2d decoded  (%2d/%2d locked)",
              rate, ok, trialsPerPoint, locks, trialsPerPoint);
    }

    double secs = std::chrono::duration<double>(clock::now() - t0).count();
    logln("");
    logln("=== Benchmark complete in %.0f s ===", secs);
    if (g_out) { fclose(g_out); g_out = nullptr; }
    return true;
}

} // namespace HavenFSK
