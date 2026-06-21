#include "AudioEngine.h"

AudioEngine::AudioEngine(QObject *parent) : QObject(parent) {}
AudioEngine::~AudioEngine() = default;

bool AudioEngine::startRx(const QString&) { return false; }
bool AudioEngine::startTx(const QString&, const std::vector<float>&) { return false; }
void AudioEngine::stop() {}
