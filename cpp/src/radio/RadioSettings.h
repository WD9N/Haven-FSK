#pragma once
#include <QSettings>
#include <QString>
#include <cstdint>

namespace HavenFSK {

// QSettings keys for radio control configuration
namespace RadioSettingsKeys {
    static constexpr const char* RIG_METHOD   = "radio/method";
    // method values: "rigctld", "tci", "none"

    static constexpr const char* RIGCTLD_HOST = "radio/rigctld/host";
    static constexpr const char* RIGCTLD_PORT = "radio/rigctld/port";

    static constexpr const char* TCI_HOST     = "radio/tci/host";
    static constexpr const char* TCI_PORT     = "radio/tci/port";
}

// Station information keys
namespace StationKeys {
    static constexpr const char* CALLSIGN   = "station/callsign";
    static constexpr const char* GRID       = "station/grid";
    static constexpr const char* OP_NAME    = "station/opName";
    static constexpr const char* POTA_REF_1 = "station/pota/ref1";
    static constexpr const char* POTA_REF_2 = "station/pota/ref2";
    static constexpr const char* POTA_REF_3 = "station/pota/ref3";
    static constexpr const char* POTA_REF_4 = "station/pota/ref4";
    static constexpr const char* SOTA_REF   = "station/sota/ref";
    static constexpr const char* FD_CLASS   = "station/fd/class";
    static constexpr const char* FD_SECTION = "station/fd/section";
}

// Station information — loaded from QSettings
struct StationInfo {
    QString     callsign;
    QString     grid;
    QString     opName;
    QStringList potaRefs;  // up to 4, empty strings omitted
    QString     sotaRef;
    QString     fdClass;
    QString     fdSection;

    bool isActivator() const {
        return !potaRefs.isEmpty() || !sotaRef.isEmpty();
    }

    // Space-separated list of active POTA references
    QString myParks() const {
        return potaRefs.join(' ');
    }
};

inline StationInfo loadStationInfo() {
    QSettings s;
    StationInfo info;
    info.callsign  = s.value(StationKeys::CALLSIGN).toString().toUpper();
    info.grid      = s.value(StationKeys::GRID).toString().toUpper();
    info.opName    = s.value(StationKeys::OP_NAME).toString();
    info.sotaRef   = s.value(StationKeys::SOTA_REF).toString().toUpper();
    info.fdClass   = s.value(StationKeys::FD_CLASS).toString().toUpper();
    info.fdSection = s.value(StationKeys::FD_SECTION).toString().toUpper();

    const QStringList potaKeys = {
        StationKeys::POTA_REF_1, StationKeys::POTA_REF_2,
        StationKeys::POTA_REF_3, StationKeys::POTA_REF_4
    };
    for (const QString& key : potaKeys) {
        QString ref = s.value(key).toString().toUpper();
        if (!ref.isEmpty()) info.potaRefs.append(ref);
    }
    return info;
}

inline void saveStationInfo(const StationInfo& info) {
    QSettings s;
    s.setValue(StationKeys::CALLSIGN,   info.callsign);
    s.setValue(StationKeys::GRID,       info.grid);
    s.setValue(StationKeys::OP_NAME,    info.opName);
    s.setValue(StationKeys::SOTA_REF,   info.sotaRef);
    s.setValue(StationKeys::FD_CLASS,   info.fdClass);
    s.setValue(StationKeys::FD_SECTION, info.fdSection);

    const QStringList potaKeys = {
        StationKeys::POTA_REF_1, StationKeys::POTA_REF_2,
        StationKeys::POTA_REF_3, StationKeys::POTA_REF_4
    };
    for (int i = 0; i < 4; i++) {
        s.setValue(potaKeys[i],
                   i < info.potaRefs.size() ? info.potaRefs[i] : QString());
    }
}

// Rigctld connection settings
inline QString rigctldHost() {
    QSettings s;
    return s.value(RadioSettingsKeys::RIGCTLD_HOST, "localhost").toString();
}
inline uint16_t rigctldPort() {
    QSettings s;
    return static_cast<uint16_t>(
        s.value(RadioSettingsKeys::RIGCTLD_PORT, 4532).toInt());
}

// TCI connection settings
inline QString tciHost() {
    QSettings s;
    return s.value(RadioSettingsKeys::TCI_HOST, "localhost").toString();
}
inline uint16_t tciPort() {
    QSettings s;
    return static_cast<uint16_t>(
        s.value(RadioSettingsKeys::TCI_PORT, 50001).toInt());
}

} // namespace HavenFSK
