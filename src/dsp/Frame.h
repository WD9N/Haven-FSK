#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <array>
#include "MfskConstants.h"

namespace HavenFSK {

// Result of a frame parse attempt
struct ParseResult {
    std::string text;       // decoded text, empty on failure
    bool crcOk;             // true if CRC verified
    bool converged;         // true if all FEC blocks converged
    int nBlocks;            // FEC blocks decoded
    int fecIterations;      // total BP iterations across all blocks (0 if no FEC)
    uint8_t version;        // protocol version from header
    bool useFec;            // whether FEC was active
    std::string error;      // non-empty string on failure, empty on success
};

class Frame {
public:
    Frame();

    // ── Transmit path ─────────────────────────────────────────────────────

    // Build a complete transmit frame from UTF-8 text.
    // Returns audio samples ready for AudioEngine::startTx().
    // Output order: preamble audio + header audio + CRC audio + payload audio
    std::vector<float> assemble(const std::string& text) const;

    // ── Receive path ──────────────────────────────────────────────────────

    // Parse a received frame from soft symbol energies.
    // softSymbols: output of Demodulator::demodulateToSoft(), with
    // symbol 0 being the first symbol AFTER preamble detection.
    ParseResult parse(
        const std::vector<std::vector<float>>& softSymbols) const;

    // Total post-preamble symbols needed for a complete frame.
    // Used by DspPipeline::tryCompleteFrame() to know when to decode.
    static int frameSymsNeeded(int nBlocks) {
        return HEADER_TOTAL_SYMS + CRC_SYMS
               + nBlocks * (LDPC_N / BITS_PER_SYMBOL);
    }

    // ── CRC-16/CCITT-FALSE ────────────────────────────────────────────────
    // Polynomial 0x1021, init 0xFFFF, no reflection.
    // Known vector: crc16("123456789") == 0x29B1
    static uint16_t crc16(const std::vector<uint8_t>& data);
    static uint16_t crc16(const uint8_t* data, size_t len);

private:
    // Version 2: header sent 3x with bit-level majority voting (was 2x,
    // "prefer copy 1" — see DECISIONS.md ADR-102) and payload bits pass
    // through Interleaver (new in v2) — both are wire-format changes,
    // bundled into one version bump so old- and new-version stations
    // reject each other's frames cleanly via parseHeader()'s version
    // check rather than silently misdecoding.
    static constexpr uint8_t PROTOCOL_VERSION = 0x02;
    static constexpr uint8_t FLAG_FEC_ENABLED = 0x01;

    // Header layout — header is sent 3x for majority-vote redundancy on HF
    static constexpr int HEADER_BYTES       = 2;
    static constexpr int HEADER_SYMS        = HEADER_BYTES * 2;        // 4 per copy
    static constexpr int HEADER_COPIES      = 3;
    static constexpr int HEADER_TOTAL_SYMS  = HEADER_SYMS * HEADER_COPIES;  // 12 (three copies)
    static constexpr int CRC_BYTES          = 2;
    static constexpr int CRC_SYMS           = CRC_BYTES * 2;           // 4
    static constexpr int PAYLOAD_START      = HEADER_TOTAL_SYMS + CRC_SYMS;  // 16

    // Build 2-byte header
    std::array<uint8_t, 2> buildHeader(uint8_t nBlocks) const;

    // Parse 2-byte header. Returns false if version unknown.
    bool parseHeader(const std::array<uint8_t, 2>& hdr,
                     uint8_t& version,
                     uint8_t& flags,
                     uint8_t& nBlocks) const;

    // Bit-level majority vote across HEADER_COPIES decoded header byte
    // sets. For each bit position independently, the value held by at
    // least 2 of 3 copies wins — more robust than whole-byte majority,
    // which would require two entire copies to match exactly.
    static std::array<uint8_t, 2> majorityVoteHeader(
        const std::vector<std::array<uint8_t, 2>>& copies);

    // Take hard decision (argmax) from one soft symbol energy vector
    static int argmax(const std::vector<float>& energies);

    // Hard-decode a slice of softSymbols to bytes using argmax
    // Processes pairs of symbols: byte = (sym[i] << 4) | sym[i+1]
    static std::vector<uint8_t> hardDecodeSymbols(
        const std::vector<std::vector<float>>& syms,
        int offset,
        int count);

    // Convert BPSK float array back to bytes for modulation
    // +1.0f -> bit 0, -1.0f -> bit 1, then pack MSB first
    static std::vector<uint8_t> bpskToBytes(const std::vector<float>& bpsk);

    // Strip trailing null bytes, then exactly one trailing space (the padding
    // space added by assemble()). User-entered trailing spaces are preserved.
    static std::string stripPadding(const std::string& s);
};

} // namespace HavenFSK
