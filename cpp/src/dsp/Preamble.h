#pragma once
#include <vector>
#include "Constants.h"

namespace HavenFSK {

class Preamble {
public:
    Preamble();

    // Generate preamble audio using a continuous phase accumulator.
    // Stores the final phase in m_finalPhase so Frame::assemble()
    // can seed the Modulator for a seamless preamble→header transition.
    std::vector<float> generate();

    // Returns the phase at the end of the last generate() call.
    // Frame::assemble() passes this to mod.setPhase() so the header
    // starts with continuous phase from the preamble.
    double finalPhase() const { return m_finalPhase; }

    // Feed a window of soft symbol energies (shape [N][NUM_TONES]).
    // Returns true if preamble detected; sets matchOffset to the index
    // within softSymbols where the preamble begins. Caller uses
    // matchOffset + PREAMBLE_LENGTH to find the first frame symbol.
    bool detect(const std::vector<std::vector<float>>& softSymbols,
                int& matchOffset) const;

    // Normalized correlation of hard-decision symbols against
    // PREAMBLE_SYMBOLS[] starting at offset.
    // Returns score in [0.0, 1.0] where 1.0 = perfect match.
    // Public so DspPipeline can run its own sliding-window search.
    float correlate(const std::vector<int>& symbols, int offset) const;

    // Soft correlation: returns fraction of FFT energy at expected-tone bins,
    // averaged across all PREAMBLE_LENGTH positions. More robust than
    // correlate() when tones are attenuated but still present. Score range:
    // ~0.0625 = uniform noise (1/NUM_TONES), 1.0 = perfect, 0.0 = tone absent.
    float softCorrelate(
        const std::vector<std::vector<float>>& softSymbols, int offset) const;

private:
    double m_finalPhase {0.0};  // phase at end of last generate() call

    // Take hard decisions from soft energies, return symbol indices
    std::vector<int> hardDecisions(
        const std::vector<std::vector<float>>& softSymbols) const;
};

} // namespace HavenFSK
