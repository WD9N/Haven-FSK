#include "Demodulator.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace HavenFSK {

Demodulator::Demodulator()
{
    buildHannWindow();
    buildToneBins();
    m_fftCfg = kiss_fftr_alloc(FFT_SIZE, 0, nullptr, nullptr);
    if (!m_fftCfg)
        throw std::runtime_error("kiss_fftr_alloc failed");
}

Demodulator::~Demodulator()
{
    kiss_fftr_free(m_fftCfg);
}

void Demodulator::buildHannWindow()
{
    for (int i = 0; i < SAMPLES_PER_SYMBOL; ++i) {
        m_hannWindow[i] = 0.5f * (1.0f - static_cast<float>(
            std::cos(2.0 * M_PI * i / (SAMPLES_PER_SYMBOL - 1))));
    }
}

void Demodulator::buildToneBins()
{
    for (int i = 0; i < NUM_TONES; ++i) {
        double f = BASE_FREQ + i * SYMBOL_RATE;
        m_toneBins[i] = static_cast<int>(
            std::round(f * FFT_SIZE / SAMPLE_RATE));
    }
}

std::vector<float> Demodulator::detectSymbol(const float* block) const
{
    // Zero-padded buffer
    std::vector<float> buf(FFT_SIZE, 0.0f);
    for (int i = 0; i < SAMPLES_PER_SYMBOL; ++i)
        buf[i] = block[i] * m_hannWindow[i];

    std::vector<kiss_fft_cpx> out(FFT_SIZE / 2 + 1);
    kiss_fftr(m_fftCfg, buf.data(), out.data());

    const int maxBin = FFT_SIZE / 2;
    std::vector<float> energies(NUM_TONES, 0.0f);
    for (int t = 0; t < NUM_TONES; ++t) {
        int center = m_toneBins[t];
        float e = 0.0f;
        for (int k = center - FFT_GUARD_BINS; k <= center + FFT_GUARD_BINS; ++k) {
            if (k < 0 || k > maxBin) continue;
            e += out[k].r * out[k].r + out[k].i * out[k].i;
        }
        energies[t] = e;
    }
    return energies;
}

std::vector<std::vector<float>> Demodulator::demodulateToSoft(
    const std::vector<float>& audio) const
{
    int numSymbols = static_cast<int>(audio.size()) / SAMPLES_PER_SYMBOL;
    std::vector<std::vector<float>> result;
    result.reserve(numSymbols);
    for (int s = 0; s < numSymbols; ++s)
        result.push_back(detectSymbol(audio.data() + s * SAMPLES_PER_SYMBOL));
    return result;
}

std::vector<uint8_t> Demodulator::demodulate(const std::vector<float>& audio) const
{
    auto soft = demodulateToSoft(audio);
    std::vector<int> symbols;
    symbols.reserve(soft.size());
    for (const auto& energies : soft) {
        int best = static_cast<int>(
            std::max_element(energies.begin(), energies.end()) - energies.begin());
        symbols.push_back(best);
    }
    return symbolsToBytes(symbols);
}

std::vector<uint8_t> Demodulator::symbolsToBytes(const std::vector<int>& symbols) const
{
    std::vector<uint8_t> bytes;
    int n = static_cast<int>(symbols.size());
    bytes.reserve(n / 2);
    for (int i = 0; i + 1 < n; i += 2) {
        bytes.push_back(static_cast<uint8_t>(
            (symbols[i] << 4) | symbols[i + 1]));
    }
    return bytes;
}

} // namespace HavenFSK
