#include "Modulator.h"
#include <cmath>
#include <algorithm>
#include <numeric>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace HavenFSK {

Modulator::Modulator()
{
    buildToneTable();
    buildRamps();
}

void Modulator::buildToneTable()
{
    // Pre-build tone table for reference; TX generation now uses the
    // continuous phase accumulator instead of this table.
    for (int i = 0; i < NUM_TONES; ++i) {
        double f = BASE_FREQ + i * SYMBOL_RATE;
        for (int t = 0; t < SAMPLES_PER_SYMBOL; ++t) {
            m_toneTable[i][t] = static_cast<float>(
                std::sin(2.0 * M_PI * f * t / SAMPLE_RATE));
        }
    }
}

void Modulator::buildRamps()
{
    for (int i = 0; i < RAMP_SAMPLES; ++i)
        m_rampUp[i] = static_cast<float>(
            std::sin(M_PI / 2.0 * i / RAMP_SAMPLES));
    for (int i = 0; i < RAMP_SAMPLES; ++i)
        m_rampDown[i] = m_rampUp[RAMP_SAMPLES - 1 - i];
}

std::vector<int> Modulator::bytesToSymbols(
    const std::vector<uint8_t>& data) const
{
    std::vector<int> symbols;
    symbols.reserve(data.size() * 2);
    for (uint8_t byte : data) {
        symbols.push_back((byte >> 4) & 0x0F);
        symbols.push_back(byte & 0x0F);
    }
    return symbols;
}

void Modulator::applyRamps(std::vector<float>& samples)
{
    int n       = static_cast<int>(samples.size());
    int rampLen = RAMP_SAMPLES;

    for (int i = 0; i < rampLen && i < n; i++)
        samples[i] *= m_rampUp[i];

    for (int i = 0; i < rampLen && i < n; i++)
        samples[n - 1 - i] *= m_rampDown[i];
}

std::vector<float> Modulator::symbolToSamples(int symbol)
{
    if (symbol < 0 || symbol >= NUM_TONES)
        symbol = 0;

    // Tone frequency for this symbol
    double freq = BASE_FREQ + symbol * SYMBOL_RATE;

    // Phase increment per sample for this tone
    double phaseInc = 2.0 * M_PI * freq
                    / static_cast<double>(SAMPLE_RATE);

    std::vector<float> samples(SAMPLES_PER_SYMBOL);

    for (int i = 0; i < SAMPLES_PER_SYMBOL; i++) {
        // Generate sample from continuous phase accumulator
        samples[i] = static_cast<float>(std::sin(m_txPhase));

        // Advance phase — carries across symbol boundaries
        m_txPhase += phaseInc;

        // Wrap to [-π, π] to prevent float precision drift over long TX
        if (m_txPhase > M_PI)
            m_txPhase -= 2.0 * M_PI;
    }

    // Ramps removed: with continuous phase (CPFSK) there are no phase
    // discontinuities to mask. Ramps at 31.25 symbols/sec created
    // periodic amplitude dips that caused the pulsing artifact.

    return samples;
}

std::vector<float> Modulator::modulate(const std::vector<uint8_t>& data)
{
    if (data.empty()) return {};

    auto symbols = bytesToSymbols(data);

    std::vector<float> output;
    output.reserve(symbols.size() * SAMPLES_PER_SYMBOL);

    for (int sym : symbols) {
        auto block = symbolToSamples(sym);
        output.insert(output.end(), block.begin(), block.end());
    }

    float peak = 0.0f;
    for (float s : output)
        peak = std::max(peak, std::abs(s));

    if (peak > 0.0f) {
        float scale = static_cast<float>(TX_AMPLITUDE) / peak;
        for (float& s : output)
            s *= scale;
    }

    return output;
}

std::vector<float> Modulator::modulateText(const std::string& text)
{
    std::vector<uint8_t> bytes(text.begin(), text.end());
    return modulate(bytes);
}

} // namespace HavenFSK
