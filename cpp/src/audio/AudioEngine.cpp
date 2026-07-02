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

// ── TX (QMediaPlayer with in-memory WAV) ─────────────────────────────────
//
// PCM samples are converted to a WAV file in memory, wrapped in a QBuffer,
// and played via QMediaPlayer::setSourceDevice(). QMediaPlayer handles all
// platform audio complexity. StoppedState fires when playback genuinely
// finishes — the correct completion signal on all platforms.

QByteArray AudioEngine::buildWav(const std::vector<float>& samples) const {
    // Convert float32 → int16 PCM
    QByteArray pcm;
    pcm.resize(static_cast<int>(samples.size() * sizeof(int16_t)));
    int16_t* dst = reinterpret_cast<int16_t*>(pcm.data());
    for (size_t i = 0; i < samples.size(); i++) {
        float s = std::max(-1.0f, std::min(1.0f, samples[i]));
        dst[i] = static_cast<int16_t>(s * 32767.0f);
    }

    uint32_t sampleRate    = HavenFSK::SAMPLE_RATE;
    uint16_t channels      = 1;
    uint16_t bitsPerSample = 16;
    uint32_t byteRate      = sampleRate * channels * bitsPerSample / 8;
    uint16_t blockAlign    = channels * bitsPerSample / 8;
    uint32_t dataSize      = static_cast<uint32_t>(pcm.size());
    uint32_t fileSize      = 36 + dataSize;

    QByteArray wav;
    wav.reserve(44 + static_cast<int>(pcm.size()));

    auto appendU32 = [&](uint32_t v) {
        wav.append(static_cast<char>(v         & 0xFF));
        wav.append(static_cast<char>((v >>  8) & 0xFF));
        wav.append(static_cast<char>((v >> 16) & 0xFF));
        wav.append(static_cast<char>((v >> 24) & 0xFF));
    };
    auto appendU16 = [&](uint16_t v) {
        wav.append(static_cast<char>(v        & 0xFF));
        wav.append(static_cast<char>((v >> 8) & 0xFF));
    };

    wav.append("RIFF", 4);   appendU32(fileSize);
    wav.append("WAVE", 4);
    wav.append("fmt ", 4);   appendU32(16);
    appendU16(1);             // PCM format
    appendU16(channels);
    appendU32(sampleRate);
    appendU32(byteRate);
    appendU16(blockAlign);
    appendU16(bitsPerSample);
    wav.append("data", 4);   appendU32(dataSize);
    wav.append(pcm);

    return wav;
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

    qDebug() << "AudioEngine: startTx"
             << "samples=" << samples.size()
             << "duration=" << (samples.size() /
                static_cast<double>(HavenFSK::SAMPLE_RATE)) << "s"
             << "gain=" << initialGain;

    // Build WAV at full scale — gain applied by GainedAudioDevice
    m_txWavData = buildWav(samples);

    m_txGainDevice = new GainedAudioDevice(m_txWavData, initialGain, this);
    if (!m_txGainDevice->open(QIODevice::ReadOnly)) {
        qWarning() << "AudioEngine: failed to open GainedAudioDevice";
        delete m_txGainDevice;
        m_txGainDevice = nullptr;
        m_txWavData.clear();
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

    m_txPlayer   = new QMediaPlayer(this);
    m_txAudioOut = new QAudioOutput(dev, this);
    m_txAudioOut->setVolume(1.0f);  // full — gain lives in GainedAudioDevice
    m_txPlayer->setAudioOutput(m_txAudioOut);

    connect(m_txPlayer, &QMediaPlayer::playbackStateChanged,
            this, &AudioEngine::onTxPlaybackStateChanged);

    // Log when QMediaPlayer reports the actual audio format it negotiated
    // with the output device.  A sample rate other than 48000 Hz means the
    // OS audio engine resampled the WAV — all Haven tones would be shifted
    // by the ratio and the remote receiver would fail to decode.
    connect(m_txAudioOut, &QAudioOutput::deviceChanged,
            this, [this]() {
        if (m_txAudioOut) {
            auto fmt = m_txAudioOut->device().preferredFormat();
            qDebug() << "AudioEngine: TX output device preferred format —"
                     << fmt.sampleRate() << "Hz,"
                     << fmt.channelCount() << "ch,"
                     << fmt.sampleFormat();
            if (fmt.sampleRate() != HavenFSK::SAMPLE_RATE)
                qWarning() << "AudioEngine: TX sample rate mismatch — device prefers"
                           << fmt.sampleRate() << "Hz, WAV is"
                           << HavenFSK::SAMPLE_RATE
                           << "Hz. OS will resample and shift all Haven tones.";
        }
    });

    m_txPlayer->setSourceDevice(m_txGainDevice, QUrl("audio/wav"));

    m_transmitting = true;
    m_txPlayer->play();

    // Log the TX output device's preferred format immediately (deviceChanged
    // may not fire if the device was already set).
    {
        auto fmt = dev.preferredFormat();
        qDebug() << "AudioEngine: TX output device preferred format —"
                 << fmt.sampleRate() << "Hz,"
                 << fmt.channelCount() << "ch,"
                 << fmt.sampleFormat();
        if (fmt.sampleRate() != HavenFSK::SAMPLE_RATE)
            qWarning() << "AudioEngine: TX sample rate mismatch — device prefers"
                       << fmt.sampleRate() << "Hz, WAV is"
                       << HavenFSK::SAMPLE_RATE
                       << "Hz. OS will resample and shift all Haven tones.";
    }

    qDebug() << "AudioEngine: TX started with real-time gain"
             << initialGain << "(" << m_txWavData.size() << "bytes)";
    return true;
}

void AudioEngine::onTxPlaybackStateChanged(
    QMediaPlayer::PlaybackState state)
{
    qDebug() << "AudioEngine: TX playback state=" << state
             << "transmitting=" << m_transmitting.load();

    if (state == QMediaPlayer::StoppedState && m_transmitting) {
        qDebug() << "AudioEngine: TX complete (QMediaPlayer stopped)";
        m_transmitting = false;

        if (m_txPlayer) {
            delete m_txPlayer;   // already stopped — StoppedState just fired
            m_txPlayer = nullptr;
        }
        if (m_txAudioOut) {
            delete m_txAudioOut;
            m_txAudioOut = nullptr;
        }
        if (m_txGainDevice) {
            m_txGainDevice->close();
            delete m_txGainDevice;
            m_txGainDevice = nullptr;
        }
        m_txWavData.clear();

        emit txComplete();
    }
}

void AudioEngine::stopTx() {
    m_transmitting = false;
    if (m_txPlayer) {
        m_txPlayer->stop();
        delete m_txPlayer;
        m_txPlayer = nullptr;
    }
    if (m_txAudioOut) {
        delete m_txAudioOut;
        m_txAudioOut = nullptr;
    }
    if (m_txGainDevice) {
        m_txGainDevice->close();
        delete m_txGainDevice;
        m_txGainDevice = nullptr;
    }
    m_txWavData.clear();
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
