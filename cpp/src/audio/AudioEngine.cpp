#include "AudioEngine.h"
#include "../dsp/Constants.h"
#include <QMediaDevices>
#include <QAudioDevice>
#include <QDebug>
#include <cmath>
#include <algorithm>

// ── Audio format ──────────────────────────────────────────────────────────

QAudioFormat AudioEngine::havenFormat() {
    QAudioFormat fmt;
    fmt.setSampleRate(HavenFSK::SAMPLE_RATE);
    fmt.setChannelCount(HavenFSK::AUDIO_CHANNELS);
    fmt.setSampleFormat(QAudioFormat::Int16);
    return fmt;
}

// ── Constructor / destructor ──────────────────────────────────────────────

AudioEngine::AudioEngine(QObject* parent)
    : QObject(parent)
{}

AudioEngine::~AudioEngine() {
    stop();
}

// ── Device enumeration ────────────────────────────────────────────────────

QStringList AudioEngine::availableInputDevices() {
    QStringList names;
    for (const QAudioDevice& dev : QMediaDevices::audioInputs())
        names.append(dev.description());
    return names;
}

QStringList AudioEngine::availableOutputDevices() {
    QStringList names;
    for (const QAudioDevice& dev : QMediaDevices::audioOutputs())
        names.append(dev.description());
    return names;
}

QAudioDevice AudioEngine::findInputDevice(const QString& name) {
    if (name.isEmpty())
        return QMediaDevices::defaultAudioInput();
    for (const QAudioDevice& dev : QMediaDevices::audioInputs())
        if (dev.description() == name)
            return dev;
    qWarning() << "AudioEngine: input device not found:" << name
               << "— using system default";
    return QMediaDevices::defaultAudioInput();
}

// ── RX ────────────────────────────────────────────────────────────────────

bool AudioEngine::startRx(const QString& deviceName) {
    stopRx();

    QAudioDevice dev = findInputDevice(deviceName);
    QAudioFormat fmt = havenFormat();

    if (!dev.isFormatSupported(fmt)) {
        emit audioError(
            QString("Input device '%1' does not support 48000 Hz mono.")
            .arg(dev.description()));
        return false;
    }

    m_rxSource = std::make_unique<QAudioSource>(dev, fmt, this);
    m_rxSource->setBufferSize(
        HavenFSK::AUDIO_CHUNK_SAMPLES * 4 * static_cast<int>(sizeof(int16_t)));

    m_rxDevice = m_rxSource->start();
    if (!m_rxDevice) {
        emit audioError(
            QString("Failed to open input device '%1'").arg(dev.description()));
        m_rxSource.reset();
        return false;
    }

    // Verify the format Qt actually negotiated.  Some VAC/driver combinations
    // advertise support for the requested format but deliver different data:
    //
    //  - Wrong sample rate (e.g. 44100 Hz instead of 48000 Hz): shifts every
    //    HAVEN tone by the ratio, so tone 15 misses every hypothesis window.
    //
    //  - Wrong channel count (e.g. stereo instead of mono): pcmToFloat()
    //    interprets interleaved L,R int16 pairs as sequential mono samples,
    //    halving the apparent sample rate and destroying all tone bin mapping.
    //
    // Always log the actual format so mismatches are immediately visible.
    QAudioFormat actual = m_rxSource->format();
    qDebug() << "AudioEngine: RX actual format —"
             << actual.sampleRate() << "Hz,"
             << actual.channelCount() << "ch,"
             << actual.sampleFormat();

    bool formatOk = true;
    if (actual.sampleRate() != HavenFSK::SAMPLE_RATE) {
        qWarning() << "AudioEngine: sample rate mismatch — got"
                   << actual.sampleRate() << "Hz, need"
                   << HavenFSK::SAMPLE_RATE << "Hz."
                   << "Set the Windows audio device and VAC to"
                   << HavenFSK::SAMPLE_RATE << "Hz.";
        formatOk = false;
    }
    if (actual.channelCount() != HavenFSK::AUDIO_CHANNELS) {
        qWarning() << "AudioEngine: channel count mismatch — got"
                   << actual.channelCount() << "ch, need"
                   << HavenFSK::AUDIO_CHANNELS << "ch (mono)."
                   << "Set the Windows audio device and VAC to Mono,"
                   << "or configure the VAC to 1 channel.";
        formatOk = false;
    }
    if (!formatOk) {
        emit audioError(
            QString("RX audio format mismatch (got %1 Hz, %2 ch). "
                    "Set VAC to %3 Hz mono. See debug log for details.")
            .arg(actual.sampleRate())
            .arg(actual.channelCount())
            .arg(HavenFSK::SAMPLE_RATE));
    }

    connect(m_rxDevice, &QIODevice::readyRead,
            this, &AudioEngine::onRxDataAvailable);

    m_receiving = true;
    qDebug() << "AudioEngine: RX started on" << dev.description();
    return true;
}

void AudioEngine::onRxDataAvailable() {
    if (!m_rxDevice || !m_receiving) return;

    QByteArray raw = m_rxDevice->readAll();
    m_rxBuffer.append(raw);

    const int chunkBytes =
        HavenFSK::AUDIO_CHUNK_SAMPLES * static_cast<int>(sizeof(int16_t));

    while (m_rxBuffer.size() >= chunkBytes) {
        QByteArray chunk = m_rxBuffer.left(chunkBytes);
        m_rxBuffer.remove(0, chunkBytes);
        std::vector<float> samples = pcmToFloat(chunk);
        emit rxLevelChanged(computeRms(samples));
        emit rxDataReady(samples);
    }
}

void AudioEngine::stopRx() {
    if (m_rxSource) {
        m_rxSource->stop();
        m_rxSource.reset();
        m_rxDevice = nullptr;
    }
    m_rxBuffer.clear();
    m_receiving = false;
}

bool AudioEngine::isReceiving() const {
    return m_receiving.load();
}

// ── TX (QAudioSink, pull mode, computed-duration completion) ──────────────
//
// PCM samples are converted to raw int16 bytes (no container) and played
// via QAudioSink::start(QIODevice*) in pull mode — see ADR-107. Completion
// is a computed-duration QTimer rather than QAudioSink's own state signal,
// which is unreliable for one-shot playback (fires as soon as data is
// handed to the driver, not when playback genuinely finishes).

QByteArray AudioEngine::floatToPcm16(const std::vector<float>& samples) {
    QByteArray pcm;
    pcm.resize(static_cast<int>(samples.size() * sizeof(int16_t)));
    int16_t* dst = reinterpret_cast<int16_t*>(pcm.data());
    for (size_t i = 0; i < samples.size(); i++) {
        float s = std::max(-1.0f, std::min(1.0f, samples[i]));
        dst[i] = static_cast<int16_t>(s * 32767.0f);
    }
    return pcm;
}

bool AudioEngine::startTx(const QString& deviceName,
                           const std::vector<float>& samples,
                           float initialGain)
{
    stopTx();

    if (samples.empty()) {
        qWarning() << "AudioEngine::startTx: empty samples";
        return false;
    }

    double durationSec = samples.size() /
        static_cast<double>(HavenFSK::SAMPLE_RATE);
    qDebug() << "AudioEngine: startTx"
             << "samples=" << samples.size()
             << "duration=" << durationSec << "s"
             << "gain=" << initialGain;

    // Raw PCM at full scale — gain applied by GainedAudioDevice. No WAV
    // header: QAudioSink gets its format from the QAudioFormat passed to
    // its constructor below, not by parsing a container (see ADR-107).
    m_txPcmData = floatToPcm16(samples);

    m_txGainDevice = new GainedAudioDevice(
        m_txPcmData, initialGain, this, /*headerBytes=*/0);
    if (!m_txGainDevice->open(QIODevice::ReadOnly)) {
        qWarning() << "AudioEngine: failed to open GainedAudioDevice";
        delete m_txGainDevice;
        m_txGainDevice = nullptr;
        m_txPcmData.clear();
        return false;
    }

    // Find output device
    QAudioDevice dev;
    for (const auto& d : QMediaDevices::audioOutputs())
        if (d.description() == deviceName) { dev = d; break; }
    if (dev.isNull()) {
        dev = QMediaDevices::defaultAudioOutput();
        qWarning() << "AudioEngine: TX device not found:" << deviceName
                   << "using default:" << dev.description();
    }

    QAudioFormat fmt = havenFormat();
    if (!dev.isFormatSupported(fmt)) {
        QAudioFormat nearest = dev.preferredFormat();
        qWarning() << "AudioEngine: TX device does not support"
                   << fmt.sampleRate() << "Hz" << fmt.channelCount()
                   << "ch Int16 — device prefers" << nearest.sampleRate()
                   << "Hz" << nearest.channelCount() << "ch."
                   << "OS may resample and shift all Haven tones.";
        emit audioError(
            QString("TX audio format mismatch (device wants %1 Hz). "
                    "See debug log for details.").arg(nearest.sampleRate()));
    }

    m_txAudioSink = new QAudioSink(dev, fmt, this);
    m_transmitting = true;
    m_txAudioSink->start(m_txGainDevice);   // pull mode

    // Completion detection: QAudioSink::stateChanged()/IdleState is
    // unreliable for one-shot playback (fires as soon as data is handed to
    // the driver, not when playback genuinely finishes — see ADR-107 for
    // the full history; this is the same behavior that previously ruled
    // QAudioSink out for TX). Since this PCM is generated by HAVEN itself
    // with an exactly-known sample count and rate, a computed-duration
    // timer is accurate here (unlike arbitrary/unknown media duration
    // estimation). +150ms safety margin: declaring TX complete too EARLY
    // (before the last symbol has actually drained through hardware) is
    // the failure mode to avoid, not a little late — TX_TAIL_MS already
    // adds further PTT-hold margin on top of this at the caller.
    if (!m_txCompletionTimer) {
        m_txCompletionTimer = new QTimer(this);
        m_txCompletionTimer->setSingleShot(true);
        connect(m_txCompletionTimer, &QTimer::timeout,
                this, &AudioEngine::onTxCompletionTimer);
    }
    int timerMs = static_cast<int>(durationSec * 1000.0) + 150;
    m_txCompletionTimer->start(timerMs);

    qDebug() << "AudioEngine: TX started (QAudioSink) with real-time gain"
             << initialGain << "(" << m_txPcmData.size() << "bytes),"
             << "completion timer=" << timerMs << "ms";
    return true;
}

void AudioEngine::onTxCompletionTimer() {
    if (!m_transmitting) return;
    qDebug() << "AudioEngine: TX complete (computed duration elapsed)";
    m_transmitting = false;

    if (m_txAudioSink) {
        m_txAudioSink->stop();
        delete m_txAudioSink;
        m_txAudioSink = nullptr;
    }
    if (m_txGainDevice) {
        m_txGainDevice->close();
        delete m_txGainDevice;
        m_txGainDevice = nullptr;
    }
    m_txPcmData.clear();

    emit txComplete();
}

void AudioEngine::stopTx() {
    m_transmitting = false;
    if (m_txCompletionTimer)
        m_txCompletionTimer->stop();
    if (m_txAudioSink) {
        m_txAudioSink->stop();
        delete m_txAudioSink;
        m_txAudioSink = nullptr;
    }
    if (m_txGainDevice) {
        m_txGainDevice->close();
        delete m_txGainDevice;
        m_txGainDevice = nullptr;
    }
    m_txPcmData.clear();
}

bool AudioEngine::isTransmitting() const {
    return m_transmitting.load();
}

// ── Control ───────────────────────────────────────────────────────────────

void AudioEngine::stop() {
    stopRx();
    stopTx();
}

// ── PCM ↔ float conversion ────────────────────────────────────────────────

std::vector<float> AudioEngine::pcmToFloat(const QByteArray& pcm) {
    int nSamples = pcm.size() / static_cast<int>(sizeof(int16_t));
    std::vector<float> out(nSamples);
    const int16_t* src = reinterpret_cast<const int16_t*>(pcm.constData());
    for (int i = 0; i < nSamples; i++)
        out[i] = static_cast<float>(src[i]) / 32768.0f;
    return out;
}

float AudioEngine::computeRms(const std::vector<float>& samples) {
    if (samples.empty()) return 0.0f;
    float sum = 0.0f;
    for (float s : samples) sum += s * s;
    return std::sqrt(sum / static_cast<float>(samples.size()));
}
