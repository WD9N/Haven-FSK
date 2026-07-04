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
                lock  = m_runBestLock;
                found = true;
                m_inRun = false;
            }
        } else if (m_inRun) {
            // Run ended — emit its best point.
            lock  = m_runBestLock;
            found = true;
            m_inRun = false;
        }
    }

    ++m_sampleCounter;
    return found;
}

} // namespace HavenFSK
