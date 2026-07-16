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
// Carrier tuning offsets: the Costas loop's own pull-in is only a few
// Hz (bench-verified 2026-07-15: clean at +-5 Hz, total failure at
// 10 Hz), which is far too narrow for stations tuned by eye on a
// waterfall. So acquisition measures the station's actual carrier — a
// bandpassed, squared-signal spectral-line search (estimateCarrierHz())
// over the same window as the timing histogram — and starts the NCO
// there; the Costas loop then only tracks phase and drift. Bench-clean
// to +-40 Hz offset at both +10 and -5 dB SNR; the narrowband DCD
// (carrier +-1.5x baud) is what bounds usable offset, not this loop.
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

    // NCO's current carrier estimate minus the nominal carrier, Hz —
    // after acquisition this is where the station actually is.
    float carrierOffsetHz() const;

    // Shift the NCO by deltaHz immediately (clamped to the Costas
    // deviation limit). Used when the rig dial is about to move by the
    // opposite amount, so tracking rides through the retune.
    void nudgeCarrierHz(float deltaHz);

    void reset();

private:
    double m_baudRate;
    double m_carrierHz;
    int    m_samplesPerSymbol;

    // ── NCO (carrier tracking) ─────────────────────────────────────────
    double m_ncoPhase   {0.0};
    double m_ncoFreqInc;              // radians/sample, adjusted by Costas loop
    const double m_nominalFreqInc;    // starting point, for reference/reset

    // ── Matched-filter accumulators ─────────────────────────────────────
    // Each TX pulse spans two symbol periods (raised-cosine rise across
    // its own period, fall across the next — see Psk31Modulator), so two
    // overlapping weighted accumulators run at once: m_fall* completes
    // the previous symbol, m_rise* starts the current one. Estimates
    // finalize one symbol period late.
    double m_fallI {0.0};
    double m_fallQ {0.0};
    double m_riseI {0.0};
    double m_riseQ {0.0};
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

    // Doubled-differential-phase coherence vector (IIR-smoothed); its
    // magnitude is the lock quality — see processSample().
    double m_lockI {0.0};
    double m_lockQ {0.0};
    float m_lockQuality {0.0f};

    // ── Symbol timing acquisition ────────────────────────────────────────
    // The demodulator starts wherever DCD opens — an arbitrary point
    // within a symbol, misaligned by up to half a symbol period. The
    // tracking loop only nudges ±1 sample per symbol, so it can never
    // recover from that; the matched filter needs real alignment. During
    // the TX phase-reversal preamble the envelope nulls at every symbol
    // boundary, so for the first ACQ_SYMBOLS periods after reset we
    // accumulate a lowpassed-envelope-energy histogram over sample
    // position mod symbol length, then snap the window start to the
    // histogram minimum. No bits are emitted while acquiring.
    static constexpr int ACQ_SYMBOLS = 12;

    bool m_acquiring {true};
    int  m_acqSymbolsDone {0};
    std::vector<double> m_acqHist;   // sized m_samplesPerSymbol in ctor
    double m_acqLpI {0.0};           // one-pole lowpass state, ~100 Hz
    double m_acqLpQ {0.0};

    // Raw samples buffered across the acquisition window, for the
    // carrier-frequency estimate in finishAcquisition(). A live station
    // is never exactly on the nominal carrier — operators tune by eye —
    // and the Costas loop's verified pull-in is only a few Hz (10 Hz
    // offset = total decode failure on the bench), so the NCO must
    // START on the station's actual frequency rather than pull in.
    //
    // The buffer keeps filling after acquisition too: every ACQ_SYMBOLS
    // symbols it holds a fresh estimation window, and if carrier lock
    // has collapsed while the DCD still sees a signal — the signature of
    // the operator retuning the dial mid-carrier, an instant jump the
    // Costas loop can never follow — the carrier estimate is re-run and
    // the NCO re-snapped (see the re-lock block in processSample()).
    // While lock is good the full window is simply discarded, costing
    // nothing.
    std::vector<float> m_acqBuf;

    // Lock quality below which a filled estimation window triggers a
    // carrier re-estimate. Locked signals sit near 1.0, noise near 0.2;
    // a carrier the NCO is mistuned against also reads ~0.2.
    static constexpr float RELOCK_QUALITY = 0.4f;

    void finishAcquisition();
    double estimateCarrierHz() const;   // squared-signal spectral line at 2*fc

    void applyCostasCorrection(double i, double q);
};

} // namespace HavenFSK
