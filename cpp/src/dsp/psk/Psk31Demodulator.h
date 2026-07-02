#pragma once
#include <vector>
#include <cstdint>
#include "Psk31Constants.h"

namespace HavenFSK {

// Psk31Demodulator — coherent differential-BPSK demodulator: NCO-driven
// quadrature downconversion, integrate-and-dump matched filtering per
// symbol, an energy-based early/late timing nudge, and a decision-
// directed Costas loop for carrier phase/frequency tracking.
//
// This is a from-scratch implementation using standard textbook BPSK
// Costas-loop technique (not a port of fldigi's generalized multi-carrier
// correlator/FIR-filterbank approach — see DspPipeline's Demodulator for
// the codebase's existing FFT-based, non-coherent MFSK detector, which is
// architecturally unrelated: PSK31 needs coherent phase tracking, MFSK
// does not). Processes one audio sample at a time, matching the
// continuous, streaming nature of PSK31 reception (unlike MFSK's
// block-FFT-per-symbol approach).
//
// Known limitation (found via testing, not yet resolved): carrier
// frequency pull-in was verified clean at 0 Hz and 5 Hz offset between
// TX/RX carrier estimate, but failed at 15 Hz while somehow succeeding
// again at 30 Hz — a non-monotonic result that points to a resonance
// between the Costas loop and the simplified (non-Gardner, no fractional
// resampling) timing recovery in Psk31Demodulator::processSample(),
// rather than a single clean pull-in range. Until refined against real
// on-air signals, assume reliable tracking only for small offsets
// (operators tuned close together, as is typical PSK31 practice) and
// treat anything beyond a few Hz as unverified.
class Psk31Demodulator {
public:
    struct Result {
        bool bitReady = false;
        bool bit      = false;
        float lockQuality = 0.0f;  // 0..1, rough carrier-lock confidence
    };

    explicit Psk31Demodulator(double baudRate = PSK31_DEFAULT_BAUD,
                               double carrierHz = PSK31_CARRIER_HZ);

    // Feed one audio sample. Returns bitReady=true and the decoded bit
    // whenever a symbol boundary is reached.
    Result processSample(float sample);

    void reset();

private:
    double m_baudRate;
    double m_carrierHz;
    int    m_samplesPerSymbol;

    // ── NCO (carrier tracking) ─────────────────────────────────────────
    double m_ncoPhase   {0.0};
    double m_ncoFreqInc;              // radians/sample, adjusted by Costas loop
    const double m_nominalFreqInc;    // starting point, for reference/reset

    // ── Integrate-and-dump matched filter accumulators ──────────────────
    double m_iAccum {0.0};
    double m_qAccum {0.0};
    int    m_sampleInSymbol {0};

    // Energy-based early/late timing nudge: compare front-half vs
    // back-half accumulated energy of the integration window.
    double m_frontEnergy {0.0};
    double m_backEnergy  {0.0};
    double m_timingBias  {0.0};   // fractional-sample nudge, small drift correction
    int    m_symbolLenAdjusted;   // m_samplesPerSymbol, occasionally nudged +-1

    // ── Differential decode state ────────────────────────────────────────
    // Initialized to (1,0) — matches Psk31Modulator's own initial
    // reference state (see its m_prevI/m_prevQ), so the very first
    // received symbol is decodable immediately rather than being
    // consumed as an uninformative reference (that would silently drop
    // the first transmitted bit).
    double m_prevI {1.0};
    double m_prevQ {0.0};

    float m_lockQuality {0.0f};

    void applyCostasCorrection(double i, double q);
};

} // namespace HavenFSK
