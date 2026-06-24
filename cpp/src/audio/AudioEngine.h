#pragma once
#include <QObject>
#include <QAudioSource>
#include <QAudioFormat>
#include <QAudioDevice>
#include <QIODevice>
#include <QBuffer>
#include <QStringList>
#include <QByteArray>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <vector>
#include <memory>
#include <atomic>
#include <algorithm>
#include <cstring>

// GainedAudioDevice — QIODevice wrapper that applies a real-time gain
// multiplier to int16 PCM samples as QMediaPlayer reads them.
//
// WAV header (44 bytes) passes through unchanged. PCM data after the
// header has gain applied per-sample on every read cycle.
// m_gain is std::atomic<float> — safe to update from the main thread
// while QMediaPlayer reads from its audio rendering thread.
// Changes take effect within one read cycle (~100-200ms).
class GainedAudioDevice : public QIODevice {
    Q_OBJECT
public:
    explicit GainedAudioDevice(const QByteArray& wavData,
                                float initialGain = 1.0f,
                                QObject* parent = nullptr)
        : QIODevice(parent)
        , m_data(wavData)
        , m_pos(0)
        , m_gain(std::max(0.0f, std::min(1.0f, initialGain)))
    {}

    void setGain(float linear) {
        m_gain.store(std::max(0.0f, std::min(1.0f, linear)),
                     std::memory_order_relaxed);
    }

    float gain() const { return m_gain.load(std::memory_order_relaxed); }

    bool   isSequential() const override { return false; }
    qint64 size()         const override {
        return static_cast<qint64>(m_data.size());
    }
    bool atEnd() const override {
        return m_pos >= static_cast<qint64>(m_data.size());
    }

protected:
    qint64 readData(char* data, qint64 maxSize) override {
        if (m_pos >= static_cast<qint64>(m_data.size())) return 0;

        qint64 available = static_cast<qint64>(m_data.size()) - m_pos;
        qint64 toRead    = std::min(maxSize, available);

        static constexpr qint64 WAV_HEADER = 44;

        if (m_pos < WAV_HEADER) {
            qint64 headerRemain = std::min(toRead, WAV_HEADER - m_pos);
            std::memcpy(data, m_data.constData() + m_pos,
                        static_cast<size_t>(headerRemain));
            m_pos += headerRemain;
            if (headerRemain < toRead)
                return headerRemain + readPcm(data + headerRemain,
                                              toRead - headerRemain);
            return headerRemain;
        }
        return readPcm(data, toRead);
    }

    qint64 writeData(const char*, qint64) override { return -1; }

private:
    qint64 readPcm(char* data, qint64 maxSize) {
        qint64 available = static_cast<qint64>(m_data.size()) - m_pos;
        qint64 toRead    = std::min(maxSize, available);

        float g = m_gain.load(std::memory_order_relaxed);

        const int16_t* src = reinterpret_cast<const int16_t*>(
            m_data.constData() + m_pos);
        int16_t* dst    = reinterpret_cast<int16_t*>(data);
        qint64 nSamples = toRead / static_cast<qint64>(sizeof(int16_t));

        for (qint64 i = 0; i < nSamples; i++) {
            float s = static_cast<float>(src[i]) * g;
            s = std::max(-32768.0f, std::min(32767.0f, s));
            dst[i] = static_cast<int16_t>(s);
        }

        qint64 handled = nSamples * static_cast<qint64>(sizeof(int16_t));
        if (handled < toRead)
            std::memcpy(data + handled,
                        m_data.constData() + m_pos + handled,
                        static_cast<size_t>(toRead - handled));

        m_pos += toRead;
        return toRead;
    }

    const QByteArray&  m_data;
    qint64             m_pos;
    std::atomic<float> m_gain;
};

class AudioEngine : public QObject
{
    Q_OBJECT

public:
    explicit AudioEngine(QObject* parent = nullptr);
    ~AudioEngine() override;

    // ── Device enumeration ────────────────────────────────────────────────
    static QStringList availableInputDevices();
    static QStringList availableOutputDevices();

    // ── RX ────────────────────────────────────────────────────────────────
    bool startRx(const QString& deviceName = QString());

    // ── TX ────────────────────────────────────────────────────────────────
    // Convert samples to an in-memory WAV, wrap in GainedAudioDevice,
    // play via QMediaPlayer. StoppedState signals completion.
    bool startTx(const QString& deviceName,
                 const std::vector<float>& samples,
                 float initialGain = 1.0f);

    // ── Control ───────────────────────────────────────────────────────────
    void stop();
    void stopRx();
    void stopTx();

    bool isReceiving() const;
    bool isTransmitting() const;

    // Update TX gain in real time — atomic, safe from main thread.
    // Takes effect within one QMediaPlayer read cycle (~100-200ms).
    void setTxGain(float linear) {
        if (m_txGainDevice)
            m_txGainDevice->setGain(linear);
    }

signals:
    void rxDataReady(const std::vector<float>& samples);
    void txComplete();
    void audioError(const QString& message);
    void rxLevelChanged(float level);

private slots:
    void onRxDataAvailable();
    void onTxPlaybackStateChanged(QMediaPlayer::PlaybackState state);

private:
    // ── RX members ────────────────────────────────────────────────────────
    std::unique_ptr<QAudioSource> m_rxSource;
    QIODevice*                    m_rxDevice = nullptr;
    QByteArray                    m_rxBuffer;

    // ── TX members (QMediaPlayer + GainedAudioDevice) ────────────────────
    QMediaPlayer*      m_txPlayer     {nullptr};
    QAudioOutput*      m_txAudioOut   {nullptr};
    GainedAudioDevice* m_txGainDevice {nullptr};
    QByteArray         m_txWavData;   // WAV data — must outlive GainedAudioDevice

    // ── State ─────────────────────────────────────────────────────────────
    std::atomic<bool> m_receiving    {false};
    std::atomic<bool> m_transmitting {false};

    // ── Helpers ───────────────────────────────────────────────────────────
    static QAudioFormat havenFormat();
    static QAudioDevice findInputDevice(const QString& name);
    static std::vector<float> pcmToFloat(const QByteArray& pcm);
    static float computeRms(const std::vector<float>& samples);

    // Build a complete WAV file in memory from float32 samples
    QByteArray buildWav(const std::vector<float>& samples) const;
};
