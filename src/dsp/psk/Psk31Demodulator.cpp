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
    m_acqHist.assign(static_cast<size_t>(m_samplesPerSymbol), 0.0);
    m_acqBuf.reserve(static_cast<size_t>(ACQ_SYMBOLS) * m_samplesPerSymbol);
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

    // ── Timing acquisition phase ────────────────────────────────────────
    // See header: build an envelope-energy histogram over sample position
    // mod symbol length across the preamble's phase reversals, whose
    // nulls mark symbol boundaries. Lowpass I/Q first (~100 Hz one-pole)
    // so the histogram sees the narrowband envelope, not wideband noise.
    if (m_acquiring) {
        m_acqBuf.push_back(sample);
        constexpr double LP_A = 0.0131;  // 2*pi*100/48000
        m_acqLpI += LP_A * (i - m_acqLpI);
        m_acqLpQ += LP_A * (q - m_acqLpQ);
        m_acqHist[static_cast<size_t>(m_sampleInSymbol)] +=
            m_acqLpI * m_acqLpI + m_acqLpQ * m_acqLpQ;

        m_ncoPhase += m_ncoFreqInc;
        if (m_ncoPhase > 2.0 * M_PI) m_ncoPhase -= 2.0 * M_PI;

        if (++m_sampleInSymbol >= m_samplesPerSymbol) {
            m_sampleInSymbol = 0;
            if (++m_acqSymbolsDone >= ACQ_SYMBOLS)
                finishAcquisition();
        }
        return result;
    }

    // Matched-filter accumulation. Psk31Modulator's raised-cosine
    // constellation-transition shaping means symbol s's pulse spans TWO
    // symbol periods: it rises (0.5 - 0.5cos) across its own period and
    // falls (0.5 + 0.5cos) across the next, as the following transition
    // moves off it. The matched filter therefore keeps two overlapping
    // accumulators: every sample feeds the *completing* symbol with the
    // falling weight and the *starting* symbol with the rising weight.
    // A symbol's estimate finalizes one period late with its full pulse
    // energy collected — unlike the earlier last-quarter-only window,
    // which sidestepped the ISI but discarded ~75% of the samples (and
    // most of the symbol energy with them), costing several dB of
    // weak-signal sensitivity. The cosine weighting is also what
    // suppresses ISI here: each neighbor's contribution is weighted by
    // exactly the window where its own pulse is fading out.
    m_acqBuf.push_back(sample);   // rolling re-lock window, see header

    double wRise = 0.5 - 0.5 * std::cos(M_PI * m_sampleInSymbol
                                        / static_cast<double>(m_symbolLenAdjusted));
    double wFall = 1.0 - wRise;
    m_fallI += wFall * i;
    m_fallQ += wFall * q;
    m_riseI += wRise * i;
    m_riseQ += wRise * q;

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
        // The falling accumulator has now collected both halves of the
        // completing symbol's pulse (rising weights last period, falling
        // weights this period — total weight sums to one period length).
        double n    = static_cast<double>(m_symbolLenAdjusted);
        double iSym = m_fallI / n;
        double qSym = m_fallQ / n;

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

        // Lock quality: coherence of the DOUBLED differential phase.
        // For BPSK the differential phase is 0 or pi, so doubling maps
        // both onto 0 — on a real signal the doubled-phase unit vectors
        // all point the same way and their IIR average has magnitude
        // near 1; on noise the angles are uniform and the average
        // random-walks near 0. (The previous metric, |dI|/mag per bit,
        // averages |cos(uniform)| = 2/pi ~ 0.64 on pure noise — no
        // usable threshold existed between noise and real signals.)
        // Per-bit agreement gate: the IIR coherence decays from ~1.0 far
        // too slowly (~0.9/symbol) to squelch the first few symbols of
        // noise after a carrier disappears — the smoothed metric is
        // "stale" for ~7 symbols, long enough for one garbage character
        // to pass the per-character squelch average. So a bit's REPORTED
        // quality is discounted 0.3x when its own doubled-phase vector
        // points AWAY from the coherence mean (agree < 0, i.e. more than
        // 90 degrees off). On a locked signal that is rare even at -10 dB
        // SNR; on noise the angle is uniform, so half the bits get
        // discounted and a noise character's quality average collapses
        // below any sane squelch threshold. Bench-tuned (2026-07-15):
        // a tighter +-60 degree cone or a hard zero both sank real
        // characters at -10 dB, where phase jitter is routine.
        double mag2 = dI * dI + dQ * dQ;
        float bitQuality = 0.0f;
        if (mag2 > 1e-18) {
            double c2 = (dI * dI - dQ * dQ) / mag2;   // cos(2*theta)
            double s2 = 2.0 * dI * dQ / mag2;         // sin(2*theta)

            double lockMag = std::sqrt(m_lockI * m_lockI + m_lockQ * m_lockQ);
            // cos of the angle between this symbol's doubled-phase vector
            // and the (pre-update) coherence mean; +-60 degrees passes.
            double agree = (lockMag > 1e-12)
                ? (c2 * m_lockI + s2 * m_lockQ) / lockMag : 1.0;

            constexpr double LOCK_ALPHA = 0.1;
            m_lockI += LOCK_ALPHA * (c2 - m_lockI);
            m_lockQ += LOCK_ALPHA * (s2 - m_lockQ);
            m_lockQuality = static_cast<float>(
                std::sqrt(m_lockI * m_lockI + m_lockQ * m_lockQ));

            bitQuality = (agree >= 0.0) ? m_lockQuality
                                        : 0.3f * m_lockQuality;
        }
        result.lockQuality = bitQuality;

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

        // Hand over: the symbol that was accumulating its rising half
        // becomes the completing one; a fresh symbol starts at zero.
        m_fallI = m_riseI;
        m_fallQ = m_riseQ;
        m_riseI = m_riseQ = 0.0;
        m_frontEnergy = m_backEnergy = 0.0;
        m_sampleInSymbol = 0;

        // Re-lock check once per estimation window (~0.4 s): if carrier
        // lock has collapsed but the DCD still holds (or the modem would
        // have reset us), the operator likely retuned the dial — re-run
        // the carrier estimate on the fresh window and snap the NCO to
        // wherever the station went. The confidence gate keeps this from
        // firing on noise, and a locked signal never reaches it at all.
        if (m_acqBuf.size() >=
            static_cast<size_t>(ACQ_SYMBOLS) * m_samplesPerSymbol) {
            if (m_lockQuality < RELOCK_QUALITY) {
                double est = estimateCarrierHz();
                double curHz = m_ncoFreqInc * SAMPLE_RATE / (2.0 * M_PI);
                if (est > 0.0 && std::fabs(est - curHz) > 1.0) {
                    m_ncoFreqInc = 2.0 * M_PI * est / SAMPLE_RATE;
                    // Coherence state describes the OLD tuning — let the
                    // squelch metric rebuild against the new one.
                    m_lockI = m_lockQ = 0.0;
                }
            }
            m_acqBuf.clear();
        }
    }

    return result;
}

// Where is the station, exactly? Squaring a BPSK signal cancels the
// 180-degree modulation, leaving a clean spectral line at exactly twice
// the carrier; a Goertzel scan (0.5 Hz steps in carrier terms) plus
// parabolic interpolation over the acquisition buffer (~0.38 s, 12
// symbols) resolves it to well under the few-Hz accuracy the Costas
// loop needs. Search span +-60 Hz: the narrowband DCD only opens within
// ~+-47 Hz of nominal, so anything it admits is inside this window.
// Returns the measured carrier in Hz, or 0.0 when no confident spectral
// line was found (see the confidence gate below).
double Psk31Demodulator::estimateCarrierHz() const {
    // Bandpass around the nominal carrier before squaring. Squaring
    // folds the ENTIRE audio passband's noise onto itself (classic
    // squaring loss) — unfiltered, the 2*fc line drowns below about
    // 0 dB SNR (2500 Hz ref) and the scan snaps the NCO onto a noise
    // peak, destroying weak-signal decode entirely. The signal itself
    // occupies only ~+-60 Hz here, so two cascaded RBJ biquad bandpasses
    // (Q=6, ~13 dB noise cut) keep the line visible down to the DCD's
    // own admission floor.
    std::vector<float> sq(m_acqBuf.size());
    {
        const double w0 = 2.0 * M_PI * m_carrierHz / SAMPLE_RATE;
        const double q  = 6.0;
        const double alpha = std::sin(w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        const double b0 = alpha / a0, b2 = -alpha / a0;
        const double a1 = -2.0 * std::cos(w0) / a0, a2 = (1.0 - alpha) / a0;
        double x1a = 0, x2a = 0, y1a = 0, y2a = 0;   // stage A state
        double x1b = 0, x2b = 0, y1b = 0, y2b = 0;   // stage B state
        for (size_t k = 0; k < m_acqBuf.size(); ++k) {
            double x = m_acqBuf[k];
            double ya = b0 * x + b2 * x2a - a1 * y1a - a2 * y2a;
            x2a = x1a; x1a = x; y2a = y1a; y1a = ya;
            double yb = b0 * ya + b2 * x2b - a1 * y1b - a2 * y2b;
            x2b = x1b; x1b = ya; y2b = y1b; y1b = yb;
            sq[k] = static_cast<float>(yb * yb);
        }
    }

    auto goertzel = [&sq](double freqHz) {
        const double w = 2.0 * M_PI * freqHz / SAMPLE_RATE;
        const double coeff = 2.0 * std::cos(w);
        double s1 = 0.0, s2 = 0.0;
        for (float v : sq) {
            double s0 = static_cast<double>(v) + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return s1 * s1 + s2 * s2 - coeff * s1 * s2;
    };

    constexpr double SPAN_HZ = 60.0;
    constexpr double STEP_HZ = 0.5;   // in carrier terms; 1.0 Hz at 2*fc
    const int steps = static_cast<int>(2.0 * SPAN_HZ / STEP_HZ) + 1;

    std::vector<double> pow(static_cast<size_t>(steps));
    double powSum = 0.0;
    int bestIdx = 0;
    for (int s = 0; s < steps; ++s) {
        double fc = m_carrierHz - SPAN_HZ + s * STEP_HZ;
        pow[static_cast<size_t>(s)] = goertzel(2.0 * fc);
        powSum += pow[static_cast<size_t>(s)];
        if (pow[static_cast<size_t>(s)] > pow[static_cast<size_t>(bestIdx)])
            bestIdx = s;
    }
    double bestPow = pow[static_cast<size_t>(bestIdx)];

    // Confidence gate: a real spectral line towers over the scan's mean;
    // a scan over noise is flat-ish. Rather than report a noise peak
    // (snapping the NCO onto it means total decode failure), return 0 =
    // "no confident line" and let the caller keep its current tuning.
    if (bestPow < 4.0 * (powSum / steps)) return 0.0;

    double fc = m_carrierHz - SPAN_HZ + bestIdx * STEP_HZ;

    // Parabolic peak interpolation between the winning bin's neighbors.
    if (bestIdx > 0 && bestIdx < steps - 1) {
        double prev = pow[static_cast<size_t>(bestIdx - 1)];
        double next = pow[static_cast<size_t>(bestIdx + 1)];
        double denom = prev - 2.0 * bestPow + next;
        if (std::fabs(denom) > 1e-30) {
            double delta = 0.5 * (prev - next) / denom;
            if (delta > -1.0 && delta < 1.0) fc += delta * STEP_HZ;
        }
    }
    return fc;
}

void Psk31Demodulator::finishAcquisition() {
    const int n = m_samplesPerSymbol;

    // Snap the NCO to the station's measured carrier before the first
    // symbol is demodulated — the Costas loop then only has to track
    // phase and drift, which it verifiably can, instead of pulling in a
    // tuning offset, which it verifiably cannot beyond a few Hz.
    double est = estimateCarrierHz();
    if (est > 0.0)
        m_ncoFreqInc = 2.0 * M_PI * est / SAMPLE_RATE;
    m_acqBuf.clear();

    // Circular boxcar smoothing flattens the 2x-carrier ripple and
    // per-sample noise so the broad envelope null dominates.
    const int radius = n / 48;  // ~32 samples at 31.25 baud
    std::vector<double> smooth(static_cast<size_t>(n), 0.0);
    for (int k = 0; k < n; ++k) {
        double acc = 0.0;
        for (int d = -radius; d <= radius; ++d)
            acc += m_acqHist[static_cast<size_t>((k + d + n) % n)];
        smooth[static_cast<size_t>(k)] = acc;
    }

    int minIdx = 0;
    for (int k = 1; k < n; ++k)
        if (smooth[static_cast<size_t>(k)] < smooth[static_cast<size_t>(minIdx)])
            minIdx = k;

    // The one-pole lowpass delays the envelope by ~(1-a)/a samples, so
    // the true null sits that much earlier than the histogram minimum.
    // And in THIS modulator's convention the null marks the MIDDLE of a
    // symbol period, not a boundary: the raised-cosine transition ramps
    // across the whole period (constellation reaches the new point only
    // at period end), so a reversal crosses zero at the half-period
    // point and the constellation is stable exactly at the boundaries.
    // The period start is therefore half a symbol past the null.
    constexpr int LP_DELAY = 75;
    int boundary = (minIdx - LP_DELAY + n / 2 + n) % n;

    // Histogram index 0 coincides with the current sample position
    // (we finish exactly at a nominal period wrap), so the next true
    // boundary arrives `boundary` samples from now — start the window
    // that many samples short of full.
    m_sampleInSymbol = (n - boundary) % n;

    m_fallI = m_fallQ = m_riseI = m_riseQ = 0.0;
    m_frontEnergy = m_backEnergy = 0.0;
    m_timingBias  = 0.0;
    m_prevI = 1.0;
    m_prevQ = 0.0;
    m_acquiring = false;
}

void Psk31Demodulator::applyCostasCorrection(double i, double q) {
    // Standard decision-directed BPSK Costas loop error term, normalized
    // by the symbol magnitude so it approximates sin(phase error)
    // regardless of signal level. Unnormalized, the loop gain scaled
    // with amplitude — gains tuned at loopback level were an order of
    // magnitude too slow on a weak signal, so the loop never converged
    // exactly when tracking mattered most.
    double mag = std::sqrt(i * i + q * q);
    if (mag < 1e-12) return;
    double errorSign  = (i >= 0.0) ? 1.0 : -1.0;
    double phaseError = errorSign * q / mag;

    m_ncoFreqInc += PSK31_COSTAS_BETA * phaseError;

    // Clamp to +-60 Hz around nominal to prevent the loop from wandering
    // off onto an unrelated signal or noise. Matches the acquisition
    // search span: a station admitted by the DCD (within ~+-47 Hz) can
    // sit near the edge, and the loop still needs drift headroom there.
    double maxDev = 2.0 * M_PI * 60.0 / SAMPLE_RATE;
    if (m_ncoFreqInc > m_nominalFreqInc + maxDev) m_ncoFreqInc = m_nominalFreqInc + maxDev;
    if (m_ncoFreqInc < m_nominalFreqInc - maxDev) m_ncoFreqInc = m_nominalFreqInc - maxDev;

    m_ncoPhase += PSK31_COSTAS_ALPHA * phaseError;
}

float Psk31Demodulator::carrierOffsetHz() const {
    return static_cast<float>(
        m_ncoFreqInc * SAMPLE_RATE / (2.0 * M_PI) - m_carrierHz);
}

void Psk31Demodulator::nudgeCarrierHz(float deltaHz) {
    m_ncoFreqInc += 2.0 * M_PI * static_cast<double>(deltaHz) / SAMPLE_RATE;
    double maxDev = 2.0 * M_PI * 60.0 / SAMPLE_RATE;
    if (m_ncoFreqInc > m_nominalFreqInc + maxDev) m_ncoFreqInc = m_nominalFreqInc + maxDev;
    if (m_ncoFreqInc < m_nominalFreqInc - maxDev) m_ncoFreqInc = m_nominalFreqInc - maxDev;
    // Coherence state describes the old tuning; rebuild it. If the rig
    // lands somewhere other than commanded, the re-lock path in
    // processSample() cleans up after it.
    m_lockI = m_lockQ = 0.0;
    m_acqBuf.clear();
}

void Psk31Demodulator::reset() {
    m_ncoPhase       = 0.0;
    m_ncoFreqInc     = m_nominalFreqInc;
    m_fallI          = 0.0;
    m_fallQ          = 0.0;
    m_riseI          = 0.0;
    m_riseQ          = 0.0;
    m_sampleInSymbol = 0;
    m_frontEnergy    = 0.0;
    m_backEnergy     = 0.0;
    m_timingBias     = 0.0;
    m_symbolLenAdjusted = m_samplesPerSymbol;
    m_prevI          = 1.0;
    m_prevQ          = 0.0;
    m_lockQuality    = 0.0f;
    m_acquiring      = true;
    m_acqSymbolsDone = 0;
    m_acqLpI         = 0.0;
    m_acqLpQ         = 0.0;
    m_acqHist.assign(m_acqHist.size(), 0.0);
    m_acqBuf.clear();
}

} // namespace HavenFSK
