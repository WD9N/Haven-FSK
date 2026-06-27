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
    buildToneBins();
    m_fftCfg = kiss_fftr_alloc(FFT_SIZE, 0, nullptr, nullptr);
    if (!m_fftCfg)
        throw std::runtime_error("kiss_fftr_alloc failed");
}

Demodulator::~Demodulator()
{
    kiss_fftr_free(m_fftCfg);
}

void Demodulator::buildToneBins()
{
    for (int i = 0; i < NUM_TONES; ++i) {
        double f = BASE_FREQ + i * SYMBOL_RATE;
        m_toneBins[i] = static_cast<int>(
            std::round(f * FFT_SIZE / SAMPLE_RATE));
    }
}

std::vector<float> Demodulator::detectSymbol(const float* block,
                                              int afcBinOffset) const
{
    // Zero-padded buffer — rectangular window (no windowing).
    // All HAVEN tones produce exact integer cycles per symbol period,
    // so a rectangular window gives a perfect delta at the tone bin.
    std::vector<float> buf(FFT_SIZE, 0.0f);
    for (int i = 0; i < SAMPLES_PER_SYMBOL; ++i)
        buf[i] = block[i];

    std::vector<kiss_fft_cpx> out(FFT_SIZE / 2 + 1);
    kiss_fftr(m_fftCfg, buf.data(), out.data());

    const int maxBin = FFT_SIZE / 2;
    std::vector<float> energies(NUM_TONES, 0.0f);
    for (int t = 0; t < NUM_TONES; ++t) {
        int center = m_toneBins[t] + afcBinOffset;
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
    const std::vector<float>& audio, int sampleOffset, int afcBinOffset) const
{
    int available  = static_cast<int>(audio.size()) - sampleOffset;
    if (available < SAMPLES_PER_SYMBOL) return {};
    int numSymbols = available / SAMPLES_PER_SYMBOL;
    std::vector<std::vector<float>> result;
    result.reserve(numSymbols);
    for (int s = 0; s < numSymbols; ++s)
        result.push_back(detectSymbol(
            audio.data() + sampleOffset + s * SAMPLES_PER_SYMBOL,
            afcBinOffset));
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
