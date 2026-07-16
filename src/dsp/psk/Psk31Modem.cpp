#include "Psk31Modem.h"
#include "../Constants.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace HavenFSK {

Psk31Modem::Psk31Modem(double baudRate, int variant)
    : m_baudRate(baudRate)
    , m_modulator(baudRate, PSK31_CARRIER_HZ)
    , m_demodulator(baudRate, PSK31_CARRIER_HZ)
{
    (void)variant;  // QPSK stretch goal — unimplemented, BPSK only for now
}

std::vector<float> Psk31Modem::modulateText(const std::string& text) {
    // Leading phase-reversal preamble (bit=0 repeated) so a receiving
    // station's Costas loop/AGC/timing recovery has something to lock
    // onto before real data arrives — see Psk31Constants.h for the
    // fldigi-matched symbol count and full rationale. The existing
    // Varicode decoder already treats a run of phase-reversal bits as
    // harmless idle (no RX-side change needed).
    std::vector<bool> bits(
        static_cast<size_t>(psk31PreambleSymbols(m_baudRate)), false);
    auto textBits = Varicode::encode(text);
    bits.insert(bits.end(), textBits.begin(), textBits.end());

    // Trailing postamble: steady carrier (bit=true, no phase transitions)
    // for the same symbol count as the leading preamble — matches
    // fldigi's tx_flush() postamble exactly (confirmed by reading
    // src/psk/psk.cxx's standard-BPSK case: `for (i<dcdbits) tx_symbol(2)`,
    // i.e. dcdbits repetitions of "0 degrees" = no phase change). Without
    // this, TX audio stopped the instant the last real bit's symbol
    // ended, giving the RX demodulator's matched-filter/timing-recovery
    // no settling time to fully process it — the last character (and
    // sometimes the one before it) was being silently lost. Safe for
    // Varicode: the real text's trailing "00" terminator was already sent
    // as part of textBits above, so these extra bit=true symbols just
    // accumulate harmlessly in the decoder (a run of 1s never forms a
    // "00" terminator) until the next DCD-drop reset clears them.
    std::vector<bool> postamble(
        static_cast<size_t>(psk31PreambleSymbols(m_baudRate)), true);
    bits.insert(bits.end(), postamble.begin(), postamble.end());

    return m_modulator.modulateBits(bits);
}

// Goertzel single-bin power estimate — the standard O(N) recurrence for
// one DFT bin, normalized by N^2 so results are comparable across chunk
// sizes. Frequency is used exactly (not snapped to an integer bin);
// leakage from non-integer bins is fine for a band-power *estimate*.
double Psk31Modem::goertzelPower(const std::vector<float>& x, double freqHz) {
    const double w = 2.0 * M_PI * freqHz / SAMPLE_RATE;
    const double coeff = 2.0 * std::cos(w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (float v : x) {
        s0 = static_cast<double>(v) + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    double n = static_cast<double>(x.size());
    return (n > 0.0) ? power / (n * n) : 0.0;
}

// Narrowband DCD: average per-bin Goertzel power inside the PSK passband
// vs two adjacent noise-reference bands, IIR-smoothed across chunks,
// with hysteresis + hang. See the header for threshold rationale.
bool Psk31Modem::updateDcd(const std::vector<float>& samples) {
    const double hw = psk31PassbandHalfWidthHz(m_baudRate);
    // Bin spacing ~ the chunk's natural resolution (48000/2048 ≈ 23 Hz).
    const double step = static_cast<double>(SAMPLE_RATE) / AUDIO_CHUNK_SAMPLES;

    double inBand = 0.0;
    int    inBins = 0;
    for (double f = PSK31_CARRIER_HZ - hw; f <= PSK31_CARRIER_HZ + hw + 1e-9;
         f += step) {
        inBand += goertzelPower(samples, f);
        ++inBins;
    }

    // Noise reference: a band of the same width on each side, offset by
    // 2x the half-width so the signal's own skirts don't contaminate it.
    double outBand = 0.0;
    int    outBins = 0;
    for (double f = PSK31_CARRIER_HZ - 2.0 * hw - hw;
         f <= PSK31_CARRIER_HZ - 2.0 * hw + 1e-9; f += step) {
        outBand += goertzelPower(samples, f);
        ++outBins;
    }
    for (double f = PSK31_CARRIER_HZ + 2.0 * hw;
         f <= PSK31_CARRIER_HZ + 2.0 * hw + hw + 1e-9; f += step) {
        outBand += goertzelPower(samples, f);
        ++outBins;
    }

    double inAvg  = (inBins  > 0) ? inBand  / inBins  : 0.0;
    double outAvg = (outBins > 0) ? outBand / outBins : 0.0;
    double ratioDb = 10.0 * std::log10((inAvg + 1e-30) / (outAvg + 1e-30));

    m_dcdSnrDb += DCD_SMOOTH_ALPHA * (static_cast<float>(ratioDb) - m_dcdSnrDb);

    if (m_dcdActive) {
        if (m_dcdSnrDb < DCD_OFF_DB) {
            if (--m_dcdHangLeft <= 0) m_dcdActive = false;
        } else {
            m_dcdHangLeft = DCD_HANG_CHUNKS;
        }
    } else if (m_dcdSnrDb >= DCD_ON_DB) {
        m_dcdActive   = true;
        m_dcdHangLeft = DCD_HANG_CHUNKS;
    }
    return m_dcdActive;
}

std::vector<ModemRxEvent> Psk31Modem::processAudioChunk(
    const std::vector<float>& samples)
{
    std::vector<ModemRxEvent> events;

    // Squelch, stage 1: narrowband DCD. No carrier in the passband ->
    // don't demodulate at all. Feeding pure noise through the Costas
    // loop/Varicode decoder is exactly what produced a constant stream
    // of garbage characters between real transmissions. Reset decode
    // state on the falling edge (after the hang time expires) so a
    // genuine signal starts clean rather than resuming mid-noise state.
    bool wasActive = m_dcdActive;
    updateDcd(samples);
    m_rxState = m_dcdActive ? ModemRxState::Collecting : ModemRxState::Idle;

    if (!m_dcdActive) {
        if (wasActive) {
            m_demodulator.reset();
            m_varicode.reset();
            m_qualitySum = 0.0f;
            m_qualityCount = 0;
        }
        return events;
    }

    m_lastCarrierOffsetHz = m_demodulator.carrierOffsetHz();

    for (size_t i = 0; i < samples.size(); ++i) {
        auto symResult = m_demodulator.processSample(samples[i]);
        if (!symResult.bitReady) continue;

        m_qualitySum += symResult.lockQuality;
        ++m_qualityCount;

        auto ch = m_varicode.decodeBit(symResult.bit);
        if (!ch.has_value() && m_varicode.atBoundary()) {
            // Codeword boundary with nothing decoded: idle bits between
            // characters (preamble/postamble runs) or a discarded
            // invalid/oversized accumulation. Either way these bits are
            // not part of any character — drop their quality from the
            // window, or a long high-quality idle run (the ~1 s steady
            // postamble especially) inflates the average of whatever
            // noise-decoded "character" follows it, which is exactly how
            // 1-2 garbage characters were passing squelch at the tail of
            // every strong transmission.
            m_qualitySum   = 0.0f;
            m_qualityCount = 0;
        }
        if (ch.has_value()) {
            // Squelch, stage 2: average lock quality across the bits that
            // made up this character. Even with DCD gating there, a weak
            // or marginal signal can still decode-but-be-garbage; this
            // catches that case per-character rather than all-or-nothing.
            float avgQuality = (m_qualityCount > 0)
                ? m_qualitySum / static_cast<float>(m_qualityCount) : 0.0f;
            m_qualitySum = 0.0f;
            m_qualityCount = 0;

            if (avgQuality < m_squelchThreshold) continue;

            ModemRxEvent ev;
            ev.hasMessage      = true;
            ev.text            = std::string(1, *ch);
            ev.isFramedMessage = false;  // continuous stream, not a discrete message
            // PSK31 has no CRC/FEC — crcOk/converged/fecIterations stay
            // at their ModemRxEvent defaults (false/false/0), and are
            // ignored downstream since isFramedMessage is false.
            events.push_back(ev);
        }
    }

    return events;
}

void Psk31Modem::resetRx() {
    m_demodulator.reset();
    m_varicode.reset();
    m_rxState      = ModemRxState::Idle;
    m_dcdActive    = false;
    m_dcdSnrDb     = 0.0f;
    m_dcdHangLeft  = 0;
    m_qualitySum   = 0.0f;
    m_qualityCount = 0;
}

} // namespace HavenFSK
