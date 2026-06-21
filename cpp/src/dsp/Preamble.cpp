#include "Preamble.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace HavenFSK {

Preamble::Preamble()
{
    buildPreambleAudio();
}

void Preamble::buildPreambleAudio()
{
    // Raised cosine ramps (same formula as Modulator)
    float rampUp[RAMP_SAMPLES];
    float rampDown[RAMP_SAMPLES];
    for (int i = 0; i < RAMP_SAMPLES; ++i)
        rampUp[i] = static_cast<float>(std::sin(M_PI / 2.0 * i / RAMP_SAMPLES));
    // rampUp must be fully populated before rampDown reads it
    for (int i = 0; i < RAMP_SAMPLES; ++i)
        rampDown[i] = rampUp[RAMP_SAMPLES - 1 - i];

    m_preambleAudio.reserve(PREAMBLE_LENGTH * SAMPLES_PER_SYMBOL);

    for (int s = 0; s < PREAMBLE_LENGTH; ++s) {
        int sym = PREAMBLE_SYMBOLS[s];
        double f = BASE_FREQ + sym * SYMBOL_RATE;

        std::vector<float> block(SAMPLES_PER_SYMBOL);
        for (int t = 0; t < SAMPLES_PER_SYMBOL; ++t)
            block[t] = static_cast<float>(std::sin(2.0 * M_PI * f * t / SAMPLE_RATE));

        // Apply raised cosine shaping
        for (int i = 0; i < RAMP_SAMPLES; ++i) {
            block[i]                           *= rampUp[i];
            block[SAMPLES_PER_SYMBOL - 1 - i]  *= rampDown[i];
        }

        m_preambleAudio.insert(m_preambleAudio.end(), block.begin(), block.end());
    }

    // Normalize to TX_AMPLITUDE
    float peak = 0.0f;
    for (float s : m_preambleAudio)
        peak = std::max(peak, std::abs(s));
    if (peak > 0.0f) {
        float scale = static_cast<float>(TX_AMPLITUDE) / peak;
        for (float& s : m_preambleAudio)
            s *= scale;
    }
}

std::vector<float> Preamble::generate() const
{
    return m_preambleAudio;
}

std::vector<int> Preamble::hardDecisions(
    const std::vector<std::vector<float>>& softSymbols) const
{
    std::vector<int> decisions;
    decisions.reserve(softSymbols.size());
    for (const auto& energies : softSymbols) {
        int best = static_cast<int>(
            std::max_element(energies.begin(), energies.end()) - energies.begin());
        decisions.push_back(best);
    }
    return decisions;
}

float Preamble::correlate(const std::vector<int>& symbols, int offset) const
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

bool Preamble::detect(const std::vector<std::vector<float>>& softSymbols,
                      float& score) const
{
    auto symbols = hardDecisions(softSymbols);
    score = 0.0f;

    int n = static_cast<int>(symbols.size());
    for (int offset = 0; offset <= n - PREAMBLE_LENGTH; ++offset) {
        float c = correlate(symbols, offset);
        if (c > score) score = c;
    }

    // Normalized threshold: PREAMBLE_THRESHOLD (1.4 on 0-16 scale) / PREAMBLE_LENGTH
    constexpr float threshold = static_cast<float>(PREAMBLE_THRESHOLD) / PREAMBLE_LENGTH;
    return score >= threshold;
}

} // namespace HavenFSK
