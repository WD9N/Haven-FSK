#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include "Psk31Constants.h"

namespace HavenFSK {

// Psk31Modulator — differential BPSK generator with raised-cosine
// constellation-transition shaping, matching the standard PSK31
// modulation convention: bit 0 = 180-degree phase reversal, bit 1 = no
// phase change. Verified against fldigi's src/psk/psk.cxx (tx_bit/
// tx_carriers: for plain BPSK, sym = bit<<1, then sym*=4 selects
// sym_vec_pos[0] (180 deg, bit 0) or sym_vec_pos[8] (0 deg, bit 1)) —
// this class is an independent reimplementation of that same documented
// convention, not a port of fldigi's multi-carrier code.
//
// Carrier phase is a continuous accumulator that never resets mid-
// transmission, same discipline as Modulator's CPFSK fix (see
// DECISIONS.md ADR-073/074/075) — differential decoding depends on
// phase continuity exactly as MFSK's demodulator depended on it there.
class Psk31Modulator {
public:
    explicit Psk31Modulator(double baudRate = PSK31_DEFAULT_BAUD,
                             double carrierHz = PSK31_CARRIER_HZ);

    // Modulate a pre-encoded bitstream (see Varicode::encode()) to audio.
    std::vector<float> modulateBits(const std::vector<bool>& bits);

    // Convenience: varicode-encode text, then modulate.
    std::vector<float> modulateText(const std::string& text);

    void resetPhase() { m_carrierPhase = 0.0; }

private:
    double m_baudRate;
    double m_carrierHz;
    int    m_samplesPerSymbol;

    double m_carrierPhase {0.0};  // continuous — never reset mid-transmission
    double m_prevI {1.0};
    double m_prevQ {0.0};

    // Advance one symbol period, generating samples that smoothly
    // transition the baseband constellation point from (m_prevI,m_prevQ)
    // to (newI,newQ) via a raised-cosine window, modulated onto the
    // continuous carrier.
    void appendSymbol(double newI, double newQ, std::vector<float>& out);
};

} // namespace HavenFSK
