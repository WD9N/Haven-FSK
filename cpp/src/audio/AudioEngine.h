#pragma once
#include <QObject>
#include <vector>

class AudioEngine : public QObject
{
    Q_OBJECT
public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;

    bool startRx(const QString& deviceName);
    bool startTx(const QString& deviceName, const std::vector<float>& samples);
    void stop();

signals:
    void rxDataReady(const std::vector<float>& samples);
    void txComplete();
};
