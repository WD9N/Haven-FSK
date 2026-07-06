#include "Interleaver.h"
#include "MfskConstants.h"

namespace HavenFSK {

std::vector<float> Interleaver::interleave(const std::vector<float>& bits, int nBlocks) {
    if (nBlocks <= 1) return bits;
    const int cols = LDPC_N;
    const int rows = nBlocks;

    std::vector<float> out(bits.size());
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            int inIdx = row * cols + col;
            if (inIdx >= static_cast<int>(bits.size())) continue;
            int outIdx = col * rows + row;
            out[outIdx] = bits[inIdx];
        }
    }
    return out;
}

std::vector<float> Interleaver::deinterleave(const std::vector<float>& bits, int nBlocks) {
    if (nBlocks <= 1) return bits;
    const int cols = LDPC_N;
    const int rows = nBlocks;

    std::vector<float> out(bits.size());
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            int outIdx = row * cols + col;
            int inIdx = col * rows + row;
            if (inIdx >= static_cast<int>(bits.size())) continue;
            out[outIdx] = bits[inIdx];
        }
    }
    return out;
}

} // namespace HavenFSK
