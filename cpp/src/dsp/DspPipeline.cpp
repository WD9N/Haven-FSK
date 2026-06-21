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
    // Pre-size the sample accumulator
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

    // Frame::assemble() handles: preamble + header + CRC + FEC encode + modulate
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
    // ── DCD update ────────────────────────────────────────────────────────
    bool dcdNow = m_dcd.update(samples);
    if (dcdNow != m_dcdActive) {
        m_dcdActive = dcdNow;
        emit dcdChanged(m_dcdActive);

        if (!m_dcdActive && m_rxState != RxState::Idle) {
            // Signal dropped — if we were receiving, process what we have
            if (m_rxState == RxState::Receiving && !m_symbolAccum.empty())
                processFrame();
            resetRx();
        }

        if (m_dcdActive && m_rxState == RxState::Idle)
            setRxState(RxState::Searching);
    }

    // Don't process audio if idle or transmitting
    if (m_rxState == RxState::Idle || m_transmitting)
        return;

    // ── Accumulate samples into symbol-sized blocks ────────────────────────
    // AUDIO_CHUNK_SAMPLES (2048) is not a multiple of SAMPLES_PER_SYMBOL
    // (1536), so we buffer across chunk boundaries.
    m_sampleAccum.insert(m_sampleAccum.end(), samples.begin(), samples.end());

    while ((int)m_sampleAccum.size() >= SAMPLES_PER_SYMBOL) {
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

        // Sliding window: keep 2× preamble length
        if ((int)m_searchWindow.size() > PREAMBLE_LENGTH * 2)
            m_searchWindow.erase(m_searchWindow.begin());

        if ((int)m_searchWindow.size() >= PREAMBLE_LENGTH) {
            float score = 0.0f;
            if (m_preamble.detect(m_searchWindow, score)) {
                qDebug() << "DspPipeline: preamble detected, score =" << score;
                emit preambleDetected(score);

                m_symbolAccum.clear();
                m_expectedSymbols = MAX_FRAME_SYMBOLS;
                setRxState(RxState::Receiving);
            }
        }
        break;
    }

    case RxState::Receiving: {
        m_symbolAccum.push_back(softEnergies);

        emit rxProgress(static_cast<int>(m_symbolAccum.size()),
                        m_expectedSymbols);

        // After header + CRC arrive, peek at nBlocks to refine expected count
        if ((int)m_symbolAccum.size() == 8) {
            auto argmax = [](const std::vector<float>& v) {
                return (int)std::distance(
                    v.begin(), std::max_element(v.begin(), v.end()));
            };
            int s2 = argmax(m_symbolAccum[2]);
            int s3 = argmax(m_symbolAccum[3]);
            int nBlocks = (s2 << 4) | s3;
            if (nBlocks > 0 && nBlocks <= 32) {
                m_expectedSymbols = 4 + 4 + nBlocks * 48;
                qDebug() << "DspPipeline: header peek — nBlocks ="
                         << nBlocks
                         << ", expecting" << m_expectedSymbols
                         << "total symbols";
            } else {
                m_expectedSymbols = MAX_FRAME_SYMBOLS;
            }
        }

        if ((int)m_symbolAccum.size() >= m_expectedSymbols &&
            m_expectedSymbols > 8) {
            processFrame();
            resetRx();
            if (m_dcdActive)
                setRxState(RxState::Searching);
        }

        // Safety: give up if we accumulate too many symbols
        if ((int)m_symbolAccum.size() >= MAX_FRAME_SYMBOLS) {
            qWarning() << "DspPipeline: frame too long, giving up";
            processFrame();
            resetRx();
            if (m_dcdActive)
                setRxState(RxState::Searching);
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
    msg.fecIterations = 0;  // set from FEC result if available
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
    // R: Readability from FEC convergence and iteration count
    int r = 1;
    if (m.converged) {
        if      (m.fecIterations < 50)  r = 5;
        else if (m.fecIterations < 150) r = 4;
        else                            r = 3;
    }

    // S: Signal strength from SNR dB — standard 6 dB per S-unit scale
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

    // Pattern: THEIRCALL DE MYCALL — sender is the word before DE
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

    // Fallback: first callsign-like word that isn't our call
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
