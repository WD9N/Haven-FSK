#pragma once
#include <cstdint>
#include <QSettings>
#include <QString>
#include "../dsp/IModem.h"

// Dial frequencies for the band buttons in the Transmit panel, sourced
// from BAND_PLAN.md at the repo root — that document is the authority
// for the DEFAULTS below; keep this table in sync with it, not the
// other way around. The operator can override any value via
// Operating > Band Frequencies... (stored in QSettings under
// "bandplan/", cleared when set back to the default).
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
    const char* label;      // button text, e.g. "40"
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

inline uint64_t defaultDialHz(const BandPlanEntry& e, ModemMode mode) {
    return (mode == ModemMode::Psk31) ? e.psk31Hz : e.mfskHz;
}

inline QString bandPlanKey(const BandPlanEntry& e, ModemMode mode) {
    return QString("bandplan/%1/%2")
        .arg(QLatin1String(e.label),
             mode == ModemMode::Psk31 ? QLatin1String("psk31")
                                      : QLatin1String("mfsk"));
}

// Effective dial for a band button: operator override if one is stored,
// else the BAND_PLAN.md default.
inline uint64_t suggestedDialHz(const BandPlanEntry& e, ModemMode mode) {
    QSettings s;
    return s.value(bandPlanKey(e, mode),
                   static_cast<qulonglong>(defaultDialHz(e, mode)))
        .toULongLong();
}

// Store an override; setting a value back to the default removes the
// key so future default changes (BAND_PLAN.md revisions) take effect.
inline void setSuggestedDialHz(const BandPlanEntry& e, ModemMode mode,
                               uint64_t hz) {
    QSettings s;
    if (hz == defaultDialHz(e, mode)) s.remove(bandPlanKey(e, mode));
    else s.setValue(bandPlanKey(e, mode), static_cast<qulonglong>(hz));
}

} // namespace HavenFSK
