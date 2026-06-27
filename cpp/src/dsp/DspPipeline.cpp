#include "DspPipeline.h"
#include "FEC.h"
#include "../radio/RadioSettings.h"
#include <QDateTime>
#include <QRegularExpression>
#include <QDebug>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace HavenFSK {

DspPipeline::DspPipeline(QObject* parent)
    : QObject(parent)
{
    m_rxBuffer.reserve(SAMPLE_RATE * 5);  // pre-allocate 5 seconds

    // Demodulator self-test: score a locally-generated preamble.
    // Clean-signal achievable score is ~0.65-0.70 (not 1.0) because softCorrelate
    // divides correct-tone energy by sum of all 16 guard windows. Threshold 0.5
    // catches real failures (wrong bin map, FFT config) without false-alarming.
    {
        auto preambleAudio = m_preamble.generate();
        auto softSyms = m_demodulator.demodulateToSoft(preambleAudio, 0);
        float score = m_preamble.softCorrelate(softSyms, 0);
        qDebug() << "DspPipeline: demod self-test score=" << score
                 << (score >= 0.5f ? "[PASS]" : "[FAIL — demodulator bug]");
        if (score < 0.5f) {
            QString got;
            for (int i = 0; i < PREAMBLE_LENGTH && i < (int)softSyms.size(); ++i) {
                const auto& e = softSyms[i];
                int best = (int)(std::max_element(e.begin(), e.end()) - e.begin());
                got += QString::number(best) + " ";
            }
            qDebug() << "  self-test got :" << got;
            qDebug() << "  self-test want: 0 15 0 15 7 8 7 8 0 15 0 15 7 8 7 8";
        }
    }
}

// ── TX ────────────────────────────────────────────────────────────────────

bool DspPipeline::transmit(const QString& text) {
    if (m_transmitting) {
        qWarning() << "DspPipeline: transmit called while already transmitting";
        return false;
    }
    if (text.trimmed().isEmpty()) {
        qWarning() << "DspPipeline: transmit called with empty text";
        return false;
    }
    m_transmitting = true;
    m_lastTxText = text;
    emit messageTransmitted(text);
    std::vector<float> audio = m_frame.assemble(text.toStdString());
    qDebug() << "DspPipeline: TX" << text.length() << "chars ="
             << audio.size() << "samples ="
             << (audio.size() / static_cast<double>(SAMPLE_RATE)) << "seconds";
    emit txAudioReady(audio);
    return true;
}

void DspPipeline::onTxComplete() {
    m_transmitting = false;
    qDebug() << "DspPipeline: TX complete";
}

// ── RX ────────────────────────────────────────────────────────────────────

void DspPipeline::onAudioChunk(const std::vector<float>& samples) {
    // Apply RX gain. Waterfall gets raw audio via its own direct connection.
    std::vector<float> corrected = samples;
    if (std::abs(m_rxGain - 1.0f) > 0.001f)
        for (float& s : corrected) s *= m_rxGain;

    // DCD — advisory UI indicator only; does not gate the RX pipeline.
    bool dcdNow = m_dcd.update(corrected);
    if (dcdNow != m_dcdActive) {
        m_dcdActive = dcdNow;
        emit dcdChanged(m_dcdActive);
    }

    if (m_transmitting) return;

    // ── Collecting ────────────────────────────────────────────────────────
    if (m_rxState == RxState::Collecting) {
        m_rxBuffer.insert(m_rxBuffer.end(), corrected.begin(), corrected.end());

        // Keep m_preTrigger current so that preamble detection can resume
        // immediately after this frame completes with no dead zone.
        m_preTrigger.insert(m_preTrigger.end(), corrected.begin(), corrected.end());
        if (static_cast<int>(m_preTrigger.size()) > PRE_TRIGGER_SAMPLES)
            m_preTrigger.erase(m_preTrigger.begin(),
                               m_preTrigger.end() - PRE_TRIGGER_SAMPLES);

        if (static_cast<int>(m_rxBuffer.size()) > MAX_BUFFER_SAMPLES) {
            qWarning() << "DspPipeline: buffer limit — resetting";
            resetRx();
            return;
        }
        ++m_collectTicks;
        tryCompleteFrame();
        if (m_collectTicks > COLLECT_TIMEOUT_CHUNKS) {
            qDebug() << "DspPipeline: collect timeout — discarding";
            resetRx();
        }
        return;
    }

    // ── Idle: rolling buffer + continuous preamble scan ───────────────────
    m_preTrigger.insert(m_preTrigger.end(), corrected.begin(), corrected.end());
    if (static_cast<int>(m_preTrigger.size()) > PRE_TRIGGER_SAMPLES)
        m_preTrigger.erase(m_preTrigger.begin(),
                           m_preTrigger.end() - PRE_TRIGGER_SAMPLES);

    if (++m_scanTicks % SCAN_INTERVAL_CHUNKS == 0) {
        // Snapshot the rolling buffer for the preamble scan.
        // tryFindPreamble() transitions to Collecting if preamble is found,
        // at which point m_rxBuffer grows with new chunks above.
        // If not found, m_rxBuffer is overwritten next scan tick.
        m_rxBuffer = m_preTrigger;
        tryFindPreamble();
    }
}

// ── Preamble-triggered RX pipeline ───────────────────────────────────────

void DspPipeline::tryFindPreamble() {
    if (static_cast<int>(m_rxBuffer.size()) <
        SAMPLES_PER_SYMBOL * PREAMBLE_LENGTH)
        return;

    // Periodic audio-level log — confirms audio is reaching the demodulator.
    if (m_scanTicks % 40 == 0) {
        float peak = 0.0f;
        for (float s : m_rxBuffer) peak = std::max(peak, std::abs(s));
        float dbFS = (peak > 1e-10f) ? 20.0f * std::log10(peak) : -96.0f;
        qDebug() << "DspPipeline: scan buffer peak" << dbFS << "dBFS";
    }

    // Multi-hypothesis preamble scan.
    //
    // Guard bins cover only ±11.7 Hz (3 bins × 3.906 Hz/bin).  Two separate
    // radios tuned to the same dial frequency can still differ by ±20-40 Hz
    // at HF due to oscillator tolerance (e.g. 2 ppm at 14 MHz = ±28 Hz).
    //
    // We always try five frequency hypotheses spaced 20 Hz apart, covering
    // ±51 Hz continuously.  When AFC is enabled the search is centred on the
    // last measured offset (so the window tracks the radio).  When AFC is
    // disabled the search is centred on zero — each frame starts fresh with
    // no cross-frame history.  Either way, bin shifting in the FFT domain
    // avoids the double-sideband artefact of real-valued frequency mixing.
    float hypCenter = m_afcEnabled ? m_afcOffsetHz : 0.0f;
    const std::vector<float> HYP_HZ = {
        hypCenter,
        hypCenter + 20.0f, hypCenter - 20.0f,
        hypCenter + 40.0f, hypCenter - 40.0f,
    };

    constexpr int TIMING_STEPS        = 8;
    constexpr int TIMING_STEP_SAMPLES = SAMPLES_PER_SYMBOL / TIMING_STEPS;

    int   bestSymOff  = -1;
    float bestScore   = 0.0f;
    int   bestTiming  = 0;
    float bestHypHz   = 0.0f;

    for (float hypHz : HYP_HZ) {
        int afcBin = static_cast<int>(
            std::round(hypHz * FFT_SIZE / SAMPLE_RATE));

        // Coarse filter — hard-decision scan at timing offset 0.
        auto softCoarse = m_demodulator.demodulateToSoft(m_rxBuffer, 0, afcBin);
        if (softCoarse.empty()) continue;

        std::vector<int> hardCoarse;
        hardCoarse.reserve(softCoarse.size());
        for (const auto& e : softCoarse)
            hardCoarse.push_back(static_cast<int>(
                std::max_element(e.begin(), e.end()) - e.begin()));

        float coarseBest = 0.0f;
        for (int i = 0;
             i <= static_cast<int>(hardCoarse.size()) - PREAMBLE_LENGTH; ++i)
        {
            float s = m_preamble.correlate(hardCoarse, i);
            if (s > coarseBest) coarseBest = s;
        }
        if (coarseBest < COARSE_PREAMBLE_THRESHOLD) continue;

        // Fine scan — 8-step timing sweep within this AFC hypothesis.
        for (int t = 0; t < TIMING_STEPS; ++t) {
            int sOff = t * TIMING_STEP_SAMPLES;
            auto softTry = m_demodulator.demodulateToSoft(
                m_rxBuffer, sOff, afcBin);
            if (softTry.empty()) continue;

            for (int i = 0;
                 i <= static_cast<int>(softTry.size()) - PREAMBLE_LENGTH; ++i)
            {
                float score = m_preamble.softCorrelate(softTry, i);
                if (score > bestScore) {
                    bestScore  = score;
                    bestSymOff = i;
                    bestTiming = sOff;
                    bestHypHz  = hypHz;
                }
            }
        }
    }

    // Log preamble scan result periodically, or whenever a strong candidate
    // appears.  Running this every scan tick (every 340 ms) adds one full
    // demodulation pass (93 FFTs) plus four OutputDebugString calls, which
    // on Windows blocks the event loop and lags the waterfall.
    // Gate: print every ~2 s (every 48 chunks) OR if score is near threshold.
    const bool logThisScan =
        (m_scanTicks % 48 == 0) ||
        (bestScore >= SOFT_PREAMBLE_THRESHOLD * 0.8f);

    if (logThisScan) {
        int afcBin = static_cast<int>(
            std::round(bestHypHz * FFT_SIZE / SAMPLE_RATE));
        int safeOff = (bestSymOff >= 0) ? bestSymOff : 0;
        auto softBestDbg = m_demodulator.demodulateToSoft(
            m_rxBuffer, bestTiming, afcBin);
        QString got, want, frac, maxf;
        for (int i = 0; i < PREAMBLE_LENGTH; ++i) {
            int idx = safeOff + i;
            if (idx < 0 || idx >= static_cast<int>(softBestDbg.size())) {
                got  += "? ";
                want += QString::number(PREAMBLE_SYMBOLS[i]) + " ";
                frac += "? ";
                maxf += "? ";
                continue;
            }
            const auto& e = softBestDbg[idx];
            int argMax = static_cast<int>(
                std::max_element(e.begin(), e.end()) - e.begin());
            float total = 0.0f;
            for (float x : e) total += x;
            float f  = (total > 1e-10f) ? e[PREAMBLE_SYMBOLS[i]] / total : 0.0f;
            float mf = (total > 1e-10f) ? e[argMax]               / total : 0.0f;
            got  += QString::number(argMax) + " ";
            want += QString::number(PREAMBLE_SYMBOLS[i]) + " ";
            frac += QString::number(f,  'f', 2) + " ";
            maxf += QString::number(mf, 'f', 2) + " ";
        }
        qDebug() << "DspPipeline: preamble scan: soft=" << bestScore
                 << "sym=" << bestSymOff << "sampleOff=" << bestTiming
                 << "afc=" << bestHypHz << "Hz";
        qDebug() << "  got :" << got;
        qDebug() << "  want:" << want;
        qDebug() << "  frac:" << frac;
        qDebug() << "  maxf:" << maxf;  // energy fraction at the *actual* dominant tone
    }

    if (bestScore < SOFT_PREAMBLE_THRESHOLD || bestSymOff < 0)
        return;

    // Refine frequency estimate from preamble soft symbols.
    // bestHypHz is the coarse hypothesis (20 Hz grid); measureToneOffset()
    // returns the sub-bin residual so the frame demodulator gets the best
    // possible bin alignment regardless of whether AFC tracking is on.
    {
        int afcBin = static_cast<int>(
            std::round(bestHypHz * FFT_SIZE / SAMPLE_RATE));
        auto softBest = m_demodulator.demodulateToSoft(
            m_rxBuffer, bestTiming, afcBin);
        int preambleEnd = bestSymOff + PREAMBLE_LENGTH;
        if (preambleEnd <= static_cast<int>(softBest.size())) {
            std::vector<std::vector<float>> preambleSoft(
                softBest.begin() + bestSymOff,
                softBest.begin() + preambleEnd);
            float residual = measureToneOffset(preambleSoft);
            float total    = bestHypHz + residual;

            // Bin offset used for all symbol decoding in this frame.
            // Quantised to integer FFT bins (3.906 Hz/bin); guard window
            // (±3 bins = ±11.7 Hz) absorbs any remaining sub-bin error.
            m_demodBinOffset = static_cast<int>(
                std::round(total * FFT_SIZE / SAMPLE_RATE));

            if (m_afcEnabled) {
                // Persist the measured offset as the centre for the NEXT
                // frame's hypothesis scan (slow frequency tracking).
                m_afcOffsetHz = std::max(-AFC_MAX_HZ,
                                std::min(AFC_MAX_HZ, total));
                qDebug() << "DspPipeline: AFC hyp=" << bestHypHz
                         << "residual=" << residual
                         << "total=" << m_afcOffsetHz << "Hz"
                         << "binOff=" << m_demodBinOffset;
                emit afcOffsetChanged(m_afcOffsetHz);
            } else {
                qDebug() << "DspPipeline: AFC disabled — hyp=" << bestHypHz
                         << "residual=" << residual
                         << "total=" << total << "Hz"
                         << "binOff=" << m_demodBinOffset << "(not persisted)";
            }
        }
    }

    m_timingOffset     = bestTiming;
    m_preambleSymOff   = bestSymOff;
    m_nBlocks          = -1;
    m_symsNeeded       = 0;
    m_collectTicks     = 0;
    m_lastCheckSamples = 0;

    qDebug() << "DspPipeline: preamble found (score=" << bestScore
             << ") — collecting frame";
    emit preambleDetected(bestScore);
    setRxState(RxState::Collecting);
    tryCompleteFrame();
}

void DspPipeline::tryCompleteFrame() {
    // Throttle: only re-process when at least one new symbol has arrived
    int currentSamples = static_cast<int>(m_rxBuffer.size());
    if (currentSamples - m_lastCheckSamples < SAMPLES_PER_SYMBOL &&
        m_nBlocks > 0)
        return;
    m_lastCheckSamples = currentSamples;

    // Demodulate the entire frame at the bin offset found during preamble
    // detection.  Bin-shifting in the FFT domain is exact and produces no
    // sidebands (unlike the cosine-multiply approach which doubles each tone).
    auto softSymbols = m_demodulator.demodulateToSoft(
        m_rxBuffer, m_timingOffset, m_demodBinOffset);
    int  frameStart  = m_preambleSymOff + PREAMBLE_LENGTH;

    // Need both header copies before we know frame length (8 symbols)
    constexpr int HDR_TOTAL = 8;
    if (static_cast<int>(softSymbols.size()) < frameStart + HDR_TOTAL) {
        qDebug() << "DspPipeline: waiting for header —"
                 << softSymbols.size() << "syms, need"
                 << (frameStart + HDR_TOTAL);
        return;
    }

    // Decode header if not yet done
    if (m_nBlocks < 0) {
        auto argmx = [](const std::vector<float>& e) {
            return static_cast<int>(
                std::max_element(e.begin(), e.end()) - e.begin());
        };
        int s = frameStart;
        // Copy 1
        uint8_t b0a = static_cast<uint8_t>(
            (argmx(softSymbols[s+0]) << 4) | argmx(softSymbols[s+1]));
        uint8_t b1a = static_cast<uint8_t>(
            (argmx(softSymbols[s+2]) << 4) | argmx(softSymbols[s+3]));
        // Copy 2
        uint8_t b1b = static_cast<uint8_t>(
            (argmx(softSymbols[s+6]) << 4) | argmx(softSymbols[s+7]));

        if (b1a != b1b)
            qDebug() << "DspPipeline: header nBlocks mismatch:"
                     << b1a << "vs" << b1b << "— using copy 1";

        m_nBlocks = b1a;

        constexpr int MAX_NBLOCKS = 125;
        if (m_nBlocks <= 0 || m_nBlocks > MAX_NBLOCKS) {
            qDebug() << "DspPipeline: nBlocks" << m_nBlocks
                     << "out of range [1," << MAX_NBLOCKS << "] — discarding";
            resetRx();
            return;
        }

        m_symsNeeded = frameStart + Frame::frameSymsNeeded(m_nBlocks);

        qDebug() << "DspPipeline: header decoded nBlocks=" << m_nBlocks
                 << "need" << m_symsNeeded << "total symbols";
    }

    emit rxProgress(static_cast<int>(softSymbols.size()), m_symsNeeded);

    if (static_cast<int>(softSymbols.size()) < m_symsNeeded) {
        qDebug() << "DspPipeline: collecting"
                 << softSymbols.size() << "/" << m_symsNeeded;
        return;
    }

    // Complete frame available — extract and decode
    qDebug() << "DspPipeline: frame complete — decoding";
    std::vector<std::vector<float>> frameSymbols(
        softSymbols.begin() + frameStart,
        softSymbols.begin() + m_symsNeeded);

    processFrame(frameSymbols);
    resetRx();
}

void DspPipeline::processFrame(
    const std::vector<std::vector<float>>& softSymbols)
{
    if (softSymbols.empty()) return;

    qDebug() << "DspPipeline: processing frame,"
             << softSymbols.size() << "symbols";

    ParseResult result = m_frame.parse(softSymbols);

    if (!result.error.empty()) {
        qDebug() << "DspPipeline: frame parse error:"
                 << result.error.c_str();
        return;
    }

    if (!result.crcOk) {
        qDebug() << "DspPipeline: CRC failed — frame discarded";
        return;
    }

    RxMessage msg;
    msg.text          = QString::fromStdString(result.text);
    msg.crcOk         = result.crcOk;
    msg.converged     = result.converged;
    msg.nBlocks       = result.nBlocks;
    msg.fecIterations = result.fecIterations;
    msg.snr           = m_dcd.lastSnrDb();

    HavenFSK::StationInfo info = HavenFSK::loadStationInfo();
    msg.senderCallsign = parseSenderCallsign(msg.text, info.callsign);

    updateRxCache(msg);
    expireRxCache();

    qDebug() << "DspPipeline: decoded message:" << msg.text
             << "(CRC OK, FEC converged:" << msg.converged
             << ", sender:" << msg.senderCallsign << ")";

    emit messageReceived(msg);
}

// ── AFC implementation ────────────────────────────────────────────────────

float DspPipeline::measureToneOffset(
    const std::vector<std::vector<float>>& softSymbols) const
{
    if (softSymbols.empty()) return 0.0f;
    float totalOffset = 0.0f;
    int   count       = 0;

    for (const auto& energies : softSymbols) {
        if (static_cast<int>(energies.size()) != NUM_TONES) continue;

        int winner = 0;
        for (int i = 1; i < NUM_TONES; i++)
            if (energies[i] > energies[winner]) winner = i;

        float eL = (winner > 0) ? energies[winner - 1] : 0.0f;
        float eR = (winner < NUM_TONES - 1) ? energies[winner + 1] : 0.0f;
        float eS = eL + energies[winner] + eR;
        if (eS < 1e-10f) continue;

        float centroid = (eR - eL) / eS;
        totalOffset += centroid * static_cast<float>(SYMBOL_RATE);
        count++;
    }

    return (count > 0) ? (totalOffset / count) : 0.0f;
}

// ── RS measurement cache ──────────────────────────────────────────────────

const RxMeasurement* DspPipeline::getRxMeasurement(
    const QString& callsign) const
{
    auto it = m_rxCache.find(callsign.toUpper());
    if (it == m_rxCache.end()) return nullptr;
    if (it->timestamp.secsTo(QDateTime::currentDateTime())
        > RS_CACHE_MINUTES * 60) return nullptr;
    return &(*it);
}

QString DspPipeline::computeRS(const RxMeasurement& m) {
    int r = 1;
    if (m.converged) {
        if      (m.fecIterations < 50)  r = 5;
        else if (m.fecIterations < 150) r = 4;
        else                            r = 3;
    }
    int s = 1;
    if      (m.snrDb >= 26) s = 9;
    else if (m.snrDb >= 21) s = 8;
    else if (m.snrDb >= 15) s = 7;
    else if (m.snrDb >= 12) s = 6;
    else if (m.snrDb >=  9) s = 5;
    else if (m.snrDb >=  6) s = 4;
    else if (m.snrDb >=  3) s = 3;
    else if (m.snrDb >=  0) s = 2;
    else                    s = 1;
    return QString("%1%2").arg(r).arg(s);
}

QString DspPipeline::parseSenderCallsign(const QString& text,
                                          const QString& myCallsign)
{
    QStringList words = text.toUpper().split(' ', Qt::SkipEmptyParts);
    QString myCall = myCallsign.toUpper();
    static QRegularExpression callRe(
        "^[A-Z0-9]{1,3}[0-9][A-Z0-9]{0,3}[A-Z]$");

    for (int i = 1; i < words.size() - 1; i++) {
        if (words[i] == "DE") {
            QString before = words[i - 1];
            QString after  = words[i + 1];
            if (callRe.match(before).hasMatch() && before != myCall)
                return before;
            if (callRe.match(after).hasMatch() && after != myCall)
                return after;
        }
    }
    for (const QString& word : words) {
        if (callRe.match(word).hasMatch() && word != myCall)
            return word;
    }
    return QString();
}

void DspPipeline::updateRxCache(const RxMessage& msg) {
    if (msg.senderCallsign.isEmpty()) return;
    RxMeasurement m;
    m.snrDb         = msg.snr;
    m.fecIterations = msg.fecIterations;
    m.converged     = msg.converged;
    m.timestamp     = QDateTime::currentDateTime();
    m_rxCache[msg.senderCallsign.toUpper()] = m;
}

void DspPipeline::expireRxCache() {
    auto now = QDateTime::currentDateTime();
    for (auto it = m_rxCache.begin(); it != m_rxCache.end(); ) {
        if (it->timestamp.secsTo(now) > RS_CACHE_MINUTES * 60)
            it = m_rxCache.erase(it);
        else
            ++it;
    }
}

// ── Helpers ───────────────────────────────────────────────────────────────

void DspPipeline::setRxState(RxState newState) {
    if (m_rxState != newState) {
        m_rxState = newState;
        emit rxStateChanged(m_rxState);
        qDebug() << "DspPipeline: RX state ->"
                 << (newState == RxState::Idle ? "Idle" : "Collecting");
    }
}

void DspPipeline::resetRx() {
    m_rxBuffer.clear();
    // m_preTrigger is intentionally preserved — it contains audio that arrived
    // during collection and is already stale-preamble-free.  Clearing it would
    // create a ~680 ms window where no preamble can be detected.
    m_timingOffset     = 0;
    m_preambleSymOff   = -1;
    m_nBlocks          = -1;
    m_symsNeeded       = 0;
    m_demodBinOffset   = 0;
    m_scanTicks        = SCAN_INTERVAL_CHUNKS - 1;  // fire next chunk, not in 8
    m_collectTicks     = 0;
    m_lastCheckSamples = 0;
    setRxState(RxState::Idle);
}

} // namespace HavenFSK
