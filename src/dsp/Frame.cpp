#include "Frame.h"
#include "FEC.h"
#include "Modulator.h"
#include "Demodulator.h"
#include "Preamble.h"
#include "Interleaver.h"
#include <algorithm>
#include <cstring>
#include <cassert>
#include <QDebug>
#include <QString>

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

std::array<uint8_t, 2> Frame::majorityVoteHeader(
    const std::vector<std::array<uint8_t, 2>>& copies)
{
    std::array<uint8_t, 2> result{0, 0};
    int n = static_cast<int>(copies.size());
    for (int byteIdx = 0; byteIdx < 2; byteIdx++) {
        uint8_t out = 0;
        for (int bit = 7; bit >= 0; bit--) {
            int ones = 0;
            for (const auto& copy : copies)
                if ((copy[byteIdx] >> bit) & 1) ones++;
            int bitVal = (ones * 2 >= n) ? 1 : 0;
            out = static_cast<uint8_t>((out << 1) | bitVal);
        }
        result[byteIdx] = out;
    }
    return result;
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
        // Gray-decode tone index back to nibble value — mirrors grayEncode() in Modulator.
        int s0 = grayDecode(static_cast<uint8_t>(argmax(syms[offset + i])));
        int s1 = grayDecode(static_cast<uint8_t>(argmax(syms[offset + i + 1])));
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
    // Strip trailing null bytes
    size_t end = s.size();
    while (end > 0 && s[end - 1] == '\0') end--;
    std::string trimmed = s.substr(0, end);
    // Strip exactly one trailing space — the padding space added by assemble().
    // Removing all whitespace would corrupt messages with intentional trailing
    // spaces and break CRC verification on the RX side.
    if (!trimmed.empty() && trimmed.back() == ' ')
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

    // Generate preamble with continuous phase accumulator
    std::vector<float> preambleAudio = preamble.generate();

    // Seed Modulator with preamble's final phase — header starts exactly
    // where preamble left off. Zero discontinuity at preamble→header boundary.
    mod.setPhase(preamble.finalPhase());

    // Header sent 3x — each copy is continuous-phase from the previous.
    // RX majority-votes the 3 copies bit-by-bit (see Frame::parse()),
    // tolerating a fade hitting up to one full copy without corrupting
    // the recovered header.
    std::vector<uint8_t> hdrVec(hdr.begin(), hdr.end());
    std::vector<float> headerAudio = mod.modulate(hdrVec);
    for (int copy = 1; copy < 3; ++copy) {
        std::vector<float> headerAudioN = mod.modulate(hdrVec);
        headerAudio.insert(headerAudio.end(),
                           headerAudioN.begin(), headerAudioN.end());
    }

    std::vector<uint8_t> crcVec(crcBytes.begin(), crcBytes.end());
    std::vector<float> crcAudio = mod.modulate(crcVec);

    // Interleave payload bits across all nBlocks LDPC blocks before
    // packing to bytes/symbols — spreads HF fade bursts so LDPC sees
    // scattered errors rather than a concentrated run (see Interleaver.h).
    std::vector<float> interleavedBpsk = Interleaver::interleave(enc.bpsk, enc.nBlocks);
    std::vector<uint8_t> payloadBytes = bpskToBytes(interleavedBpsk);
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
    result.crcOk          = false;
    result.converged      = false;
    result.nBlocks        = 0;
    result.fecIterations  = 0;
    result.version        = 0;
    result.useFec         = false;

    // Minimum: 4 header symbols + 4 CRC symbols
    if ((int)softSymbols.size() < PAYLOAD_START) {
        result.error = "Frame too short";
        return result;
    }

    // ── Decode header — 3 copies, bit-level majority vote ──────────────────
    std::vector<std::array<uint8_t, 2>> hdrCopies;
    for (int c = 0; c < HEADER_COPIES; c++) {
        auto bytes = hardDecodeSymbols(softSymbols, c * HEADER_SYMS, HEADER_SYMS);
        if ((int)bytes.size() < HEADER_BYTES) {
            result.error = "Header decode failed";
            return result;
        }
        hdrCopies.push_back({bytes[0], bytes[1]});
    }

    if (!(hdrCopies[0] == hdrCopies[1] && hdrCopies[1] == hdrCopies[2])) {
        qDebug() << "Frame::parse: header copies differ — nBlocks candidates:"
                 << hdrCopies[0][1] << hdrCopies[1][1] << hdrCopies[2][1]
                 << "— using bit-level majority vote";
    }
    std::array<uint8_t, 2> hdr = majorityVoteHeader(hdrCopies);

    qDebug() << "Frame::parse hdr[0]=0x"
                + QString::number(hdr[0], 16).rightJustified(2, '0')
                + " hdr[1]=0x"
                + QString::number(hdr[1], 16).rightJustified(2, '0')
                + " expect hdr[0]=0x21 (3x-header majority vote, PAYLOAD_START=16)";

    uint8_t version, flags, nBlocks;
    if (!parseHeader(hdr, version, flags, nBlocks)) {
        result.error = "Unknown protocol version";
        return result;
    }
    result.version = version;
    result.useFec  = (flags & FLAG_FEC_ENABLED) != 0;
    result.nBlocks = nBlocks;

    // Sanity-check nBlocks before trusting it for buffer sizing.
    // A corrupted header byte can produce nBlocks=200+, which would demand
    // thousands of seconds of audio and cause a long collection timeout.
    constexpr int MAX_NBLOCKS = 125;
    if (nBlocks > MAX_NBLOCKS) {
        result.error = "nBlocks out of range";
        return result;
    }

    // ── Decode CRC (symbols 8-11, after double header) ────────────────────
    auto crcBytes = hardDecodeSymbols(softSymbols, HEADER_TOTAL_SYMS, CRC_SYMS);
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

        // Convert soft symbol energies to per-bit LLR values, then undo
        // the TX-side interleave (see Frame::assemble()) before FEC
        // decode — LLRs are still in the interleaved (transmission)
        // order at this point, matching Interleaver's bit-level domain.
        FEC fec;
        auto llr = fec.softToLLR(payloadSoft);
        auto deinterleavedLlr = Interleaver::deinterleave(llr, nBlocks);

        int origLen = nBlocks * LDPC_BYTES_PER_BLOCK;
        auto decRes = fec.decodeMessage(deinterleavedLlr, nBlocks, origLen);

        result.converged     = decRes.allConverged;
        result.fecIterations = decRes.totalIterations;

        std::string text(decRes.bytes.begin(), decRes.bytes.end());
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
