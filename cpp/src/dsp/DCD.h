#pragma once
#include <vector>
#include "Constants.h"
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

    void reset();

private:
    kiss_fftr_cfg m_fftCfg;
    int m_fftSize;
    bool m_active = false;
    int m_clearCount = 0;   // consecutive clear readings since last active

    // Returns true if signal energy >= DCD_THRESHOLD_DB above noise floor
    bool computeCarrierPresent(const std::vector<float>& chunk);
};

} // namespace HavenFSK
