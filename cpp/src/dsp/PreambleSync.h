#pragma once
#include "SlidingDft.h"
#include "MfskConstants.h"
#include <vector>

namespace HavenFSK {

struct PreambleLock {
    // 0-based sample index (since this PreambleSync's internal counter
    // started, i.e. since construction or the last reset()) of the FIRST
    // sample of the preamble's first symbol.
    long long sampleIndex   = 0;
    int   binShift          = 0;    // matched tone-bin frequency shift
    float freqOffsetHz      = 0.0f; // binShift * SYMBOL_RATE
    float score             = 0.0f; // softCorrelate-style score in [0,1]
};

// Continuous preamble detector built on a per-sample SlidingDft, replacing
// a discrete-offset block-FFT search (see DECISIONS.md ADR-105 for why).
// Because the sliding DFT refreshes every tracked tone bin's energy after
// every single sample, this checks EVERY possible symbol-timing alignment
// as it becomes available — there is no gap between tested alignments the
// way there was with a sparse coarse timing grid (only 2 candidates before
// a threshold gate decided whether to even try the fine sweep).
//
// Bin resolution is exactly SYMBOL_RATE (48000/1536 = 31.25 Hz/bin) with
// no zero-padding needed, since a windowLen == SAMPLES_PER_SYMBOL sliding
// DFT naturally puts one bin exactly on every HAVEN tone.
class PreambleSync {
public:
    PreambleSync();

    // Feed one sample (already RX-gain-corrected). The correlation score
    // is a smooth-ish function of timing alignment that rises to a sharp
    // peak at the true symbol boundary and falls off either side (there
    // are no guard bins in this exact-bin scoring, so misalignment costs
    // real score) — so this does NOT fire on the first sample where score
    // crosses SCORE_THRESHOLD (that's still on the rising edge, not the
    // best alignment). Instead it tracks the run of consecutive samples
    // above threshold and returns true, with 'lock' filled from the peak
    // of that run, once the run ends (score drops, either by falling back
    // below threshold or by the local maximum being confirmed passed).
    // Safe to keep calling after a lock (e.g. while the caller is busy
    // collecting a frame) — it just keeps tracking in the background; the
    // caller decides whether to act on the result.
    bool pushSample(float sample, PreambleLock& lock);

    // Most recent correlation score computed (regardless of threshold) —
    // for periodic "how close are we" diagnostics, not decode logic.
    float lastScore() const { return m_lastScore; }

    void reset();

    static constexpr float SCORE_THRESHOLD = 0.45f;

private:
    // Tone 0 sits exactly at bin BASE_FREQ/SYMBOL_RATE = 500/31.25 = 16 —
    // exact because both are exact multiples of the bin width by
    // construction (see MfskConstants.h). AFC_MARGIN_BINS extends the
    // tracked range on both sides so the same continuous bin range covers
    // every AFC frequency-shift hypothesis without recomputing anything.
    static constexpr int TONE_BIN_BASE   =
        static_cast<int>(BASE_FREQ / SYMBOL_RATE + 0.5);
    static constexpr int AFC_MARGIN_BINS = 7;  // ~219 Hz, covers +/-200 Hz AFC range
    static constexpr int FIRST_BIN = TONE_BIN_BASE - AFC_MARGIN_BINS;
    static constexpr int LAST_BIN  = TONE_BIN_BASE + AFC_MARGIN_BINS + NUM_TONES;
    static constexpr int NUM_BINS  = LAST_BIN - FIRST_BIN;

    // Ring buffer depth: one full preamble plus a little margin.
    static constexpr int HISTORY_LEN =
        PREAMBLE_LENGTH * SAMPLES_PER_SYMBOL + SAMPLES_PER_SYMBOL;

    SlidingDft m_dft;
    std::vector<std::vector<float>> m_history; // [HISTORY_LEN][NUM_BINS]
    long long m_sampleCounter = 0;
    float m_lastScore = 0.0f;

    // Peak-picking state for the current above-threshold run (see
    // pushSample's comment) — mirrors fldigi's synchronize(), which scans
    // for the true peak over a window rather than acting on the first
    // sample that looks plausible.
    bool         m_inRun        = false;
    float        m_runBestScore = 0.0f;
    PreambleLock m_runBestLock;
    int          m_runLength    = 0;

    // Safety cap on how long a run is allowed to stay above threshold
    // before being force-finalized — bounds worst-case lock latency if a
    // real signal's correlation somehow stays elevated for an unusually
    // long stretch instead of dropping off shortly after the true peak.
    static constexpr int MAX_RUN_SAMPLES = 2 * SAMPLES_PER_SYMBOL;
};

} // namespace HavenFSK
