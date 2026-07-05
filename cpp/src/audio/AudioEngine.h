#pragma once
#include <QObject>
#include <QAudioSource>
#include <QAudioFormat>
#include <QAudioDevice>
#include <QIODevice>
#include <QBuffer>
#include <QStringList>
#include <QByteArray>
#include <QAudioSink>
#include <QTimer>
#include <QElapsedTimer>
#include <vector>
#include <memory>
#include <atomic>
#include <algorithm>
#include <cstring>

// GainedAudioDevice — QIODevice wrapper that applies a real-time gain
// multiplier to int16 PCM samples as QAudioSink reads them (pull mode).
//
// Any header bytes (see headerBytes below) pass through unchanged. PCM
// data after that has gain applied per-sample on every read cycle.
// m_gain is std::atomic<float> — safe to update from the main thread
// while QAudioSink reads from its audio rendering thread.
// Changes take effect within one read cycle (~100-200ms).
//
// m_data holds its own copy of the PCM (cheap — QByteArray is
// copy-on-write, so this is just a refcount bump, not a real copy)
// rather than a reference to the caller's buffer. AudioEngine::startTx()
// used to pass a reference to its own m_txPcmData member; that only
// stayed safe because of hand-maintained destruction ordering (gain
// device deleted before m_txPcmData.clear()). An owned copy removes that
// ordering dependency entirely instead of relying on it staying correct
// through future changes (see DECISIONS.md).
class GainedAudioDevice : public QIODevice {
    Q_OBJECT
public:
    // headerBytes: how many leading bytes of wavData to pass through
    // unmodified (and un-gained) before treating the rest as int16 PCM —
    // 44 for a WAV container (QMediaPlayer path), 0 for raw PCM with no
    // container (QAudioSink path, which gets format via QAudioFormat
    // instead of parsing a header).
    explicit GainedAudioDevice(const QByteArray& wavData,
                                float initialGain = 1.0f,
                                QObject* parent = nullptr,
                                qint64 headerBytes = 44)
        : QIODevice(parent)
        , m_data(wavData)
        , m_pos(0)
        , m_gain(std::max(0.0f, std::min(1.0f, initialGain)))
        , m_headerBytes(headerBytes)
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

        if (m_pos < m_headerBytes) {
            qint64 headerRemain = std::min(toRead, m_headerBytes - m_pos);
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

    QByteArray         m_data;
    qint64             m_pos;
    std::atomic<float> m_gain;
    qint64             m_headerBytes;
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
    // Convert samples to raw int16 PCM, wrap in GainedAudioDevice, play via
    // QAudioSink pull mode. Completion is a computed-duration QTimer, not
    // QAudioSink::stateChanged()/IdleState — see ADR-107: that signal fires
    // as soon as data is handed to the driver, not when playback genuinely
    // finishes (the same unreliability that previously ruled QAudioSink out
    // for TX — this works around it instead of depending on it).
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
    // Takes effect within one QAudioSink read cycle (~100-200ms).
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
    void onTxCompletionTimer();

private:
    // ── RX members ────────────────────────────────────────────────────────
    std::unique_ptr<QAudioSource> m_rxSource;
    QIODevice*                    m_rxDevice = nullptr;
    QByteArray                    m_rxBuffer;

    // Detects real-time gaps in RX audio delivery: if wall-clock time
    // between onRxDataAvailable() calls significantly exceeds the amount
    // of audio-time actually delivered, samples were likely dropped by
    // the OS/driver before Qt ever saw them (Qt6.11's QAudio::UnderrunError
    // is deprecated and no longer emitted, so this can't be detected via
    // QAudioSource's own error signal — see ADR-109). Root-caused a real
    // bug this way: a clean, discrete symbol-alignment shift found via
    // offline analysis of a captured recording, consistent with a block
    // of samples silently lost mid-transmission.
    QElapsedTimer m_rxGapTimer;

    // ── TX members (QAudioSink + GainedAudioDevice) ──────────────────────
    QAudioSink*        m_txAudioSink       {nullptr};
    GainedAudioDevice* m_txGainDevice      {nullptr};
    QByteArray         m_txPcmData;   // raw PCM — must outlive GainedAudioDevice
    QTimer*            m_txCompletionTimer {nullptr};

    // ── State ─────────────────────────────────────────────────────────────
    std::atomic<bool> m_receiving    {false};
    std::atomic<bool> m_transmitting {false};

    // ── Helpers ───────────────────────────────────────────────────────────
    static QAudioFormat havenFormat();
    static QAudioDevice findInputDevice(const QString& name);
    static std::vector<float> pcmToFloat(const QByteArray& pcm);
    static float computeRms(const std::vector<float>& samples);

    // Convert float32 samples to raw int16 PCM bytes (no container/header —
    // QAudioSink gets its format from the QAudioFormat passed at
    // construction, not by parsing one).
    static QByteArray floatToPcm16(const std::vector<float>& samples);
};
