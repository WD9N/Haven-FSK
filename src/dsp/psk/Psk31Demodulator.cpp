#include "Psk31Demodulator.h"
#include "../Constants.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace HavenFSK {

Psk31Demodulator::Psk31Demodulator(double baudRate, double carrierHz)
    : m_baudRate(baudRate)
    , m_carrierHz(carrierHz)
    , m_samplesPerSymbol(static_cast<int>(SAMPLE_RATE / baudRate))
    , m_ncoFreqInc(2.0 * M_PI * carrierHz / SAMPLE_RATE)
    , m_nominalFreqInc(2.0 * M_PI * carrierHz / SAMPLE_RATE)
{
    m_symbolLenAdjusted = m_samplesPerSymbol;
}

Psk31Demodulator::Result Psk31Demodulator::processSample(float sample) {
    Result result;

    // Quadrature downconvert against the NCO (Costas-loop-corrected
    // carrier estimate). Sign convention matches Psk31Modulator, which
    // synthesizes sample = I*cos(theta) + Q*sin(theta) (note: '+', not
    // the more common I*cos-Q*sin convention) — so recovering Q here
    // uses +sin, not -sin. Multiplying by cos/sin and averaging over a
    // symbol (many carrier cycles) isolates I and Q respectively since
    // the cross term sin*cos averages to zero and cos^2/sin^2 average
    // to 0.5.
    double i = static_cast<double>(sample) * std::cos(m_ncoPhase);
    double q = static_cast<double>(sample) * std::sin(m_ncoPhase);

    // Only integrate the last quarter of the symbol window. Psk31Modulator
    // uses raised-cosine constellation-transition shaping that ramps
    // across the *entire* symbol period (see appendSymbol()), so
    // integrating the full window averages roughly 50% previous-symbol /
    // 50% new-symbol energy — smearing consecutive symbols together (ISI)
    // and badly degrading decision margin. By the last quarter of the
    // window the transition has mostly settled onto the new symbol, so
    // integrating just that portion recovers a much cleaner estimate.
    // (Found via loopback self-test: full-window integration decoded
    // correctly only with the Costas loop disabled, and still drifted:
    // the smeared signal had so little margin left that the loop's own
    // small corrections were enough to push decisions across the
    // boundary. Restricting the window fixed both symptoms at once.)
    if (m_sampleInSymbol >= (m_symbolLenAdjusted * 3) / 4) {
        m_iAccum += i;
        m_qAccum += q;
    }

    // Energy-based early/late timing hint: compare front-half vs
    // back-half energy of the integration window. This is a simplified
    // heuristic, not a rigorous Gardner TED (no fractional resampling) —
    // it provides coarse drift tracking. Real-world tuning against an
    // actual PSK31 signal will likely want refinement here.
    double instEnergy = i * i + q * q;
    if (m_sampleInSymbol < m_symbolLenAdjusted / 2) m_frontEnergy += instEnergy;
    else                                            m_backEnergy  += instEnergy;

    m_ncoPhase += m_ncoFreqInc;
    if (m_ncoPhase > 2.0 * M_PI) m_ncoPhase -= 2.0 * M_PI;

    ++m_sampleInSymbol;

    if (m_sampleInSymbol >= m_symbolLenAdjusted) {
        // Normalize by the number of samples actually accumulated (the
        // last quarter of the window), not the full window length.
        double n    = static_cast<double>(m_symbolLenAdjusted) -
                      static_cast<double>((m_symbolLenAdjusted * 3) / 4);
        double iSym = m_iAccum / n;
        double qSym = m_qAccum / n;

        // m_prevI/m_prevQ start at (1,0) with m_havePrev always true
        // (see header comment) — matches Psk31Modulator's own initial
        // reference state, so the first received symbol decodes
        // immediately instead of being consumed as a reference.
        // Differential phase: arg(conj(prev) * current). Real part
        // dominates for bit=1 (no phase change), goes negative for
        // bit=0 (180-degree reversal) — matches Psk31Modulator's
        // convention exactly (see that file's header comment).
        double dI = m_prevI * iSym + m_prevQ * qSym;
        double dQ = m_prevI * qSym - m_prevQ * iSym;

        result.bitReady = true;
        result.bit = (dI >= 0.0);

        double mag = std::sqrt(dI * dI + dQ * dQ);
        m_lockQuality = (mag > 1e-9) ?
            static_cast<float>(std::abs(dI) / mag) : 0.0f;
        result.lockQuality = m_lockQuality;

        applyCostasCorrection(iSym, qSym);

        m_prevI = iSym;
        m_prevQ = qSym;

        // Nudge integration window length by the accumulated timing bias.
        double totalEnergy = m_frontEnergy + m_backEnergy;
        if (totalEnergy > 1e-9) {
            double imbalance = (m_frontEnergy - m_backEnergy) / totalEnergy;
            m_timingBias += PSK31_TIMING_GAIN * imbalance;
        }
        m_symbolLenAdjusted = m_samplesPerSymbol;
        if (m_timingBias > 1.0)       { m_symbolLenAdjusted += 1; m_timingBias -= 1.0; }
        else if (m_timingBias < -1.0) { m_symbolLenAdjusted -= 1; m_timingBias += 1.0; }

        m_iAccum = m_qAccum = 0.0;
        m_frontEnergy = m_backEnergy = 0.0;
        m_sampleInSymbol = 0;
    }

    return result;
}

void Psk31Demodulator::applyCostasCorrection(double i, double q) {
    // Standard decision-directed BPSK Costas loop error term.
    double errorSign  = (i >= 0.0) ? 1.0 : -1.0;
    double phaseError = errorSign * q;

    m_ncoFreqInc += PSK31_COSTAS_BETA * phaseError;

    // Clamp frequency pull-in range to +-50 Hz to prevent the loop from
    // wandering off onto an unrelated signal or noise.
    double maxDev = 2.0 * M_PI * 50.0 / SAMPLE_RATE;
    if (m_ncoFreqInc > m_nominalFreqInc + maxDev) m_ncoFreqInc = m_nominalFreqInc + maxDev;
    if (m_ncoFreqInc < m_nominalFreqInc - maxDev) m_ncoFreqInc = m_nominalFreqInc - maxDev;

    m_ncoPhase += PSK31_COSTAS_ALPHA * phaseError;
}

void Psk31Demodulator::reset() {
    m_ncoPhase       = 0.0;
    m_ncoFreqInc     = m_nominalFreqInc;
    m_iAccum         = 0.0;
    m_qAccum         = 0.0;
    m_sampleInSymbol = 0;
    m_frontEnergy    = 0.0;
    m_backEnergy     = 0.0;
    m_timingBias     = 0.0;
    m_symbolLenAdjusted = m_samplesPerSymbol;
    m_prevI          = 1.0;
    m_prevQ          = 0.0;
    m_lockQuality    = 0.0f;
}

} // namespace HavenFSK
