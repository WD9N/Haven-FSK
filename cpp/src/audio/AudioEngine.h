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
    // Convert samples to an in-memory WAV file and play via QMediaPlayer.
    // StoppedState signals genuine playback completion on all platforms.
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
    void onTxPlaybackStateChanged(QMediaPlayer::PlaybackState state);

private:
    // ── RX members ────────────────────────────────────────────────────────
    std::unique_ptr<QAudioSource> m_rxSource;
    QIODevice*                    m_rxDevice = nullptr;
    QByteArray                    m_rxBuffer;

    // ── TX members (QMediaPlayer) ─────────────────────────────────────────
    QMediaPlayer*     m_txPlayer    {nullptr};
    QAudioOutput*     m_txAudioOut  {nullptr};
    QBuffer*          m_txWavBuffer {nullptr};
    QByteArray        m_txWavData;   // WAV header + PCM data — must outlive player

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
