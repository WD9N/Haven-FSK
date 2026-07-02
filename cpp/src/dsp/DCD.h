#pragma once
#include <vector>
#include "MfskConstants.h"
#include "kiss_fftr.h"

namespace HavenFSK {

class DCD {
public:
    DCD();
    ~DCD();

    // Feed one chunk of audio samples
    // Returns true if carrier is currently detected (channel busy)
    bool update(const std::vector<float>& chunk);

    bool isActive() const { return m_active; }

    // Returns most recent signal-to-noise ratio in dB.
    // Signal band: DCD_FREQ_LOW to DCD_FREQ_HIGH (450-1050 Hz)
    // Noise reference band: 150-400 Hz
    // Returns 0.0 if not enough data yet.
    float lastSnrDb() const { return m_lastSnrDb; }

    void reset();

private:
    kiss_fftr_cfg m_fftCfg;
    int m_fftSize;
    bool m_active = false;
    int m_clearCount = 0;   // consecutive clear readings since last active
    float m_lastSnrDb = 0.0f;

    // Returns true if signal energy >= DCD_THRESHOLD_DB above noise floor
    bool computeCarrierPresent(const std::vector<float>& chunk);
};

} // namespace HavenFSK
