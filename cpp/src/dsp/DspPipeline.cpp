#include "DspPipeline.h"
#include "ModemFactory.h"
#include "../radio/RadioSettings.h"
#include <QDateTime>
#include <QRegularExpression>
#include <QDebug>
#include <cmath>

namespace HavenFSK {

DspPipeline::DspPipeline(QObject* parent)
    : QObject(parent)
{
    m_modem = createModem(ModemMode::Mfsk16);
    m_lastPolledRxState = m_modem->rxState();
    m_lastPolledAfcHz   = m_modem->afcOffsetHz();
}

// ── Mode ─────────────────────────────────────────────────────────────────

void DspPipeline::setMode(ModemMode mode, const ModemConfig& cfg) {
    if (m_transmitting) {
        qWarning() << "DspPipeline: setMode ignored — transmit in progress";
        return;
    }
    m_modem = createModem(mode, cfg);
    m_lastPolledRxState = m_modem->rxState();
    m_lastPolledAfcHz   = m_modem->afcOffsetHz();
    qDebug() << "DspPipeline: mode ->" << QString::fromStdString(m_modem->modeName());
    emit modeChanged(mode);
    emit rxStateChanged(rxState());
}

RxState DspPipeline::rxState() const {
    return (m_modem->rxState() == ModemRxState::Idle)
           ? RxState::Idle : RxState::Collecting;
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
    m_modem->setRxSuspended(true);
    emit messageTransmitted(text);
    std::vector<float> audio = m_modem->modulateText(text.toStdString());
    qDebug() << "DspPipeline: TX" << text.length() << "chars ="
             << audio.size() << "samples ="
             << (audio.size() / static_cast<double>(SAMPLE_RATE)) << "seconds";
    emit txAudioReady(audio);
    return true;
}

void DspPipeline::onTxComplete() {
    m_transmitting = false;
    m_modem->setRxSuspended(false);
    qDebug() << "DspPipeline: TX complete";
}

// ── RX ────────────────────────────────────────────────────────────────────

void DspPipeline::onAudioChunk(const std::vector<float>& samples) {
    std::vector<float> corrected = samples;
    if (std::abs(m_rxGain - 1.0f) > 0.001f)
        for (float& s : corrected) s *= m_rxGain;

    auto events = m_modem->processAudioChunk(corrected);

    for (const auto& ev : events) {
        if (ev.preambleDetected)
            emit preambleDetected(ev.preambleScore);

        if (ev.symbolsExpected > 0)
            emit rxProgress(ev.symbolsReceived, ev.symbolsExpected);

        if (ev.hasMessage) {
            RxMessage msg;
            msg.text          = QString::fromStdString(ev.text);
            msg.crcOk         = ev.crcOk;
            msg.converged     = ev.converged;
            msg.nBlocks       = ev.nBlocks;
            msg.fecIterations = ev.fecIterations;
            msg.snr           = m_modem->lastSnrDb();

            HavenFSK::StationInfo info = HavenFSK::loadStationInfo();
            msg.senderCallsign = parseSenderCallsign(msg.text, info.callsign);

            updateRxCache(msg);
            expireRxCache();

            qDebug() << "DspPipeline: decoded message:" << msg.text
                     << "(CRC OK, FEC converged:" << msg.converged
                     << ", sender:" << msg.senderCallsign << ")";

            emit messageReceived(msg);
        }
    }

    pollModemStatus();
}

void DspPipeline::pollModemStatus() {
    bool dcdNow = m_modem->dcdActive();
    if (dcdNow != m_dcdActive) {
        m_dcdActive = dcdNow;
        emit dcdChanged(m_dcdActive);
    }

    ModemRxState stateNow = m_modem->rxState();
    if (stateNow != m_lastPolledRxState) {
        m_lastPolledRxState = stateNow;
        emit rxStateChanged(rxState());
    }

    float afcNow = m_modem->afcOffsetHz();
    if (std::abs(afcNow - m_lastPolledAfcHz) > 1e-6f) {
        m_lastPolledAfcHz = afcNow;
        emit afcOffsetChanged(afcNow);
    }
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

// ── Diagnostics ────────────────────────────────────────────────────────────

void DspPipeline::setToneMonitor(bool active) {
    m_modem->setToneMonitor(active);
}

std::vector<float> DspPipeline::generateToneSweepAudio() const {
    return m_modem->generateDiagnosticAudio();
}

void DspPipeline::runToneSweepTest() {
    m_modem->runDiagnosticSelfTest();
}

} // namespace HavenFSK
