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
// The gain value lives in AudioEngine (m_txGain), not here: this device
// is created and deleted per-transmission on the DSP thread, so any
// cross-thread caller holding a pointer to it could race its deletion.
// Instead the device holds a pointer to AudioEngine's std::atomic<float>,
// whose lifetime spans all transmissions — the GUI thread only ever
// writes that atomic (AudioEngine::setTxGain) and never touches this
// object. Changes take effect within one read cycle (~100-200ms).
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
    // gain: pointer to the owning AudioEngine's atomic gain value; must
    // outlive this device (guaranteed — the device is a child of the
    // engine and is deleted in onTxCompletionTimer()/stopTx()).
    explicit GainedAudioDevice(const QByteArray& wavData,
                                const std::atomic<float>* gain,
                                QObject* parent = nullptr,
                                qint64 headerBytes = 44)
        : QIODevice(parent)
        , m_data(wavData)
        , m_pos(0)
        , m_gain(gain)
        , m_headerBytes(headerBytes)
    {}

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

        float g = m_gain->load(std::memory_order_relaxed);

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

    QByteArray                m_data;
    qint64                    m_pos;
    const std::atomic<float>* m_gain;
    qint64                    m_headerBytes;
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

    // Update TX gain in real time — writes only m_txGain (a member of
    // this engine, alive for the engine's whole lifetime), so it is safe
    // to call directly from the GUI thread even while the DSP thread is
    // creating/deleting the per-transmission GainedAudioDevice.
    // Takes effect within one QAudioSink read cycle (~100-200ms).
    void setTxGain(float linear) {
        m_txGain.store(std::max(0.0f, std::min(1.0f, linear)),
                       std::memory_order_relaxed);
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

    // TX gain value read by GainedAudioDevice. Lives here (not in the
    // device) so setTxGain() from the GUI thread never touches the
    // per-transmission device object — see setTxGain() comment.
    std::atomic<float> m_txGain {1.0f};

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
