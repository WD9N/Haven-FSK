#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include "Constants.h"

namespace HavenFSK {

class Modulator {
public:
    Modulator();

    // Convert raw bytes to audio samples
    // Output is float32, normalized to TX_AMPLITUDE (0.25) peak
    std::vector<float> modulate(const std::vector<uint8_t>& data) const;

    // Convenience: encode UTF-8 text to audio
    std::vector<float> modulateText(const std::string& text) const;

private:
    // Pre-built tone table [NUM_TONES][SAMPLES_PER_SYMBOL]
    float m_toneTable[NUM_TONES][SAMPLES_PER_SYMBOL];

    // Pre-built raised cosine ramps
    float m_rampUp[RAMP_SAMPLES];
    float m_rampDown[RAMP_SAMPLES];

    void buildToneTable();
    void buildRamps();

    // Split bytes into 4-bit symbol indices, MSB (high nibble) first
    std::vector<int> bytesToSymbols(const std::vector<uint8_t>& data) const;

    // Generate one shaped symbol period for the given symbol index
    std::vector<float> symbolToSamples(int symbol) const;
};

} // namespace HavenFSK
