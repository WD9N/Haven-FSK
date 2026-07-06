#pragma once
#include <vector>

namespace HavenFSK {

// Interleaver — row/column block interleaver spanning nBlocks LDPC
// blocks. Writes bit-level values (BPSK floats on TX, LLRs on RX) row-
// wise (one row per LDPC_N-bit block, in natural order) and reads them
// out column-wise, so a contiguous burst error (e.g. a fade lasting a
// fraction of a second) that would otherwise corrupt many consecutive
// bits within one block instead lands as isolated single-bit errors
// spread across many different blocks — LDPC belief propagation
// corrects randomly-distributed errors far more reliably than a burst
// concentrated in one block's parity-check neighborhood.
//
// No-op (returns input unchanged) when nBlocks <= 1: there is only one
// block to spread a burst across, so row/column interleaving has
// nothing to do. This is a protocol/wire-format change — see
// DECISIONS.md for the PROTOCOL_VERSION bump this is bundled with.
class Interleaver {
public:
    static std::vector<float> interleave(const std::vector<float>& bits, int nBlocks);
    static std::vector<float> deinterleave(const std::vector<float>& bits, int nBlocks);
};

} // namespace HavenFSK
