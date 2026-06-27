#include "Preamble.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace HavenFSK {

Preamble::Preamble()
{
    // No pre-building — generate() computes fresh CPFSK audio on each call
}

std::vector<float> Preamble::generate()
{
    std::vector<float> audio;
    audio.reserve(PREAMBLE_LENGTH * SAMPLES_PER_SYMBOL);

    double phase = 0.0;  // continuous phase accumulator

    for (int s = 0; s < PREAMBLE_LENGTH; ++s) {
        int sym = PREAMBLE_SYMBOLS[s];
        double freq     = BASE_FREQ + sym * SYMBOL_RATE;
        double phaseInc = 2.0 * M_PI * freq
                        / static_cast<double>(SAMPLE_RATE);

        std::vector<float> block(SAMPLES_PER_SYMBOL);

        for (int i = 0; i < SAMPLES_PER_SYMBOL; i++) {
            block[i] = static_cast<float>(std::sin(phase));
            phase += phaseInc;
            // Wrap to [-π, π] to prevent float drift over 16 symbols
            if (phase >  M_PI) phase -= 2.0 * M_PI;
            if (phase < -M_PI) phase += 2.0 * M_PI;
        }

        // Ramps removed: CPFSK has no phase discontinuities — ramps
        // would create amplitude dips at 31.25 Hz (the pulsing artifact).
        audio.insert(audio.end(), block.begin(), block.end());
    }

    // Store final phase — Frame::assemble() seeds the Modulator with this
    // so the header section starts with continuous phase from the preamble
    m_finalPhase = phase;

    // Normalize to TX_AMPLITUDE
    float peak = 0.0f;
    for (float s : audio) peak = std::max(peak, std::abs(s));
    if (peak > 0.0f) {
        float scale = static_cast<float>(TX_AMPLITUDE) / peak;
        for (float& s : audio) s *= scale;
    }

    return audio;
}

std::vector<int> Preamble::hardDecisions(
    const std::vector<std::vector<float>>& softSymbols) const
{
    std::vector<int> decisions;
    decisions.reserve(softSymbols.size());
    for (const auto& energies : softSymbols) {
        int best = static_cast<int>(
            std::max_element(energies.begin(), energies.end())
            - energies.begin());
        decisions.push_back(best);
    }
    return decisions;
}

float Preamble::correlate(
    const std::vector<int>& symbols, int offset) const
{
    int remaining = static_cast<int>(symbols.size()) - offset;
    if (remaining < PREAMBLE_LENGTH) return 0.0f;

    int matches = 0;
    for (int i = 0; i < PREAMBLE_LENGTH; ++i) {
        if (symbols[offset + i] == PREAMBLE_SYMBOLS[i])
            ++matches;
    }
    return static_cast<float>(matches) / PREAMBLE_LENGTH;
}

float Preamble::softCorrelate(
    const std::vector<std::vector<float>>& softSymbols, int offset) const
{
    int remaining = static_cast<int>(softSymbols.size()) - offset;
    if (remaining < PREAMBLE_LENGTH) return 0.0f;

    float totalScore = 0.0f;
    for (int i = 0; i < PREAMBLE_LENGTH; ++i) {
        const auto& energies = softSymbols[offset + i];
        float total = 0.0f;
        for (float e : energies) total += e;
        if (total < 1e-10f) continue;
        totalScore += energies[PREAMBLE_SYMBOLS[i]] / total;
    }
    return totalScore / PREAMBLE_LENGTH;
}

bool Preamble::detect(
    const std::vector<std::vector<float>>& softSymbols,
    int& matchOffset) const
{
    auto symbols = hardDecisions(softSymbols);

    int   bestOffset = 0;
    float bestScore  = 0.0f;

    int n = static_cast<int>(symbols.size());
    for (int offset = 0; offset <= n - PREAMBLE_LENGTH; ++offset) {
        float score = correlate(symbols, offset);
        if (score > bestScore) {
            bestScore = score;
            bestOffset = offset;
        }
    }

    matchOffset = bestOffset;
    constexpr float threshold =
        static_cast<float>(PREAMBLE_THRESHOLD) / PREAMBLE_LENGTH;
    return bestScore >= threshold;
}

} // namespace HavenFSK
