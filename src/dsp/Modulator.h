#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include "MfskConstants.h"

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
    // Continuous phase accumulator — carries phase across all symbol boundaries
    double m_txPhase {0.0};

    std::vector<int> bytesToSymbols(const std::vector<uint8_t>& data) const;

    // Generate one symbol period — advances m_txPhase continuously
    std::vector<float> symbolToSamples(int symbol);
};

} // namespace HavenFSK
