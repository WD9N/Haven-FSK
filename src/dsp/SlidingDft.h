#pragma once
#include <complex>
#include <vector>

namespace HavenFSK {

// Recursive sliding DFT: maintains the exact windowLen-point DFT for a
// contiguous range of bins [firstBin, lastBin), updated in O(lastBin -
// firstBin) time for every new sample — rather than recomputing a block
// FFT from scratch at a handful of discrete window alignments.
//
// After push(), bins reflect the DFT of the trailing windowLen-sample
// window ending at the sample just pushed. Because this refreshes after
// every single sample, every possible symbol-timing alignment is
// available continuously as a byproduct of demodulation that has to
// happen anyway — there's no gap between tested alignments the way there
// is with a sparse discrete timing search.
//
// Technique: a bank of leaky recursive resonators, one per bin (see
// Jacobsen & Lyons, "The Sliding DFT"). fldigi's `sfft` class
// (src/filters/filters.cxx) uses the same technique for its MFSK/Olivia
// receiver — that was the reference point for adopting this approach for
// HAVEN, but this is an independent implementation sized for HAVEN's own
// tone-bin layout, not a port of that code. See DECISIONS.md ADR-105.
class SlidingDft {
public:
    SlidingDft(int windowLen, int firstBin, int lastBin);

    // Feed one new real-valued sample.
    void push(float sample);

    // True once at least windowLen samples have been pushed since
    // construction or the last reset() — before that, bin values are
    // still filling the window and are not meaningful.
    bool isStable() const { return m_count >= m_windowLen; }

    int firstBin() const { return m_firstBin; }
    int lastBin()  const { return m_lastBin; }

    // Magnitude-squared energy at the given bin (must be in
    // [firstBin, lastBin)).
    float energyAt(int bin) const;

    // Clears all internal state (delay line, bin accumulators, sample
    // count) back to construction-time defaults.
    void reset();

private:
    int m_windowLen;
    int m_firstBin;
    int m_lastBin;
    int m_count = 0;
    int m_delayPtr = 0;

    // Damping factor slightly below 1.0 — keeps the recursion numerically
    // stable over long runs. An undamped sliding DFT recursion is only
    // marginally stable and accumulates floating-point error without
    // bound; this makes it a very slowly leaky integrator instead.
    static constexpr double DAMPING = 0.99999999999;

    std::vector<std::complex<double>> m_rotator; // per-bin unit rotation
    std::vector<std::complex<double>> m_bin;      // per-bin running DFT value
    std::vector<double> m_delay;                  // circular input history
    double m_dampingPow;                          // DAMPING ^ windowLen
};

} // namespace HavenFSK
