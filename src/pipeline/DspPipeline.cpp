#include "DspPipeline.h"
#include "../dsp/ModemFactory.h"
#include "../dsp/FieldMarkers.h"
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
    // Carries the new modem's passband/name so callers don't need to read
    // them back via synchronous getters immediately after this call — see
    // modeReady's doc comment in DspPipeline.h.
    emit modeReady(mode, m_modem->passbandLowHz(), m_modem->passbandHighHz(),
                   QString::fromStdString(m_modem->modeName()));
}

RxState DspPipeline::rxState() const {
    return (m_modem->rxState() == ModemRxState::Idle)
           ? RxState::Idle : RxState::Collecting;
}

// ── TX ────────────────────────────────────────────────────────────────────

bool DspPipeline::transmit(const QString& text) {
    if (m_transmitting) {
        qWarning() << "DspPipeline: transmit called while already transmitting";
        emit transmitFailed("Already transmitting");
        return false;
    }
    if (text.trimmed().isEmpty()) {
        qWarning() << "DspPipeline: transmit called with empty text";
        emit transmitFailed("Message text is empty");
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
    float gain = m_rxGain.load(std::memory_order_relaxed);
    if (std::abs(gain - 1.0f) > 0.001f)
        for (float& s : corrected) s *= gain;

    auto events = m_modem->processAudioChunk(corrected);

    for (const auto& ev : events) {
        if (ev.preambleDetected)
            emit preambleDetected(ev.preambleScore);

        if (!ev.syncAdjustReason.empty())
            emit syncThresholdChanged(ev.syncThresholdNow,
                QString::fromStdString(ev.syncAdjustReason));

        if (ev.symbolsExpected > 0)
            emit rxProgress(ev.symbolsReceived, ev.symbolsExpected);

        if (ev.hasMessage && !ev.isFramedMessage) {
            // Continuous character stream (PSK31) — not a discrete
            // message, skip the RxMessage/CRC-cache machinery entirely.
            emit textCharacterReceived(QString::fromStdString(ev.text));
        } else if (ev.hasMessage) {
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

std::optional<RxMeasurement> DspPipeline::getRxMeasurement(
    const QString& callsign) const
{
    std::lock_guard<std::mutex> lock(m_rxCacheMutex);
    auto it = m_rxCache.find(callsign.toUpper());
    if (it == m_rxCache.end()) return std::nullopt;
    if (it->timestamp.secsTo(QDateTime::currentDateTime())
        > RS_CACHE_MINUTES * 60) return std::nullopt;
    return *it;
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
    // Inline field markers first (ADR-133): a 'd' field is the sender's
    // own declaration of identity — no heuristics needed. Shape-check it
    // anyway so garbage can't ride in as a "callsign".
    {
        MarkedMessage marked = parseMarkedText(text.toStdString());
        QString declared = QString::fromStdString(
            fieldValue(marked, FieldId::Sender)).toUpper();
        static QRegularExpression declRe(
            "^[A-Z0-9]{1,3}[0-9][A-Z0-9]{0,3}[A-Z]$");
        if (!declared.isEmpty() && declRe.match(declared).hasMatch()
            && declared != myCallsign.toUpper())
            return declared;
    }

    // Strip structured tag/value spans before scanning — tag values must
    // never be candidates for the sender callsign. Grid squares
    // especially: a 6-char Maidenhead locator (EN52XA) is exactly
    // callsign-shaped, and "GRID: EN52XA" (space after the colon) or a
    // bare grid in the text was being picked up as <theirCall>. Tag set
    // kept in sync with RxDisplay::renderMessage/LogPanel by convention.
    // Fallback heuristic scans the marker-stripped text — leftover
    // marker bytes glued to words would defeat the callsign regex.
    QString scrubbed = QString::fromStdString(
        stripMarkers(text.toStdString())).toUpper();
    static QRegularExpression tagValueRe(
        "RS:\\s*\\S{1,2}"
        "|(?:NAME|QTH|GRID|POTA|SOTA|FD):\\s*[^\\s].*?"
        "(?=\\s+(?:NAME:|QTH:|GRID:|RS:|POTA:|SOTA:|FD:)|$)");
    scrubbed.remove(tagValueRe);

    QStringList words = scrubbed.split(' ', Qt::SkipEmptyParts);
    QString myCall = myCallsign.toUpper();
    static QRegularExpression callRe(
        "^[A-Z0-9]{1,3}[0-9][A-Z0-9]{0,3}[A-Z]$");
    // Bare (untagged) grid squares still look like callsigns — reject
    // pure Maidenhead shapes. Costs the rare special-event call that
    // happens to fit (e.g. GB19HQ); mis-logging every grid costs more.
    static QRegularExpression gridRe("^[A-R]{2}[0-9]{2}[A-X]{2}$");
    auto isCall = [&](const QString& w) {
        return callRe.match(w).hasMatch() && w != myCall &&
               !gridRe.match(w).hasMatch();
    };

    for (int i = 1; i < words.size() - 1; i++) {
        if (words[i] == "DE") {
            if (isCall(words[i - 1])) return words[i - 1];
            if (isCall(words[i + 1])) return words[i + 1];
        }
    }
    for (const QString& word : words)
        if (isCall(word)) return word;
    return QString();
}

void DspPipeline::updateRxCache(const RxMessage& msg) {
    if (msg.senderCallsign.isEmpty()) return;
    RxMeasurement m;
    m.snrDb         = msg.snr;
    m.fecIterations = msg.fecIterations;
    m.converged     = msg.converged;
    m.timestamp     = QDateTime::currentDateTime();
    std::lock_guard<std::mutex> lock(m_rxCacheMutex);
    m_rxCache[msg.senderCallsign.toUpper()] = m;
}

void DspPipeline::expireRxCache() {
    auto now = QDateTime::currentDateTime();
    std::lock_guard<std::mutex> lock(m_rxCacheMutex);
    for (auto it = m_rxCache.begin(); it != m_rxCache.end(); ) {
        if (it->timestamp.secsTo(now) > RS_CACHE_MINUTES * 60)
            it = m_rxCache.erase(it);
        else
            ++it;
    }
}

// ── Tune ───────────────────────────────────────────────────────────────────

void DspPipeline::requestTuneAudio() {
    emit tuneAudioReady(m_modem->generateTuneAudio());
}

} // namespace HavenFSK
