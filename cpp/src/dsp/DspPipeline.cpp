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

    // ── DCD update ────────────────────────────────────────────────────────
    bool dcdNow = m_dcd.update(corrected);
    if (dcdNow != m_dcdActive) {
        m_dcdActive = dcdNow;
        emit dcdChanged(m_dcdActive);

        if (m_dcdActive && m_rxState == RxState::Idle) {
            m_rxBuffer.clear();
            m_buffering = true;
            setRxState(RxState::Buffering);
            qDebug() << "DspPipeline: DCD on — buffering";
        }
        else if (!m_dcdActive && m_rxState == RxState::Buffering) {
            m_buffering = false;
            setRxState(RxState::Decoding);
            qDebug() << "DspPipeline: DCD off —"
                     << m_rxBuffer.size()
                     << "samples buffered, processing";
            processRxBuffer();
        }
    }

    if (m_transmitting)
        return;

    // ── Buffer raw audio while DCD active ─────────────────────────────────
    if (m_buffering) {
        m_rxBuffer.insert(m_rxBuffer.end(), corrected.begin(), corrected.end());

        if (static_cast<int>(m_rxBuffer.size()) > MAX_BUFFER_SAMPLES) {
            qWarning() << "DspPipeline: buffer limit reached — processing early";
            m_buffering = false;
            setRxState(RxState::Decoding);
            processRxBuffer();
        }
    }
}

// ── Buffered RX pipeline ──────────────────────────────────────────────────

void DspPipeline::processRxBuffer() {
    constexpr int minSamples = SAMPLES_PER_SYMBOL * PREAMBLE_LENGTH;
    if (static_cast<int>(m_rxBuffer.size()) < minSamples) {
        qDebug() << "DspPipeline: buffer too short —"
                 << m_rxBuffer.size() << "samples";
        resetRx();
        return;
    }

    qDebug() << "DspPipeline: processing buffer"
             << m_rxBuffer.size() << "samples ="
             << (m_rxBuffer.size() / static_cast<double>(SAMPLE_RATE)) << "s";

    // Step 1: Demodulate entire buffer to soft symbols (no AFC correction yet)
    auto softSymbols = m_demodulator.demodulateToSoft(m_rxBuffer);
    qDebug() << "DspPipeline: demodulated" << softSymbols.size() << "symbols";

    // Step 2: Hard decisions for preamble sliding-window search
    std::vector<int> hardSymbols;
    hardSymbols.reserve(softSymbols.size());
    for (const auto& e : softSymbols) {
        hardSymbols.push_back(static_cast<int>(
            std::max_element(e.begin(), e.end()) - e.begin()));
    }

    int   preambleOffset = -1;
    float bestScore      = 0.0f;
    for (int i = 0;
         i <= static_cast<int>(hardSymbols.size()) - PREAMBLE_LENGTH; ++i)
    {
        float score = m_preamble.correlate(hardSymbols, i);
        if (score > bestScore) {
            bestScore      = score;
            preambleOffset = i;
        }
    }

    qDebug() << "DspPipeline: best preamble score=" << bestScore
             << "at symbol offset=" << preambleOffset;

    if (bestScore < 0.375f || preambleOffset < 0) {
        qDebug() << "DspPipeline: no preamble found — discarding buffer";
        resetRx();
        return;
    }

    // Step 3: Compute AFC from preamble soft symbols using centroid method
    int preambleEnd = preambleOffset + PREAMBLE_LENGTH;
    std::vector<std::vector<float>> preambleSoft(
        softSymbols.begin() + preambleOffset,
        softSymbols.begin() + preambleEnd);
    float afcOffset = measureToneOffset(preambleSoft);
    m_afcOffsetHz = std::max(-AFC_MAX_HZ, std::min(AFC_MAX_HZ, afcOffset));
    m_afcPhase    = 0.0f;
    qDebug() << "DspPipeline: AFC offset=" << m_afcOffsetHz << "Hz";
    emit afcOffsetChanged(m_afcOffsetHz);

    // Step 4: Re-demodulate with AFC correction applied to raw buffer
    std::vector<std::vector<float>> correctedSoft;
    if (std::abs(m_afcOffsetHz) >= 0.5f) {
        std::vector<float> correctedAudio = m_rxBuffer;
        float phase = 0.0f;
        const float phaseInc = -2.0f * static_cast<float>(M_PI)
                               * m_afcOffsetHz
                               / static_cast<float>(SAMPLE_RATE);
        for (auto& s : correctedAudio) {
            s *= std::cos(phase);
            phase += phaseInc;
            if (phase >  static_cast<float>(M_PI))
                phase -= 2.0f * static_cast<float>(M_PI);
            if (phase < -static_cast<float>(M_PI))
                phase += 2.0f * static_cast<float>(M_PI);
        }
        correctedSoft = m_demodulator.demodulateToSoft(correctedAudio);
    } else {
        correctedSoft = std::move(softSymbols);
    }

    // Step 5: Extract frame symbols — everything after the preamble
    int frameStart = preambleOffset + PREAMBLE_LENGTH;
    if (frameStart >= static_cast<int>(correctedSoft.size())) {
        qDebug() << "DspPipeline: no frame symbols after preamble";
        resetRx();
        return;
    }

    std::vector<std::vector<float>> frameSymbols(
        correctedSoft.begin() + frameStart,
        correctedSoft.end());
    qDebug() << "DspPipeline: frame symbols=" << frameSymbols.size();

    // Step 6: Decode frame
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
    msg.fecIterations = 0;
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

void DspPipeline::applyAfcCorrection(std::vector<float>& samples) {
    if (!m_afcEnabled || std::abs(m_afcOffsetHz) < 0.5f) return;

    const float phaseInc = -2.0f * static_cast<float>(M_PI)
                           * m_afcOffsetHz
                           / static_cast<float>(SAMPLE_RATE);

    for (auto& s : samples) {
        s *= std::cos(m_afcPhase);
        m_afcPhase += phaseInc;
        while (m_afcPhase >  static_cast<float>(M_PI))
            m_afcPhase -= 2.0f * static_cast<float>(M_PI);
        while (m_afcPhase < -static_cast<float>(M_PI))
            m_afcPhase += 2.0f * static_cast<float>(M_PI);
    }
}

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

void DspPipeline::updateAfcTracking(
    const std::vector<std::vector<float>>& softSymbols)
{
    if (!m_afcEnabled) return;
    float measured = measureToneOffset(softSymbols);

    m_afcOffsetHz = (1.0f - AFC_TRACK_ALPHA) * m_afcOffsetHz
                  + AFC_TRACK_ALPHA * measured;

    m_afcOffsetHz = std::max(-AFC_MAX_HZ, std::min(AFC_MAX_HZ, m_afcOffsetHz));
    emit afcOffsetChanged(m_afcOffsetHz);
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
                 << (newState == RxState::Idle      ? "Idle"      :
                     newState == RxState::Buffering ? "Buffering" :
                                                      "Decoding");
    }
}

void DspPipeline::resetRx() {
    m_rxBuffer.clear();
    m_buffering = false;
    setRxState(RxState::Idle);
}

} // namespace HavenFSK
