#include "SlidingDft.h"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace HavenFSK {

SlidingDft::SlidingDft(int windowLen, int firstBin, int lastBin)
    : m_windowLen(windowLen), m_firstBin(firstBin), m_lastBin(lastBin)
{
    const int numBins = lastBin - firstBin;
    m_rotator.resize(numBins);
    m_bin.assign(numBins, std::complex<double>(0.0, 0.0));
    m_delay.assign(windowLen, 0.0);

    const double twoPiOverN = 2.0 * M_PI / static_cast<double>(windowLen);
    for (int i = 0; i < numBins; ++i) {
        double phi = twoPiOverN * static_cast<double>(firstBin + i);
        m_rotator[i] = std::complex<double>(DAMPING * std::cos(phi),
                                             DAMPING * std::sin(phi));
    }

    m_dampingPow = 1.0;
    for (int i = 0; i < windowLen; ++i) m_dampingPow *= DAMPING;
}

void SlidingDft::push(float sample)
{
    double& oldest = m_delay[m_delayPtr];
    const double leaving  = m_dampingPow * oldest;
    const double incoming = static_cast<double>(sample) - leaving;
    oldest = static_cast<double>(sample);

    m_delayPtr = (m_delayPtr + 1) % m_windowLen;

    for (size_t i = 0; i < m_bin.size(); ++i)
        m_bin[i] = (m_bin[i] + incoming) * m_rotator[i];

    if (m_count < m_windowLen) ++m_count;
}

float SlidingDft::energyAt(int bin) const
{
    const auto& b = m_bin[static_cast<size_t>(bin - m_firstBin)];
    return static_cast<float>(b.real() * b.real() + b.imag() * b.imag());
}

void SlidingDft::reset()
{
    std::fill(m_delay.begin(), m_delay.end(), 0.0);
    std::fill(m_bin.begin(), m_bin.end(), std::complex<double>(0.0, 0.0));
    m_delayPtr = 0;
    m_count = 0;
}

} // namespace HavenFSK
