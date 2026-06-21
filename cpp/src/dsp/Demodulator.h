#pragma once
#include <vector>
#include <cstdint>
#include "Constants.h"
#include "kiss_fftr.h"

namespace HavenFSK {

class Demodulator {
public:
    Demodulator();
    ~Demodulator();

    // Returns soft symbol energies: shape [num_symbols][NUM_TONES]
    // Used by FEC layer for belief propagation input
    std::vector<std::vector<float>> demodulateToSoft(
        const std::vector<float>& audio) const;

    // Hard decision demodulation — returns decoded bytes
    std::vector<uint8_t> demodulate(const std::vector<float>& audio) const;

private:
    kiss_fftr_cfg m_fftCfg;

    // Pre-computed Hann window [SAMPLES_PER_SYMBOL]
    float m_hannWindow[SAMPLES_PER_SYMBOL];

    // Pre-computed tone bin indices in the zero-padded FFT
    int m_toneBins[NUM_TONES];

    void buildHannWindow();
    void buildToneBins();

    // Detect one symbol block (SAMPLES_PER_SYMBOL floats)
    // Returns energy at each of the NUM_TONES tone bins
    std::vector<float> detectSymbol(const float* block) const;

    // Pack pairs of 4-bit symbol indices into bytes, high nibble first
    std::vector<uint8_t> symbolsToBytes(const std::vector<int>& symbols) const;
};

} // namespace HavenFSK
