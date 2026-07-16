#pragma once
#include <cstdint>
#include <QSettings>
#include <QString>
#include "../dsp/IModem.h"

// Dial frequencies for the band buttons in the Transmit panel, sourced
// from BAND_PLAN.md at the repo root — that document is the authority
// for the DEFAULTS below; keep this table in sync with it, not the
// other way around. The operator can override any frequency or button
// label — right-click a band button (macro-style) or use
// Operating > Band Frequencies... Overrides live in QSettings under
// "bandplan/<index>/" and are removed when set back to the default, so
// future BAND_PLAN.md revisions flow through to unmodified entries.
//
// HAVEN MFSK column: the suggested HAVEN-FSK frequencies from
// BAND_PLAN.md's summary table (20 m / 14.090 is the recommended
// primary calling frequency; 160 m / 1.808 sits in the ARRL digital
// segment — confirmed by WD9N 2026-07-15).
//
// PSK31 column: where PSK31 activity actually lives per BAND_PLAN.md's
// established-frequency listings (the mode's own watering holes, not
// HAVEN's). Where the doc lists both a "primary" and a "general"
// PSK31 spot (40 m, 20 m), this uses the busier general cluster.
// 6 m has no PSK31 entry in the doc; 50.290 is the common convention.
//
// HAVEN's PSK31 audio carrier is fixed at 1000 Hz, so the emitted
// signal sits 1 kHz above dial; MFSK tones sit 500-968 Hz above dial.

namespace HavenFSK {

struct BandPlanEntry {
    const char* label;      // default button text (sans "m"), e.g. "40"
    uint64_t    psk31Hz;    // PSK31 activity dial (BAND_PLAN.md listings)
    uint64_t    mfskHz;     // HAVEN-FSK suggested dial (BAND_PLAN.md summary)
};

inline constexpr BandPlanEntry BAND_PLAN[] = {
    // band     PSK31 dial   HAVEN MFSK dial
    { "160",    1845000,      1808000 },
    { "80",     3580000,      3585000 },
    { "40",     7070000,      7065000 },
    { "30",    10140000,     10142000 },
    { "20",    14070000,     14090000 },  // 14.090 = HAVEN primary calling freq
    { "17",    18100000,     18110000 },
    { "15",    21063000,     21090000 },
    { "12",    24920000,     24927000 },
    { "10",    28080000,     28130000 },
    { "6",     50290000,     50323000 },
};

inline constexpr int BAND_PLAN_COUNT =
    static_cast<int>(sizeof(BAND_PLAN) / sizeof(BAND_PLAN[0]));

inline QString bandPlanKey(int i, const char* field) {
    return QString("bandplan/%1/%2").arg(i).arg(QLatin1String(field));
}

inline const char* bandModeField(ModemMode mode) {
    return (mode == ModemMode::Psk31) ? "psk31" : "mfsk";
}

inline uint64_t defaultDialHz(int i, ModemMode mode) {
    return (mode == ModemMode::Psk31) ? BAND_PLAN[i].psk31Hz
                                      : BAND_PLAN[i].mfskHz;
}

inline QString defaultBandLabel(int i) {
    return QString(QLatin1String(BAND_PLAN[i].label)) + QLatin1String("m");
}

// Effective dial for a band button: operator override if one is stored,
// else the BAND_PLAN.md default. Also reads the pre-2026-07-16
// label-keyed override ("bandplan/<label>/<mode>") so overrides saved
// by the first release of the editor keep working.
inline uint64_t suggestedDialHz(int i, ModemMode mode) {
    QSettings s;
    QString key = bandPlanKey(i, bandModeField(mode));
    if (s.contains(key)) return s.value(key).toULongLong();
    QString legacy = QString("bandplan/%1/%2")
        .arg(QLatin1String(BAND_PLAN[i].label),
             QLatin1String(bandModeField(mode)));
    return s.value(legacy,
                   static_cast<qulonglong>(defaultDialHz(i, mode)))
        .toULongLong();
}

// Store an override; setting a value back to the default removes the
// keys (current and legacy) so future default changes take effect.
inline void setSuggestedDialHz(int i, ModemMode mode, uint64_t hz) {
    QSettings s;
    QString legacy = QString("bandplan/%1/%2")
        .arg(QLatin1String(BAND_PLAN[i].label),
             QLatin1String(bandModeField(mode)));
    if (hz == defaultDialHz(i, mode)) {
        s.remove(bandPlanKey(i, bandModeField(mode)));
        s.remove(legacy);
    } else {
        s.setValue(bandPlanKey(i, bandModeField(mode)),
                   static_cast<qulonglong>(hz));
        s.remove(legacy);
    }
}

inline QString bandLabel(int i) {
    QSettings s;
    return s.value(bandPlanKey(i, "label"), defaultBandLabel(i)).toString();
}

inline void setBandLabel(int i, const QString& label) {
    QSettings s;
    QString trimmed = label.trimmed();
    if (trimmed.isEmpty() || trimmed == defaultBandLabel(i))
        s.remove(bandPlanKey(i, "label"));
    else
        s.setValue(bandPlanKey(i, "label"), trimmed);
}

} // namespace HavenFSK
