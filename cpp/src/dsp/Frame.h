#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <array>
#include "Constants.h"

namespace HavenFSK {

// Result of a frame parse attempt
struct ParseResult {
    std::string text;     // decoded text, empty on failure
    bool crcOk;           // true if CRC verified
    bool converged;       // true if FEC BP converged
    int nBlocks;          // FEC blocks decoded
    uint8_t version;      // protocol version from header
    bool useFec;          // whether FEC was active
    std::string error;    // non-empty string on failure, empty on success
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

    // ── CRC-16/CCITT-FALSE ────────────────────────────────────────────────
    // Polynomial 0x1021, init 0xFFFF, no reflection.
    // Known vector: crc16("123456789") == 0x29B1
    static uint16_t crc16(const std::vector<uint8_t>& data);
    static uint16_t crc16(const uint8_t* data, size_t len);

private:
    static constexpr uint8_t PROTOCOL_VERSION = 0x01;
    static constexpr uint8_t FLAG_FEC_ENABLED = 0x01;

    // Header layout
    static constexpr int HEADER_BYTES  = 2;
    static constexpr int HEADER_SYMS   = HEADER_BYTES * 2;  // 4
    static constexpr int CRC_BYTES     = 2;
    static constexpr int CRC_SYMS      = CRC_BYTES * 2;     // 4
    static constexpr int PAYLOAD_START = HEADER_SYMS + CRC_SYMS;  // 8

    // Build 2-byte header
    std::array<uint8_t, 2> buildHeader(uint8_t nBlocks) const;

    // Parse 2-byte header. Returns false if version unknown.
    bool parseHeader(const std::array<uint8_t, 2>& hdr,
                     uint8_t& version,
                     uint8_t& flags,
                     uint8_t& nBlocks) const;

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

    // Strip trailing null bytes and whitespace from decoded text
    static std::string stripPadding(const std::string& s);
};

} // namespace HavenFSK
