#include "Varicode.h"
#include "Psk31VaricodeTable.h"

namespace HavenFSK {

std::vector<bool> Varicode::encode(const std::string& text) {
    std::vector<bool> bits;
    bits.reserve(text.size() * 8);
    for (unsigned char c : text) {
        const char* code = PSK31_VARICODE_TABLE[c];
        for (const char* p = code; *p != '\0'; ++p)
            bits.push_back(*p == '1');
        // Inter-character separator — two zero bits. Codewords never
        // contain "00" internally, so this is always unambiguous.
        bits.push_back(false);
        bits.push_back(false);
    }
    return bits;
}

std::optional<char> Varicode::decodeBit(bool bit) {
    if (!bit) {
        if (!m_accum.empty() && m_accum.back() == '0') {
            // "00" terminator seen. The codeword is everything before
            // the single trailing '0' already in the accumulator (that
            // '0' plus this new '0' form the two-zero separator).
            std::string codeword = m_accum.substr(0, m_accum.size() - 1);
            m_accum.clear();
            if (codeword.empty()) return std::nullopt;  // idle pattern
            return lookupCodeword(codeword);
        }
        m_accum.push_back('0');
    } else {
        m_accum.push_back('1');
    }

    if (m_accum.size() > MAX_ACCUM_BITS) m_accum.clear();
    return std::nullopt;
}

void Varicode::reset() {
    m_accum.clear();
}

std::optional<char> Varicode::lookupCodeword(const std::string& codeword) {
    for (int i = 0; i < 256; ++i) {
        if (codeword == PSK31_VARICODE_TABLE[i])
            return static_cast<char>(static_cast<unsigned char>(i));
    }
    return std::nullopt;
}

} // namespace HavenFSK
