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
    static constexpr const char* POTA_REFS  = "station/pota/refs";
    // Stored as QSettings::setValue(key, QStringList) — unbounded list
    static constexpr const char* SOTA_REF   = "station/sota/ref";
    static constexpr const char* FD_CLASS   = "station/fd/class";
    static constexpr const char* FD_SECTION = "station/fd/section";
    static constexpr const char* STATE      = "station/state";
    static constexpr const char* COUNTY     = "station/county";
}

// Station information — loaded from QSettings
struct StationInfo {
    QString     callsign;
    QString     grid;
    QString     opName;
    QStringList potaRefs;  // unbounded, empty strings omitted
    QString     sotaRef;
    QString     fdClass;
    QString     fdSection;
    QString     state;     // operator's state or province
    QString     county;    // operator's county

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
    info.state     = s.value(StationKeys::STATE).toString();
    info.county    = s.value(StationKeys::COUNTY).toString();

    info.potaRefs = s.value(StationKeys::POTA_REFS,
                             QStringList()).toStringList();
    info.potaRefs.removeAll(QString());
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
    s.setValue(StationKeys::STATE,      info.state);
    s.setValue(StationKeys::COUNTY,     info.county);

    s.setValue(StationKeys::POTA_REFS, info.potaRefs);
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
