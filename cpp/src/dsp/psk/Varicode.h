#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

// PSK31 Varicode — variable-length character code (G3PLX). See
// Psk31VaricodeTable.h for the table itself and its provenance.

namespace HavenFSK {

class Varicode {
public:
    // Encode text to a bitstream, including the "00" inter-character
    // separator after each codeword. true = '1' bit, false = '0' bit.
    static std::vector<bool> encode(const std::string& text);

    // Stateful decoder: feed one demodulated bit at a time. Accumulates
    // until a codeword-terminating "00" is seen; returns the decoded
    // character at that point, or nullopt while still accumulating (or
    // when "00" terminates an empty/idle accumulator — no character).
    std::optional<char> decodeBit(bool bit);

    void reset();

private:
    std::string m_accum;  // '0'/'1' bits accumulated since last terminator

    // Guard against unbounded growth from a channel that never produces
    // "00" (e.g. pure noise) — longest real codeword is 12 bits.
    static constexpr size_t MAX_ACCUM_BITS = 16;

    static std::optional<char> lookupCodeword(const std::string& codeword);
};

} // namespace HavenFSK
