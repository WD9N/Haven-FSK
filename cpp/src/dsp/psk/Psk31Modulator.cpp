#include "Psk31Modulator.h"
#include "Varicode.h"
#include "../Constants.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace HavenFSK {

Psk31Modulator::Psk31Modulator(double baudRate, double carrierHz)
    : m_baudRate(baudRate)
    , m_carrierHz(carrierHz)
    , m_samplesPerSymbol(static_cast<int>(SAMPLE_RATE / baudRate))
{
}

std::vector<float> Psk31Modulator::modulateText(const std::string& text) {
    return modulateBits(Varicode::encode(text));
}

std::vector<float> Psk31Modulator::modulateBits(const std::vector<bool>& bits) {
    std::vector<float> out;
    out.reserve(bits.size() * static_cast<size_t>(m_samplesPerSymbol));

    for (bool bit : bits) {
        // Differential BPSK: bit 0 = 180-degree phase reversal
        // (multiply by -1), bit 1 = no change (multiply by +1).
        double newI = bit ?  m_prevI : -m_prevI;
        double newQ = bit ?  m_prevQ : -m_prevQ;
        appendSymbol(newI, newQ, out);
    }
    return out;
}

void Psk31Modulator::appendSymbol(double newI, double newQ,
                                   std::vector<float>& out)
{
    double phaseInc = 2.0 * M_PI * m_carrierHz / SAMPLE_RATE;
    int n = m_samplesPerSymbol;

    for (int i = 0; i < n; ++i) {
        // Raised-cosine constellation transition: shapeA ramps 1 -> 0
        // across the symbol so the baseband point moves smoothly from
        // the previous symbol to the new one, keeping occupied
        // bandwidth narrow (avoids the wideband splatter of an abrupt
        // phase jump). shapeA(0)=1 (start = old symbol), shapeA(n-1)~0
        // (end = new symbol).
        double symPhase = M_PI * static_cast<double>(i) / static_cast<double>(n);
        double shapeA = 0.5 * std::cos(symPhase) + 0.5;
        double shapeB = 1.0 - shapeA;

        double ival = shapeA * m_prevI + shapeB * newI;
        double qval = shapeA * m_prevQ + shapeB * newQ;

        double sample = ival * std::cos(m_carrierPhase)
                      + qval * std::sin(m_carrierPhase);
        out.push_back(static_cast<float>(sample));

        m_carrierPhase += phaseInc;
        if (m_carrierPhase > 2.0 * M_PI) m_carrierPhase -= 2.0 * M_PI;
    }

    m_prevI = newI;
    m_prevQ = newQ;
}

} // namespace HavenFSK
