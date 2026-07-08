#include "Interleaver.h"
#include "MfskConstants.h"
#include "DspLog.h"

namespace HavenFSK {

// Both callers (Frame::assemble/parse) always pass exactly nBlocks*LDPC_N
// bits. The permutation is only a bijection at that exact size — for a
// shorter input, an in-range read can map to an out-of-range write, so
// BOTH indices must be guarded (the old code guarded only the read, which
// on short input was a silent heap overflow, not partial handling).
static void warnIfPartial(const char* fn, size_t size, int total) {
    if (static_cast<int>(size) != total)
        dspWarn("Interleaver::%s: %d bits != nBlocks*LDPC_N (%d) — "
                "output will be partial", fn, static_cast<int>(size), total);
}

std::vector<float> Interleaver::interleave(const std::vector<float>& bits, int nBlocks) {
    if (nBlocks <= 1) return bits;
    const int cols = LDPC_N;
    const int rows = nBlocks;
    warnIfPartial("interleave", bits.size(), rows * cols);

    std::vector<float> out(bits.size());
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            int inIdx  = row * cols + col;
            int outIdx = col * rows + row;
            if (inIdx  >= static_cast<int>(bits.size())) continue;
            if (outIdx >= static_cast<int>(out.size()))  continue;
            out[outIdx] = bits[inIdx];
        }
    }
    return out;
}

std::vector<float> Interleaver::deinterleave(const std::vector<float>& bits, int nBlocks) {
    if (nBlocks <= 1) return bits;
    const int cols = LDPC_N;
    const int rows = nBlocks;
    warnIfPartial("deinterleave", bits.size(), rows * cols);

    std::vector<float> out(bits.size());
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            int outIdx = row * cols + col;
            int inIdx  = col * rows + row;
            if (inIdx  >= static_cast<int>(bits.size())) continue;
            if (outIdx >= static_cast<int>(out.size()))  continue;
            out[outIdx] = bits[inIdx];
        }
    }
    return out;
}

} // namespace HavenFSK
