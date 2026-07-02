#pragma once
#include <vector>
#include <cstdint>
#include "MfskConstants.h"
#include "kiss_fftr.h"

namespace HavenFSK {

class Demodulator {
public:
    Demodulator();
    ~Demodulator();

    // Returns soft symbol energies: shape [num_symbols][NUM_TONES]
    // sampleOffset skips that many samples before slicing into symbol blocks.
    // afcBinOffset shifts every tone's expected bin by this many zero-padded
    // FFT bins (use round(offsetHz * FFT_SIZE / SAMPLE_RATE)) to compensate
    // for oscillator frequency offset between TX and RX radios.
    std::vector<std::vector<float>> demodulateToSoft(
        const std::vector<float>& audio,
        int sampleOffset   = 0,
        int afcBinOffset   = 0) const;

    // Hard decision demodulation — returns decoded bytes
    std::vector<uint8_t> demodulate(const std::vector<float>& audio) const;

private:
    kiss_fftr_cfg m_fftCfg;

    // Pre-computed tone bin indices in the zero-padded FFT
    int m_toneBins[NUM_TONES];

    void buildToneBins();

    // Detect one symbol block (SAMPLES_PER_SYMBOL floats)
    // Returns energy at each of the NUM_TONES tone bins.
    // afcBinOffset shifts each tone's center bin before energy summation.
    std::vector<float> detectSymbol(const float* block,
                                    int afcBinOffset = 0) const;

    // Pack pairs of 4-bit symbol indices into bytes, high nibble first
    std::vector<uint8_t> symbolsToBytes(const std::vector<int>& symbols) const;
};

} // namespace HavenFSK
