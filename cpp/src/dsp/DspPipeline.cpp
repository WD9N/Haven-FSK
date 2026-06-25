#include "DspPipeline.h"
#include "FEC.h"
#include "../radio/RadioSettings.h"
#include <QDateTime>
#include <QRegularExpression>
#include <QDebug>
#include <algorithm>
#include <cmath>

namespace HavenFSK {

DspPipeline::DspPipeline(QObject* parent)
    : QObject(parent)
{
    m_sampleAccum.reserve(SAMPLES_PER_SYMBOL * 2);
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
    m_lastTxText = text;            // store for CQ detection
    emit messageTransmitted(text);  // notify UI before assembling
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
    // Apply RX gain then AFC correction.
    // Waterfall gets raw audio via its own direct connection (unaffected).
    std::vector<float> corrected = samples;
    if (std::abs(m_rxGain - 1.0f) > 0.001f)
        for (float& s : corrected) s *= m_rxGain;
    applyAfcCorrection(corrected);

    // ── DCD update ────────────────────────────────────────────────────────
    bool dcdNow = m_dcd.update(corrected);
    if (dcdNow != m_dcdActive) {
        m_dcdActive = dcdNow;
        emit dcdChanged(m_dcdActive);

        if (!m_dcdActive && m_rxState != RxState::Idle) {
            if (m_rxState == RxState::Receiving && !m_symbolAccum.empty())
                processFrame();
            // Partial reset: keep half the correction for the next station
            m_afcOffsetHz *= AFC_RESET_DECAY;
            m_afcPhase = 0.0f;
            emit afcOffsetChanged(m_afcOffsetHz);
            resetRx();
        }

        if (m_dcdActive && m_rxState == RxState::Idle)
            setRxState(RxState::Searching);
    }

    if (m_rxState == RxState::Idle || m_transmitting)
        return;

    // ── Accumulate samples into symbol-sized blocks ────────────────────────
    m_sampleAccum.insert(m_sampleAccum.end(),
                          corrected.begin(), corrected.end());

    while (static_cast<int>(m_sampleAccum.size()) >= SAMPLES_PER_SYMBOL) {
        std::vector<float> symbolBlock(
            m_sampleAccum.begin(),
            m_sampleAccum.begin() + SAMPLES_PER_SYMBOL);
        m_sampleAccum.erase(
            m_sampleAccum.begin(),
            m_sampleAccum.begin() + SAMPLES_PER_SYMBOL);

        auto softSymbols = m_demodulator.demodulateToSoft(symbolBlock);
        if (softSymbols.empty()) continue;
        processSymbol(softSymbols[0]);
    }
}

void DspPipeline::processSymbol(const std::vector<float>& softEnergies) {
    switch (m_rxState) {

    case RxState::Searching: {
        m_searchWindow.push_back(softEnergies);

        if (static_cast<int>(m_searchWindow.size()) > PREAMBLE_LENGTH * 2)
            m_searchWindow.erase(m_searchWindow.begin());

        if (static_cast<int>(m_searchWindow.size()) >= PREAMBLE_LENGTH) {
            float score = 0.0f;
            if (m_preamble.detect(m_searchWindow, score)) {
                qDebug() << "DspPipeline: preamble DETECTED score=" << score;
                emit preambleDetected(score);

                m_symbolAccum.clear();
                m_expectedSymbols = MAX_FRAME_SYMBOLS;
                setRxState(RxState::Receiving);

                // AFC hard lock from preamble symbols — immediate correction,
                // phase reset. Handles the jump when a new station calls.
                if (m_afcEnabled && !m_searchWindow.empty()) {
                    float offset = measureToneOffset(m_searchWindow);
                    m_afcOffsetHz = std::max(-AFC_MAX_HZ,
                                    std::min(AFC_MAX_HZ, offset));
                    m_afcPhase = 0.0f;
                    qDebug() << "DspPipeline: AFC hard lock"
                             << m_afcOffsetHz << "Hz";
                    emit afcOffsetChanged(m_afcOffsetHz);
                }
            } else {
                qDebug() << "DspPipeline: preamble miss score=" << score;
            }
        }
        break;
    }

    case RxState::Receiving: {
        m_symbolAccum.push_back(softEnergies);

        emit rxProgress(static_cast<int>(m_symbolAccum.size()),
                        m_expectedSymbols);

        // Header peek to refine expected symbol count
        if (static_cast<int>(m_symbolAccum.size()) == 8) {
            auto argmax = [](const std::vector<float>& v) {
                return static_cast<int>(std::distance(
                    v.begin(), std::max_element(v.begin(), v.end())));
            };
            int s2 = argmax(m_symbolAccum[2]);
            int s3 = argmax(m_symbolAccum[3]);
            int nBlocks = (s2 << 4) | s3;
            if (nBlocks > 0 && nBlocks <= 32) {
                m_expectedSymbols = 4 + 4 + nBlocks * 48;
                qDebug() << "DspPipeline: header peek — nBlocks ="
                         << nBlocks << ", expecting"
                         << m_expectedSymbols << "total symbols";
            } else {
                m_expectedSymbols = MAX_FRAME_SYMBOLS;
            }
        }

        // Slow AFC tracking every 8 symbols using 16 recent symbols
        if (static_cast<int>(m_symbolAccum.size()) % 8 == 0 &&
            static_cast<int>(m_symbolAccum.size()) >= 16)
        {
            int start = std::max(0,
                static_cast<int>(m_symbolAccum.size()) - 16);
            std::vector<std::vector<float>> recent(
                m_symbolAccum.begin() + start,
                m_symbolAccum.end());
            updateAfcTracking(recent);
        }

        if (static_cast<int>(m_symbolAccum.size()) >= m_expectedSymbols &&
            m_expectedSymbols > 8) {
            processFrame();
            resetRx();
            if (m_dcdActive) setRxState(RxState::Searching);
        }

        if (static_cast<int>(m_symbolAccum.size()) >= MAX_FRAME_SYMBOLS) {
            qWarning() << "DspPipeline: frame too long, giving up";
            processFrame();
            resetRx();
            if (m_dcdActive) setRxState(RxState::Searching);
        }
        break;
    }

    case RxState::Idle:
    default:
        break;
    }
}

void DspPipeline::processFrame() {
    if (m_symbolAccum.empty()) return;

    qDebug() << "DspPipeline: processing frame,"
             << m_symbolAccum.size() << "symbols";

    ParseResult result = m_frame.parse(m_symbolAccum);

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

    // NCO: multiply by cos(-2π * offset * n / Fs)
    // Shifts received audio spectrum by -afcOffsetHz to compensate offset.
    // TX path is completely unaffected — Modulator never uses this audio.
    const float phaseInc = -2.0f * static_cast<float>(M_PI)
                           * m_afcOffsetHz
                           / static_cast<float>(SAMPLE_RATE);

    for (auto& s : samples) {
        s *= std::cos(m_afcPhase);
        m_afcPhase += phaseInc;
        // Wrap to [-π, π] to prevent float precision loss
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

        // Find winning tone
        int winner = 0;
        for (int i = 1; i < NUM_TONES; i++)
            if (energies[i] > energies[winner]) winner = i;

        // Centroid of winner and neighbors to estimate sub-tone offset
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

    // Slow exponential average — responds over ~50 symbol periods (~1.6 s).
    // Alpha=0.02 tracks thermal drift only, not noise fluctuations.
    m_afcOffsetHz = (1.0f - AFC_TRACK_ALPHA) * m_afcOffsetHz
                  + AFC_TRACK_ALPHA * measured;

    m_afcOffsetHz = std::max(-AFC_MAX_HZ,
                    std::min(AFC_MAX_HZ, m_afcOffsetHz));

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
                     newState == RxState::Searching ? "Searching" :
                                                      "Receiving");
    }
}

void DspPipeline::resetRx() {
    m_sampleAccum.clear();
    m_symbolAccum.clear();
    m_searchWindow.clear();
    m_expectedSymbols = 0;
    setRxState(RxState::Idle);
}

} // namespace HavenFSK
