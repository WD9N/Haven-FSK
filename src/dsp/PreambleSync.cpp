#include "PreambleSync.h"
#include <algorithm>
#include <cmath>

namespace HavenFSK {

PreambleSync::PreambleSync()
    : m_dft(SAMPLES_PER_SYMBOL, FIRST_BIN, LAST_BIN)
{
    m_history.assign(HISTORY_LEN, std::vector<float>(NUM_BINS, 0.0f));
}

void PreambleSync::reset()
{
    m_dft.reset();
    for (auto& row : m_history) std::fill(row.begin(), row.end(), 0.0f);
    m_sampleCounter = 0;
    m_lastScore = 0.0f;
    m_inRun = false;
    m_runBestScore = 0.0f;
    m_runLength = 0;
    m_lastRunEndSample = -1;
}

bool PreambleSync::pushSample(float sample, PreambleLock& lock)
{
    m_dft.push(sample);

    if (!m_dft.isStable()) {
        ++m_sampleCounter;
        return false;
    }

    // Snapshot the current bin energies into the ring buffer at this
    // sample's slot — this row now represents "the DFT of the window
    // ending exactly here", available for any future correlation check
    // that needs this position as one of its 16 preamble-symbol taps.
    auto& row = m_history[static_cast<size_t>(m_sampleCounter % HISTORY_LEN)];
    for (int b = 0; b < NUM_BINS; ++b)
        row[static_cast<size_t>(b)] = m_dft.energyAt(FIRST_BIN + b);

    bool found = false;

    constexpr long long REACH_BACK =
        static_cast<long long>(PREAMBLE_LENGTH - 1) * SAMPLES_PER_SYMBOL;

    if (m_sampleCounter >= REACH_BACK) {
        float bestScore = 0.0f;
        int   bestShift = 0;

        for (int shift = -AFC_MARGIN_BINS; shift <= AFC_MARGIN_BINS; ++shift) {
            const int toneBin0 = TONE_BIN_BASE + shift - FIRST_BIN; // index into row[]

            float total = 0.0f;
            for (int i = 0; i < PREAMBLE_LENGTH; ++i) {
                const long long pos =
                    m_sampleCounter -
                    static_cast<long long>(PREAMBLE_LENGTH - 1 - i) * SAMPLES_PER_SYMBOL;
                const auto& r = m_history[static_cast<size_t>(pos % HISTORY_LEN)];

                float sum = 0.0f;
                for (int t = 0; t < NUM_TONES; ++t)
                    sum += r[static_cast<size_t>(toneBin0 + t)];

                const float e = r[static_cast<size_t>(toneBin0 + PREAMBLE_SYMBOLS[i])];
                total += (sum > 1e-10f) ? (e / sum) : 0.0f;
            }

            const float score = total / static_cast<float>(PREAMBLE_LENGTH);
            if (score > bestScore) {
                bestScore = score;
                bestShift = shift;
            }
        }

        m_lastScore = bestScore;

        if (bestScore >= SCORE_THRESHOLD) {
            // Track the best point seen anywhere in this above-threshold
            // run — the score curve isn't perfectly monotonic on the way
            // up (small sample-to-sample wobble), so committing on the
            // first local dip fires early on a mediocre point instead of
            // riding the climb to the true peak. Only the run *ending*
            // (score dropping back below threshold, or a safety cap on
            // run length) finalizes the lock.
            if (!m_inRun) {
                m_inRun = true;
                m_runLength = 0;
                // Only reset the high-water mark if enough samples have
                // passed since the last run ended to indicate this is a
                // genuinely new signal — not the same preamble's own
                // correlation dipping below threshold briefly mid-scan and
                // recovering (see RESET_GAP_SAMPLES's doc comment).
                if (m_lastRunEndSample < 0 ||
                    m_sampleCounter - m_lastRunEndSample > RESET_GAP_SAMPLES) {
                    m_runBestScore = 0.0f;
                }
            }
            ++m_runLength;

            if (bestScore > m_runBestScore) {
                m_runBestScore = bestScore;
                m_runBestLock.sampleIndex  = m_sampleCounter
                                   - static_cast<long long>(PREAMBLE_LENGTH) * SAMPLES_PER_SYMBOL
                                   + 1;
                m_runBestLock.binShift     = bestShift;
                m_runBestLock.freqOffsetHz =
                    static_cast<float>(bestShift) * static_cast<float>(SYMBOL_RATE);
                m_runBestLock.score        = bestScore;
            }

            if (m_runLength >= MAX_RUN_SAMPLES) {
                // A safety cap on an ongoing, still-above-threshold run,
                // not the run truly ending — the same real signal likely
                // continues past this point, and its true peak may still
                // be ahead. m_lastRunEndSample records where this happened
                // so the *next* run's start (almost certainly the very next
                // sample, still above threshold) sees a ~0 gap and keeps
                // the high-water mark instead of resetting — see
                // RESET_GAP_SAMPLES's doc comment.
                lock  = m_runBestLock;
                found = true;
                m_inRun = false;
                m_lastRunEndSample = m_sampleCounter;
            }
        } else if (m_inRun) {
            // Run truly ended (score genuinely dropped below threshold) —
            // emit its best point. The high-water mark itself is reset the
            // *next* time a run starts, only if enough samples have passed
            // since m_lastRunEndSample (see RESET_GAP_SAMPLES) — a short
            // gap means this is likely the same preamble's own correlation
            // recovering from a brief dip (confirmed necessary: a clean
            // loopback self-test locks in two runs a few hundred ms apart,
            // and needs the first run's high score preserved into the
            // second to find the true peak). A long gap means a genuinely
            // separate, later signal — resetting there prevents the field
            // bug this was built to fix: without any reset at all, a stale
            // m_runBestScore from one lock could block every future,
            // unrelated, genuinely-real preamble from ever registering,
            // confirmed via a growing-negative "out of pretrigger range"
            // offset over several minutes, tracking back to exactly the
            // last high-scoring lock.
            lock  = m_runBestLock;
            found = true;
            m_inRun = false;
            m_lastRunEndSample = m_sampleCounter;
        }
    }

    ++m_sampleCounter;
    return found;
}

} // namespace HavenFSK
