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
        m_rampUp[i] = static_cast<float>(std::sin(M_PI / 2.0 * i / RAMP_SAMPLES));
    // m_rampUp must be fully populated before m_rampDown reads it
    for (int i = 0; i < RAMP_SAMPLES; ++i)
        m_rampDown[i] = m_rampUp[RAMP_SAMPLES - 1 - i];
}

std::vector<int> Modulator::bytesToSymbols(const std::vector<uint8_t>& data) const
{
    std::vector<int> symbols;
    symbols.reserve(data.size() * 2);
    for (uint8_t byte : data) {
        symbols.push_back((byte >> 4) & 0x0F);
        symbols.push_back(byte & 0x0F);
    }
    return symbols;
}

std::vector<float> Modulator::symbolToSamples(int symbol) const
{
    std::vector<float> samples(m_toneTable[symbol],
                               m_toneTable[symbol] + SAMPLES_PER_SYMBOL);
    for (int i = 0; i < RAMP_SAMPLES; ++i) {
        samples[i]                              *= m_rampUp[i];
        samples[SAMPLES_PER_SYMBOL - 1 - i]    *= m_rampDown[i];
    }
    return samples;
}

std::vector<float> Modulator::modulate(const std::vector<uint8_t>& data) const
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

std::vector<float> Modulator::modulateText(const std::string& text) const
{
    std::vector<uint8_t> bytes(text.begin(), text.end());
    return modulate(bytes);
}

} // namespace HavenFSK
