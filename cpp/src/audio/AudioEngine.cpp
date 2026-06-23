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
// 48000 Hz is fixed by the published specification — do not make this
// configurable. Changing the sample rate would require publishing a new
// specification and a new FCC emission designator.
//
// We use Int16 PCM rather than Float32 because Qt6 Multimedia's
// platform backends (WASAPI on Windows, ALSA on Linux, CoreAudio on macOS)
// have the widest device compatibility with Int16. We convert to/from
// float32 at the AudioEngine boundary so the DSP layer always sees floats.

QAudioFormat AudioEngine::havenFormat() {
    QAudioFormat fmt;
    fmt.setSampleRate(HavenFSK::SAMPLE_RATE);        // 48000 Hz
    fmt.setChannelCount(HavenFSK::AUDIO_CHANNELS);   // 1 (mono)
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

    // Set buffer size to hold ~4 chunks for smooth streaming
    // 4 * AUDIO_CHUNK_SAMPLES * 2 bytes (int16) * 1 channel
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

// ── TX ────────────────────────────────────────────────────────────────────

bool AudioEngine::startTx(const QString& deviceName,
                           const std::vector<float>& samples)
{
    stopTx();

    if (samples.empty()) {
        emit audioError("TX called with empty sample buffer");
        return false;
    }

    QAudioDevice dev = findOutputDevice(deviceName);
    QAudioFormat fmt = havenFormat();

    if (!dev.isFormatSupported(fmt)) {
        QString msg = QString("Output device '%1' does not support "
                              "48000 Hz mono. Please select a different "
                              "audio device.").arg(dev.description());
        emit audioError(msg);
        return false;
    }

    m_txBuffer = floatToPcm(samples);
    m_txOffset = 0;

    m_txSink = std::make_unique<QAudioSink>(dev, fmt, this);

    connect(m_txSink.get(), &QAudioSink::stateChanged,
            this, &AudioEngine::onTxStateChanged);

    m_txDevice = m_txSink->start();
    if (!m_txDevice) {
        emit audioError(QString("Failed to open output device '%1'")
                        .arg(dev.description()));
        m_txSink.reset();
        m_txBuffer.clear();
        return false;
    }

    qDebug() << "AudioEngine::startTx using device:" << dev.description()
             << "(requested:" << deviceName << ")"
             << samples.size() << "samples ="
             << (samples.size() / 48000.0) << "sec";

    // Write initial chunk — WASAPI/VAC buffers are typically limited to
    // ~500ms. bytesFree() tells us how much fits right now; the write timer
    // (onWriteMore) keeps feeding the rest as the driver drains the buffer.
    m_txOffset = 0;
    qint64 canWrite = m_txSink->bytesFree();
    if (canWrite <= 0) canWrite = m_txBuffer.size(); // fallback: try all
    qint64 toWrite  = std::min(canWrite, static_cast<qint64>(m_txBuffer.size()));
    qint64 written  = m_txDevice->write(m_txBuffer.constData(), toWrite);
    if (written > 0) m_txOffset = static_cast<int>(written);
    qDebug() << "AudioEngine: initial write" << written << "/"
             << m_txBuffer.size() << "bytes";

    // Start progressive write timer if data remains
    if (m_txOffset < m_txBuffer.size()) {
        if (m_writeTimer) { m_writeTimer->stop(); m_writeTimer->deleteLater(); }
        m_writeTimer = new QTimer(this);
        m_writeTimer->setInterval(20); // 20ms — well inside any driver period
        connect(m_writeTimer, &QTimer::timeout,
                this, &AudioEngine::onWriteMore);
        m_writeTimer->start();
    }

    m_transmitting = true;

    // WASAPI on Windows signals IdleState as soon as data reaches the driver
    // buffer — not when the hardware finishes playing. Use a timer for actual
    // TX duration so txComplete fires at the right time.
    // TX tail (PTT release delay) is handled in MainWindow::onTxComplete().
    int durationMs = static_cast<int>(
        static_cast<double>(samples.size()) / HavenFSK::SAMPLE_RATE * 1000.0);
    // 50ms pad for WASAPI buffer acceptance latency only
    durationMs += 50;

    if (m_txTimer) {
        m_txTimer->stop();
        m_txTimer->deleteLater();
    }
    m_txTimer = new QTimer(this);
    m_txTimer->setSingleShot(true);
    connect(m_txTimer, &QTimer::timeout, this, [this]() {
        m_txTimer = nullptr;
        if (!m_transmitting) return;
        if (m_writeTimer) {
            m_writeTimer->stop();
            m_writeTimer->deleteLater();
            m_writeTimer = nullptr;
        }
        m_transmitting = false;
        if (m_txSink) m_txSink->stop();
        m_txSink.reset();
        m_txDevice = nullptr;
        m_txBuffer.clear();
        m_txOffset = 0;
        qDebug() << "AudioEngine: TX complete (timer)";
        emit txComplete();
    });
    m_txTimer->start(durationMs);

    return true;
}

void AudioEngine::onTxStateChanged(QAudio::State state) {
    qDebug() << "AudioEngine::onTxStateChanged state=" << state
             << "error=" << (m_txSink ? static_cast<int>(m_txSink->error()) : -1);

    // IdleState fires immediately on Windows/WASAPI as soon as data reaches
    // the driver buffer — NOT when hardware finishes playback. Completion is
    // handled by m_txTimer instead. Log it for diagnostics only.
    if (state == QAudio::IdleState) {
        qDebug() << "AudioEngine: IdleState (data in driver buffer,"
                 << "timer handles actual completion)";
        return;
    }

    // StoppedState with error = real failure; cancel timer and signal error
    if (state == QAudio::StoppedState) {
        if (m_txSink && m_txSink->error() != QAudio::NoError) {
            qWarning() << "AudioEngine: TX error:" << m_txSink->error();
            if (m_txTimer) { m_txTimer->stop(); m_txTimer->deleteLater(); m_txTimer = nullptr; }
            emit audioError(QString("TX audio error: %1")
                            .arg(static_cast<int>(m_txSink->error())));
            if (m_transmitting) {
                m_transmitting = false;
                emit txComplete();
            }
        }
    }
}

void AudioEngine::stopTx() {
    if (m_writeTimer) {
        m_writeTimer->stop();
        m_writeTimer->deleteLater();
        m_writeTimer = nullptr;
    }
    if (m_txTimer) {
        m_txTimer->stop();
        m_txTimer->deleteLater();
        m_txTimer = nullptr;
    }
    if (m_txSink) {
        m_txSink->stop();
        m_txSink.reset();
        m_txDevice = nullptr;
    }
    m_txBuffer.clear();
    m_txOffset = 0;
    m_transmitting = false;
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
// Int16 range: -32768 to +32767 → float range: -1.0 to +1.0
// Division by 32768.0f (not 32767) is standard practice — produces
// symmetric range and avoids special-casing INT16_MIN.

std::vector<float> AudioEngine::pcmToFloat(const QByteArray& pcm) {
    int nSamples = pcm.size() / static_cast<int>(sizeof(int16_t));
    std::vector<float> out(nSamples);
    const int16_t* src = reinterpret_cast<const int16_t*>(pcm.constData());
    for (int i = 0; i < nSamples; i++)
        out[i] = static_cast<float>(src[i]) / 32768.0f;
    return out;
}

QByteArray AudioEngine::floatToPcm(const std::vector<float>& samples) {
    QByteArray pcm(
        static_cast<int>(samples.size() * sizeof(int16_t)), Qt::Uninitialized);
    int16_t* dst = reinterpret_cast<int16_t*>(pcm.data());
    for (int i = 0; i < (int)samples.size(); i++) {
        float clamped = std::max(-1.0f, std::min(1.0f, samples[i]));
        dst[i] = static_cast<int16_t>(clamped * 32767.0f);
    }
    return pcm;
}

void AudioEngine::onWriteMore() {
    if (!m_txDevice || !m_transmitting || m_txOffset >= m_txBuffer.size()) {
        if (m_writeTimer) m_writeTimer->stop();
        return;
    }

    qint64 free      = m_txSink ? m_txSink->bytesFree() : 0;
    if (free <= 0) return;

    qint64 remaining = static_cast<qint64>(m_txBuffer.size()) - m_txOffset;
    qint64 toWrite   = std::min(free, remaining);

    qint64 written = m_txDevice->write(
        m_txBuffer.constData() + m_txOffset, toWrite);
    if (written > 0) {
        m_txOffset += static_cast<int>(written);
        qDebug() << "AudioEngine: write chunk" << written
                 << "bytes," << (m_txBuffer.size() - m_txOffset)
                 << "remaining";
    }

    if (m_txOffset >= m_txBuffer.size()) {
        if (m_writeTimer) m_writeTimer->stop();
        qDebug() << "AudioEngine: all TX data written to driver buffer";
    }
}

float AudioEngine::computeRms(const std::vector<float>& samples) {
    if (samples.empty()) return 0.0f;
    float sum = 0.0f;
    for (float s : samples) sum += s * s;
    return std::sqrt(sum / static_cast<float>(samples.size()));
}
