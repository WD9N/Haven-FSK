#include "AudioEngine.h"
#include "../dsp/Constants.h"
#include <QMediaDevices>
#include <QAudioDevice>
#include <QtMath>
#include <QDebug>
#include <cstring>
#include <cmath>
#include <algorithm>

// ── Audio format ──────────────────────────────────────────────────────────
// HAVEN-FSK requires 48000 Hz, mono, signed 16-bit PCM.
// 48000 Hz is fixed by the published specification — do not make configurable.

QAudioFormat AudioEngine::havenFormat() {
    QAudioFormat fmt;
    fmt.setSampleRate(HavenFSK::SAMPLE_RATE);
    fmt.setChannelCount(HavenFSK::AUDIO_CHANNELS);
    fmt.setSampleFormat(QAudioFormat::Int16);
    return fmt;
}

// ── Constructor and destructor ────────────────────────────────────────────

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

QAudioDevice AudioEngine::findOutputDevice(const QString& name) {
    if (name.isEmpty())
        return QMediaDevices::defaultAudioOutput();
    for (const QAudioDevice& dev : QMediaDevices::audioOutputs())
        if (dev.description() == name)
            return dev;
    qWarning() << "AudioEngine: output device not found:" << name
               << "— using system default";
    return QMediaDevices::defaultAudioOutput();
}

// ── RX ────────────────────────────────────────────────────────────────────

bool AudioEngine::startRx(const QString& deviceName) {
    stopRx();

    QAudioDevice dev = findInputDevice(deviceName);
    QAudioFormat fmt = havenFormat();

    if (!dev.isFormatSupported(fmt)) {
        QString msg = QString("Input device '%1' does not support "
                              "48000 Hz mono. Please select a different "
                              "audio device.").arg(dev.description());
        emit audioError(msg);
        return false;
    }

    m_rxSource = std::make_unique<QAudioSource>(dev, fmt, this);
    m_rxSource->setBufferSize(
        HavenFSK::AUDIO_CHUNK_SAMPLES * 4 * static_cast<int>(sizeof(int16_t)));

    m_rxDevice = m_rxSource->start();
    if (!m_rxDevice) {
        emit audioError(QString("Failed to open input device '%1'")
                        .arg(dev.description()));
        m_rxSource.reset();
        return false;
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

// ── TX (QBuffer pull mode) ────────────────────────────────────────────────
//
// QAudioSink pull mode: we pass a QBuffer wrapping the complete PCM data.
// Qt6 reads from the buffer as the platform backend needs it — handles all
// backend-specific chunking internally (WMF, ALSA, PulseAudio).
//
// In pull mode, IdleState fires when the QBuffer is genuinely exhausted
// (all data consumed and played) — the correct completion signal.
// This differs from push mode where IdleState fires prematurely on WASAPI.

bool AudioEngine::startTx(const QString& deviceName,
                           const std::vector<float>& samples)
{
    stopTx();

    if (samples.empty()) {
        qWarning() << "AudioEngine::startTx: empty samples";
        return false;
    }

    // Find requested output device
    QAudioDevice dev;
    for (const auto& d : QMediaDevices::audioOutputs()) {
        if (d.description() == deviceName) {
            dev = d;
            break;
        }
    }
    if (dev.isNull()) {
        dev = QMediaDevices::defaultAudioOutput();
        qWarning() << "AudioEngine: TX device not found:" << deviceName
                   << "falling back to:" << dev.description();
    }

    qDebug() << "AudioEngine: TX using device:" << dev.description()
             << "samples:" << samples.size()
             << "duration:" << (samples.size() / 48000.0) << "sec";

    QAudioFormat fmt = havenFormat();
    if (!dev.isFormatSupported(fmt)) {
        qWarning() << "AudioEngine: TX format not supported on device:"
                   << dev.description();
        emit audioError(QString("Output device '%1' does not support "
                                "48000 Hz mono.").arg(dev.description()));
        return false;
    }

    // Convert float32 → int16 PCM into member buffer
    // m_txPcmBuffer is a member — stays alive for entire playback duration
    m_txPcmBuffer.resize(static_cast<int>(samples.size() * sizeof(int16_t)));
    int16_t* dst = reinterpret_cast<int16_t*>(m_txPcmBuffer.data());
    for (size_t i = 0; i < samples.size(); i++) {
        float s = std::max(-1.0f, std::min(1.0f, samples[i]));
        dst[i] = static_cast<int16_t>(s * 32767.0f);
    }

    // Wrap in QBuffer for pull mode
    // m_txQBuffer is a member — must outlive the sink
    m_txQBuffer = new QBuffer(&m_txPcmBuffer, this);
    if (!m_txQBuffer->open(QIODevice::ReadOnly)) {
        qWarning() << "AudioEngine: failed to open TX QBuffer";
        delete m_txQBuffer;
        m_txQBuffer = nullptr;
        m_txPcmBuffer.clear();
        return false;
    }

    m_txSink = std::make_unique<QAudioSink>(dev, fmt, this);
    connect(m_txSink.get(), &QAudioSink::stateChanged,
            this, &AudioEngine::onTxStateChanged);

    m_transmitting = true;

    // Pull mode: Qt reads from QBuffer as backend needs data
    m_txSink->start(m_txQBuffer);

    qDebug() << "AudioEngine: TX pull mode started —"
             << m_txPcmBuffer.size() << "bytes";

    return true;
}

void AudioEngine::onTxStateChanged(QAudio::State state) {
    qDebug() << "AudioEngine: TX state" << state
             << "error=" << (m_txSink ? static_cast<int>(m_txSink->error()) : -1)
             << "transmitting=" << m_transmitting.load();

    if (!m_transmitting) {
        qDebug() << "AudioEngine: stale state change — ignored";
        return;
    }

    if (state == QAudio::IdleState) {
        // Pull mode: IdleState = QBuffer exhausted = all audio played.
        // This IS the correct completion signal in pull mode.
        qDebug() << "AudioEngine: TX complete (QBuffer exhausted)";
        m_transmitting = false;

        if (m_txSink) {
            disconnect(m_txSink.get(), nullptr, this, nullptr);
            m_txSink->stop();
            m_txSink.reset();
        }
        if (m_txQBuffer) {
            m_txQBuffer->close();
            delete m_txQBuffer;
            m_txQBuffer = nullptr;
        }
        m_txPcmBuffer.clear();

        emit txComplete();
    }
    else if (state == QAudio::StoppedState) {
        auto err = m_txSink ? m_txSink->error() : QAudio::NoError;
        if (err != QAudio::NoError) {
            qWarning() << "AudioEngine: TX error" << err;
            m_transmitting = false;
            if (m_txSink) {
                disconnect(m_txSink.get(), nullptr, this, nullptr);
                m_txSink->stop();
                m_txSink.reset();
            }
            if (m_txQBuffer) {
                m_txQBuffer->close();
                delete m_txQBuffer;
                m_txQBuffer = nullptr;
            }
            m_txPcmBuffer.clear();
            emit audioError(QString("TX audio error: %1")
                            .arg(static_cast<int>(err)));
            emit txComplete();
        }
    }
}

void AudioEngine::stopTx() {
    m_transmitting = false;

    if (m_txSink) {
        disconnect(m_txSink.get(), nullptr, this, nullptr);
        m_txSink->stop();
        m_txSink.reset();
    }
    if (m_txQBuffer) {
        m_txQBuffer->close();
        delete m_txQBuffer;
        m_txQBuffer = nullptr;
    }
    m_txPcmBuffer.clear();
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
