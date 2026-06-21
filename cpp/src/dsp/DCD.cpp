#include "DCD.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace HavenFSK {

static int nextPow2(int n)
{
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

DCD::DCD()
    : m_fftSize(nextPow2(AUDIO_CHUNK_SAMPLES))
{
    m_fftCfg = kiss_fftr_alloc(m_fftSize, 0, nullptr, nullptr);
    if (!m_fftCfg)
        throw std::runtime_error("kiss_fftr_alloc failed (DCD)");
}

DCD::~DCD()
{
    kiss_fftr_free(m_fftCfg);
}

bool DCD::computeCarrierPresent(const std::vector<float>& chunk)
{
    // Zero-pad or truncate to m_fftSize
    std::vector<float> buf(m_fftSize, 0.0f);
    int n = std::min(static_cast<int>(chunk.size()), m_fftSize);
    for (int i = 0; i < n; ++i)
        buf[i] = chunk[i];

    std::vector<kiss_fft_cpx> out(m_fftSize / 2 + 1);
    kiss_fftr(m_fftCfg, buf.data(), out.data());

    double signalSum = 0.0;
    int    signalCount = 0;
    double noiseSum  = 0.0;
    int    noiseCount = 0;

    for (int k = 0; k <= m_fftSize / 2; ++k) {
        double freq = static_cast<double>(k) * SAMPLE_RATE / m_fftSize;
        double energy = out[k].r * out[k].r + out[k].i * out[k].i;

        if (freq >= DCD_FREQ_LOW && freq <= DCD_FREQ_HIGH) {
            signalSum += energy;
            ++signalCount;
        } else if (freq >= 150.0 && freq <= 400.0) {
            noiseSum += energy;
            ++noiseCount;
        }
    }

    if (noiseCount == 0 || signalCount == 0) return false;

    double signalMean = signalSum / signalCount;
    double noiseMean  = noiseSum  / noiseCount;

    if (noiseMean <= 0.0) return false;

    double dbAbove = 10.0 * std::log10(signalMean / noiseMean);
    return dbAbove >= DCD_THRESHOLD_DB;
}

bool DCD::update(const std::vector<float>& chunk)
{
    if (computeCarrierPresent(chunk)) {
        m_active     = true;
        m_clearCount = 0;
    } else {
        ++m_clearCount;
        if (m_clearCount >= DCD_HOLDOFF_CHUNKS)
            m_active = false;
    }
    return m_active;
}

void DCD::reset()
{
    m_active     = false;
    m_clearCount = 0;
}

} // namespace HavenFSK
