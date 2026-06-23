#pragma once
#include <QObject>
#include <QAudioSource>
#include <QAudioSink>
#include <QAudioFormat>
#include <QAudioDevice>
#include <QIODevice>
#include <QStringList>
#include <QByteArray>
#include <QTimer>
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
    // Returns list of available audio input device names for UI dropdown.
    static QStringList availableInputDevices();

    // Returns list of available audio output device names for UI dropdown.
    static QStringList availableOutputDevices();

    // ── RX ────────────────────────────────────────────────────────────────
    // Open the named input device and begin streaming audio.
    // deviceName: empty string = use system default input device.
    // Returns true on success, false if device not found or doesn't
    // support 48000 Hz mono. Emits audioError() on failure.
    bool startRx(const QString& deviceName = QString());

    // ── TX ────────────────────────────────────────────────────────────────
    // Play the given samples through the named output device.
    // deviceName: empty string = use system default output device.
    // Emits txComplete() when playback finishes.
    // Returns true if playback started successfully.
    bool startTx(const QString& deviceName,
                 const std::vector<float>& samples);

    // ── Control ───────────────────────────────────────────────────────────
    // Stop all audio activity (both RX and TX).
    void stop();

    // Stop RX only.
    void stopRx();

    // Stop TX only (aborts current transmission).
    void stopTx();

    // True if RX is currently active.
    bool isReceiving() const;

    // True if TX is currently active.
    bool isTransmitting() const;

signals:
    // Emitted when a chunk of RX audio is ready for DSP processing.
    // samples: AUDIO_CHUNK_SAMPLES (2048) float32 values, range [-1.0, 1.0]
    void rxDataReady(const std::vector<float>& samples);

    // Emitted when TX playback completes normally.
    void txComplete();

    // Emitted on any audio error (device not found, format not supported,
    // device disconnected, etc.). message is human-readable for UI display.
    void audioError(const QString& message);

    // Emitted when RX audio level changes, for level meter display.
    // level: RMS level 0.0 to 1.0
    void rxLevelChanged(float level);

private slots:
    void onRxDataAvailable();
    void onTxStateChanged(QAudio::State state);
    void onWriteMore();  // progressive TX buffer feed

private:
    // ── RX members ────────────────────────────────────────────────────────
    std::unique_ptr<QAudioSource> m_rxSource;
    QIODevice*                    m_rxDevice  = nullptr;
    QByteArray                    m_rxBuffer;

    // ── TX members ────────────────────────────────────────────────────────
    std::unique_ptr<QAudioSink>   m_txSink;
    QIODevice*                    m_txDevice  = nullptr;
    QByteArray                    m_txBuffer;
    int                           m_txOffset  = 0;
    // Timer-based TX completion — WASAPI signals IdleState immediately after
    // writing to the driver buffer, not after hardware finishes playback.
    QTimer*                       m_txTimer   = nullptr;
    // Progressive write timer — feeds the driver buffer in chunks as it drains.
    // WASAPI/VAC only accept ~500ms at a time; we must keep writing until done.
    QTimer*                       m_writeTimer = nullptr;

    // ── State ─────────────────────────────────────────────────────────────
    std::atomic<bool> m_receiving    {false};
    std::atomic<bool> m_transmitting {false};

    // ── Helpers ───────────────────────────────────────────────────────────
    // Build the QAudioFormat used for all devices.
    static QAudioFormat havenFormat();

    // Find a QAudioDevice by name from the input or output list.
    // Returns default device if name is empty or not found (and logs warning).
    static QAudioDevice findInputDevice(const QString& name);
    static QAudioDevice findOutputDevice(const QString& name);

    // Convert interleaved int16 PCM bytes to float32 vector.
    // Qt6 Multimedia delivers audio as int16 by default on some platforms.
    static std::vector<float> pcmToFloat(const QByteArray& pcm);

    // Convert float32 vector to interleaved int16 PCM bytes for output.
    static QByteArray floatToPcm(const std::vector<float>& samples);

    // Compute RMS level of a float sample buffer.
    static float computeRms(const std::vector<float>& samples);
};
