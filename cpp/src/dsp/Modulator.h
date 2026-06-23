#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include "Constants.h"

namespace HavenFSK {

class Modulator {
public:
    Modulator();

    // Convert raw bytes to audio samples.
    // Uses continuous phase accumulator — phase carries across all symbols
    // and across multiple modulate() calls on the same instance.
    // Output is float32, normalized to TX_AMPLITUDE (0.25) peak.
    std::vector<float> modulate(const std::vector<uint8_t>& data);

    // Convenience: encode UTF-8 text to audio
    std::vector<float> modulateText(const std::string& text);

    // Reset phase accumulator to 0.0
    void resetPhase() { m_txPhase = 0.0; }

    // Set phase accumulator to a specific value.
    // Used by Frame::assemble() to continue phase seamlessly from preamble.
    void setPhase(double phase) { m_txPhase = phase; }

private:
    // Pre-built tone table — kept for reference; not used for TX generation
    float m_toneTable[NUM_TONES][SAMPLES_PER_SYMBOL];

    // Pre-built raised cosine ramps
    float m_rampUp[RAMP_SAMPLES];
    float m_rampDown[RAMP_SAMPLES];

    // Continuous phase accumulator — carries phase across all symbol boundaries
    double m_txPhase {0.0};

    void buildToneTable();
    void buildRamps();

    std::vector<int> bytesToSymbols(const std::vector<uint8_t>& data) const;

    // Generate one shaped symbol period — advances m_txPhase continuously
    std::vector<float> symbolToSamples(int symbol);

    // Apply raised cosine ramps to a symbol's amplitude envelope
    void applyRamps(std::vector<float>& samples);
};

} // namespace HavenFSK
