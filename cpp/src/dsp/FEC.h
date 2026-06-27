#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include "Constants.h"

namespace HavenFSK {

// Result of a single LDPC block decode attempt
struct DecodeResult {
    std::vector<uint8_t> bytes;  // LDPC_BYTES_PER_BLOCK decoded bytes
    bool converged;              // true if BP satisfied all parity checks
    int iterations;              // actual iterations used
};

class FEC {
public:
    FEC();

    // ── Encoder ───────────────────────────────────────────────────────────

    // Encode exactly LDPC_BYTES_PER_BLOCK (12) bytes into LDPC_N (192)
    // BPSK-modulated floats. bit 0 -> +1.0f, bit 1 -> -1.0f.
    std::vector<float> encodeBlock(const std::vector<uint8_t>& bytes) const;

    // Encode arbitrary-length payload into concatenated BPSK blocks.
    // Final block is zero-padded to LDPC_BYTES_PER_BLOCK if needed.
    struct EncodeResult {
        std::vector<float> bpsk;  // concatenated BPSK floats
        int nBlocks;              // number of LDPC blocks
        int origLen;              // original payload byte length
    };
    EncodeResult encodeMessage(const std::vector<uint8_t>& payload) const;

    // ── Decoder ───────────────────────────────────────────────────────────

    // Decode LDPC_N (192) LLR values to LDPC_BYTES_PER_BLOCK (12) bytes.
    // llr: positive = likely 0, negative = likely 1.
    DecodeResult decodeBlock(const std::vector<float>& llr) const;

    // Decode nBlocks of concatenated LLR values to origLen bytes.
    struct DecodeMessageResult {
        std::vector<uint8_t> bytes;  // decoded payload bytes (trimmed to origLen)
        bool allConverged;           // true if every LDPC block converged
        int totalIterations;         // sum of BP iterations across all blocks
    };
    DecodeMessageResult decodeMessage(const std::vector<float>& llr,
                                      int nBlocks,
                                      int origLen) const;

    // ── LLR Conversion ────────────────────────────────────────────────────

    // Convert Demodulator::demodulateToSoft() output to per-bit LLR values.
    // softSymbols: shape [num_symbols][NUM_TONES]
    // Returns: LLR vector of length num_symbols * BITS_PER_SYMBOL
    // Uses soft-output MFSK formula: LLR[b] = log(sum E0) - log(sum E1)
    // where E0/E1 are energies of tones where bit b is 0 or 1.
    std::vector<float> softToLLR(
        const std::vector<std::vector<float>>& softSymbols) const;

    // ── Utilities ─────────────────────────────────────────────────────────

    // Unpack bytes to bits, MSB first per byte
    static std::vector<uint8_t> unpackBits(const std::vector<uint8_t>& bytes);

    // Pack bits to bytes, MSB first per byte
    static std::vector<uint8_t> packBits(const std::vector<uint8_t>& bits);

    // Matrix accessor for testing
    const std::vector<std::vector<uint8_t>>& parityCheckMatrix() const {
        return m_H;
    }

private:
    // Parity check matrix [LDPC_M][LDPC_N] = [96][192]
    std::vector<std::vector<uint8_t>> m_H;

    // Sparse adjacency lists built from m_H
    std::vector<std::vector<int>> m_varToCheck;  // [LDPC_N] var -> checks
    std::vector<std::vector<int>> m_checkToVar;  // [LDPC_M] check -> vars

    // Systematic encoder support
    std::vector<std::vector<uint8_t>> m_encRows; // [LDPC_M][LDPC_K]
    std::vector<int> m_colPerm;                  // [LDPC_N]
    std::vector<int> m_colPermInv;               // [LDPC_N]

    void buildMatrixFromEmbedded();
    void buildSparseGraph();
    void buildEncoder();
};

} // namespace HavenFSK
