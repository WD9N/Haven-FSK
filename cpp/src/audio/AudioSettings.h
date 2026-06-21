#pragma once
#include <QSettings>
#include <QString>

namespace HavenFSK {

// Keys for audio device preferences in QSettings
static constexpr const char* SETTINGS_AUDIO_INPUT  = "audio/inputDevice";
static constexpr const char* SETTINGS_AUDIO_OUTPUT = "audio/outputDevice";

// Load saved audio device names. Returns empty string if not set
// (AudioEngine will use system default).
inline QString savedInputDevice() {
    QSettings s;
    return s.value(SETTINGS_AUDIO_INPUT, QString()).toString();
}

inline QString savedOutputDevice() {
    QSettings s;
    return s.value(SETTINGS_AUDIO_OUTPUT, QString()).toString();
}

// Save audio device selection.
inline void saveInputDevice(const QString& name) {
    QSettings s;
    s.setValue(SETTINGS_AUDIO_INPUT, name);
}

inline void saveOutputDevice(const QString& name) {
    QSettings s;
    s.setValue(SETTINGS_AUDIO_OUTPUT, name);
}

} // namespace HavenFSK
