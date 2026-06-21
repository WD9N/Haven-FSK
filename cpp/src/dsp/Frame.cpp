#include "Frame.h"
#include "FEC.h"
#include "Modulator.h"
#include "Demodulator.h"
#include "Preamble.h"
#include <algorithm>
#include <cstring>
#include <cassert>

namespace HavenFSK {

Frame::Frame() {}

// ── CRC-16/CCITT-FALSE ────────────────────────────────────────────────────
// Polynomial: 0x1021, Initial: 0xFFFF, No reflection
// Test vector: crc16("123456789") == 0x29B1

uint16_t Frame::crc16(const uint8_t* data, size_t len) {
    uint16_t crc = CRC16_INIT;  // 0xFFFF from Constants.h
    for (size_t i = 0; i < len; i++) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ CRC16_POLY;  // 0x1021 from Constants.h
            else
                crc <<= 1;
        }
    }
    return crc;
}

uint16_t Frame::crc16(const std::vector<uint8_t>& data) {
    return crc16(data.data(), data.size());
}

// ── Private helpers ───────────────────────────────────────────────────────

std::array<uint8_t, 2> Frame::buildHeader(uint8_t nBlocks) const {
    uint8_t byte0 = static_cast<uint8_t>(
        ((PROTOCOL_VERSION & 0x0F) << 4) | (FLAG_FEC_ENABLED & 0x0F));
    return {byte0, nBlocks};
}

bool Frame::parseHeader(const std::array<uint8_t, 2>& hdr,
                        uint8_t& version,
                        uint8_t& flags,
                        uint8_t& nBlocks) const {
    version = (hdr[0] >> 4) & 0x0F;
    flags   = hdr[0] & 0x0F;
    nBlocks = hdr[1];
    return (version == PROTOCOL_VERSION);
}

int Frame::argmax(const std::vector<float>& energies) {
    return static_cast<int>(
        std::distance(energies.begin(),
                      std::max_element(energies.begin(), energies.end())));
}

std::vector<uint8_t> Frame::hardDecodeSymbols(
    const std::vector<std::vector<float>>& syms,
    int offset,
    int count)
{
    // count = number of symbols to process (must be even)
    // Returns count/2 bytes
    std::vector<uint8_t> bytes;
    bytes.reserve(count / 2);
    for (int i = 0; i + 1 < count; i += 2) {
        int s0 = argmax(syms[offset + i]);
        int s1 = argmax(syms[offset + i + 1]);
        bytes.push_back(static_cast<uint8_t>((s0 << 4) | (s1 & 0x0F)));
    }
    return bytes;
}

std::vector<uint8_t> Frame::bpskToBytes(const std::vector<float>& bpsk) {
    // +1.0f -> 0, -1.0f -> 1
    int nBits = static_cast<int>(bpsk.size());
    std::vector<uint8_t> bits(nBits);
    for (int i = 0; i < nBits; i++)
        bits[i] = (bpsk[i] < 0.0f) ? 1 : 0;
    return FEC::packBits(bits);
}

std::string Frame::stripPadding(const std::string& s) {
    // Strip trailing null bytes first, then whitespace
    size_t end = s.size();
    while (end > 0 && s[end - 1] == '\0') end--;
    std::string trimmed = s.substr(0, end);
    while (!trimmed.empty() &&
           (trimmed.back() == ' '  ||
            trimmed.back() == '\n' ||
            trimmed.back() == '\r' ||
            trimmed.back() == '\t'))
        trimmed.pop_back();
    return trimmed;
}

// ── Transmit path ─────────────────────────────────────────────────────────

std::vector<float> Frame::assemble(const std::string& text) const {
    // 1. Append trailing space — ensures last symbol fully transmitted
    std::string padded = text + " ";
    std::vector<uint8_t> payload(padded.begin(), padded.end());

    // 2. FEC encode
    FEC fec;
    auto enc = fec.encodeMessage(payload);

    // 3. Build header
    auto hdr = buildHeader(static_cast<uint8_t>(enc.nBlocks));

    // 4. CRC covers header + ORIGINAL unencoded payload bytes
    std::vector<uint8_t> crcInput;
    crcInput.reserve(2 + payload.size());
    crcInput.push_back(hdr[0]);
    crcInput.push_back(hdr[1]);
    crcInput.insert(crcInput.end(), payload.begin(), payload.end());
    uint16_t crcVal = crc16(crcInput);

    // 5. CRC as 2 big-endian bytes
    std::array<uint8_t, 2> crcBytes = {
        static_cast<uint8_t>((crcVal >> 8) & 0xFF),
        static_cast<uint8_t>(crcVal & 0xFF)
    };

    // 6. Modulate each section
    Preamble preamble;
    Modulator mod;

    std::vector<float> preambleAudio = preamble.generate();

    std::vector<uint8_t> hdrVec(hdr.begin(), hdr.end());
    std::vector<float> headerAudio = mod.modulate(hdrVec);

    std::vector<uint8_t> crcVec(crcBytes.begin(), crcBytes.end());
    std::vector<float> crcAudio = mod.modulate(crcVec);

    // Convert BPSK floats to bytes for modulation
    std::vector<uint8_t> payloadBytes = bpskToBytes(enc.bpsk);
    std::vector<float> payloadAudio = mod.modulate(payloadBytes);

    // 7. Concatenate
    std::vector<float> frame;
    frame.reserve(preambleAudio.size() + headerAudio.size() +
                  crcAudio.size()     + payloadAudio.size());
    frame.insert(frame.end(), preambleAudio.begin(), preambleAudio.end());
    frame.insert(frame.end(), headerAudio.begin(),   headerAudio.end());
    frame.insert(frame.end(), crcAudio.begin(),      crcAudio.end());
    frame.insert(frame.end(), payloadAudio.begin(),  payloadAudio.end());

    return frame;
}

// ── Receive path ──────────────────────────────────────────────────────────

ParseResult Frame::parse(
    const std::vector<std::vector<float>>& softSymbols) const
{
    ParseResult result{};
    result.crcOk     = false;
    result.converged = false;
    result.nBlocks   = 0;
    result.version   = 0;
    result.useFec    = false;

    // Minimum: 4 header symbols + 4 CRC symbols
    if ((int)softSymbols.size() < PAYLOAD_START) {
        result.error = "Frame too short";
        return result;
    }

    // ── Decode header (symbols 0-3, hard decision) ────────────────────────
    auto hdrBytes = hardDecodeSymbols(softSymbols, 0, HEADER_SYMS);
    if ((int)hdrBytes.size() < HEADER_BYTES) {
        result.error = "Header decode failed";
        return result;
    }
    std::array<uint8_t, 2> hdr = {hdrBytes[0], hdrBytes[1]};

    uint8_t version, flags, nBlocks;
    if (!parseHeader(hdr, version, flags, nBlocks)) {
        result.error = "Unknown protocol version";
        return result;
    }
    result.version = version;
    result.useFec  = (flags & FLAG_FEC_ENABLED) != 0;
    result.nBlocks = nBlocks;

    // ── Decode CRC (symbols 4-7, hard decision) ───────────────────────────
    auto crcBytes = hardDecodeSymbols(softSymbols, HEADER_SYMS, CRC_SYMS);
    if ((int)crcBytes.size() < CRC_BYTES) {
        result.error = "CRC decode failed";
        return result;
    }
    uint16_t rxCrc = (static_cast<uint16_t>(crcBytes[0]) << 8) | crcBytes[1];

    // ── Decode payload (symbols 8+) ───────────────────────────────────────
    if (result.useFec && nBlocks > 0) {
        int symsNeeded = nBlocks * (LDPC_N / BITS_PER_SYMBOL);  // nBlocks * 48
        if ((int)softSymbols.size() < PAYLOAD_START + symsNeeded) {
            result.error = "Payload truncated";
            return result;
        }

        // Extract payload soft symbols
        std::vector<std::vector<float>> payloadSoft(
            softSymbols.begin() + PAYLOAD_START,
            softSymbols.begin() + PAYLOAD_START + symsNeeded);

        // Convert soft symbol energies to per-bit LLR values
        FEC fec;
        auto llr = fec.softToLLR(payloadSoft);

        // Decode
        int origLen = nBlocks * LDPC_BYTES_PER_BLOCK;
        auto decoded = fec.decodeMessage(llr, nBlocks, origLen);

        // Check convergence on first block as representative
        auto firstBlock = fec.decodeBlock(
            std::vector<float>(llr.begin(),
                               llr.begin() + std::min((int)llr.size(), LDPC_N)));
        result.converged = firstBlock.converged;

        // Decode bytes as UTF-8
        std::string text(decoded.begin(), decoded.end());
        result.text = stripPadding(text);

    } else {
        // No FEC — hard decode payload symbols directly
        int payloadSyms = static_cast<int>(softSymbols.size()) - PAYLOAD_START;
        if (payloadSyms > 0) {
            auto rawBytes = hardDecodeSymbols(softSymbols,
                                              PAYLOAD_START,
                                              payloadSyms & ~1);
            std::string text(rawBytes.begin(), rawBytes.end());
            result.text = stripPadding(text);
        }
        result.converged = true;  // No FEC = always "converged"
    }

    // ── Verify CRC ────────────────────────────────────────────────────────
    // CRC was computed over: header bytes + original padded payload bytes
    // We must recompute over header + decoded text + " " (the trailing space)
    std::string paddedText = result.text + " ";
    std::vector<uint8_t> crcInput;
    crcInput.reserve(2 + paddedText.size());
    crcInput.push_back(hdr[0]);
    crcInput.push_back(hdr[1]);
    crcInput.insert(crcInput.end(), paddedText.begin(), paddedText.end());
    uint16_t calcCrc = crc16(crcInput);

    result.crcOk  = (calcCrc == rxCrc);
    result.error  = "";

    return result;
}

} // namespace HavenFSK
