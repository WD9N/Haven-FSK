#pragma once
#include <QObject>
#include <QAudioSource>
#include <QAudioSink>
#include <QAudioFormat>
#include <QAudioDevice>
#include <QIODevice>
#include <QBuffer>
#include <QStringList>
#include <QByteArray>
#include <vector>
#include <memory>
#include <atomic>

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
    // Play the given samples through the named output device.
    // Uses QAudioSink pull mode — passes QBuffer to sink, Qt handles chunking.
    // Emits txComplete() when QBuffer is genuinely exhausted (all audio played).
    bool startTx(const QString& deviceName,
                 const std::vector<float>& samples);

    // ── Control ───────────────────────────────────────────────────────────
    void stop();
    void stopRx();
    void stopTx();

    bool isReceiving() const;
    bool isTransmitting() const;

signals:
    void rxDataReady(const std::vector<float>& samples);
    void txComplete();
    void audioError(const QString& message);
    void rxLevelChanged(float level);

private slots:
    void onRxDataAvailable();
    void onTxStateChanged(QAudio::State state);

private:
    // ── RX members ────────────────────────────────────────────────────────
    std::unique_ptr<QAudioSource> m_rxSource;
    QIODevice*                    m_rxDevice = nullptr;
    QByteArray                    m_rxBuffer;

    // ── TX members (pull mode) ────────────────────────────────────────────
    // QBuffer pull mode: Qt reads from m_txQBuffer as backend needs data.
    // Both m_txPcmBuffer and m_txQBuffer are members — must outlive the sink.
    std::unique_ptr<QAudioSink> m_txSink;
    QByteArray                  m_txPcmBuffer;         // complete PCM data
    QBuffer*                    m_txQBuffer {nullptr}; // wraps m_txPcmBuffer

    // ── State ─────────────────────────────────────────────────────────────
    std::atomic<bool> m_receiving    {false};
    std::atomic<bool> m_transmitting {false};

    // ── Helpers ───────────────────────────────────────────────────────────
    static QAudioFormat havenFormat();
    static QAudioDevice findInputDevice(const QString& name);
    static QAudioDevice findOutputDevice(const QString& name);
    static std::vector<float> pcmToFloat(const QByteArray& pcm);
    static float computeRms(const std::vector<float>& samples);
};
