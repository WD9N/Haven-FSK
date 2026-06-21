#pragma once
#include <vector>
#include "Constants.h"

namespace HavenFSK {

class Preamble {
public:
    Preamble();

    // Generate preamble audio for transmission
    // Returns PREAMBLE_LENGTH * SAMPLES_PER_SYMBOL samples
    std::vector<float> generate() const;

    // Feed a window of soft symbol energies (shape [N][NUM_TONES])
    // Returns true if preamble detected, sets score to correlation value
    bool detect(const std::vector<std::vector<float>>& softSymbols,
                float& score) const;

private:
    std::vector<float> m_preambleAudio;
    void buildPreambleAudio();

    // Take hard decisions from soft energies, return symbol indices
    std::vector<int> hardDecisions(
        const std::vector<std::vector<float>>& softSymbols) const;

    // Normalized correlation against PREAMBLE_SYMBOLS[]
    // Returns score in range [0.0, 1.0] where 1.0 = perfect match
    float correlate(const std::vector<int>& symbols, int offset) const;
};

} // namespace HavenFSK
