// Spec artifact generator — Phase 1 of ROADMAP.md (ADR-134).
//
// Emits, into spec/ at the repo root, the machine-readable artifacts a
// third-party HAVEN-FSK implementer needs, generated from THIS codebase
// (never transcribed by hand):
//
//   spec/ldpc_h_192_96.alist         parity check matrix, MacKay alist
//   spec/ldpc_generator_192_96.txt   codeword bit mapping (systematic
//                                    positions + parity equations)
//   spec/test_vectors/tv*.txt        per-stage golden vectors
//   spec/test_vectors/tv*.wav        reference TX audio, 48 kHz mono s16
//
// Every test vector's audio is round-tripped through the real
// Demodulator + Frame::parse before being written; the tool exits
// non-zero if any vector fails to decode back to its source text.
//
// Usage: spec_artifacts <repo-root>
#include "FEC.h"
#include "Frame.h"
#include "Modulator.h"
#include "Preamble.h"
#include "Interleaver.h"
#include "Demodulator.h"
#include "MfskConstants.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <array>

using namespace HavenFSK;

static int g_failures = 0;

// ── helpers ────────────────────────────────────────────────────────────────

static FILE* openOut(const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { printf("FAIL: cannot open %s\n", path.c_str()); g_failures++; }
    else      printf("writing %s\n", path.c_str());
    return f;
}

static std::string hex(const std::vector<uint8_t>& bytes) {
    std::string s;
    char buf[4];
    for (uint8_t b : bytes) { snprintf(buf, sizeof buf, "%02X", b); s += buf; }
    return s;
}

// Printable-ASCII rendering with \xNN escapes for control bytes (markers).
static std::string escaped(const std::string& text) {
    std::string s;
    char buf[8];
    for (unsigned char c : text) {
        if (c >= 0x20 && c <= 0x7E && c != '\\') s += (char)c;
        else { snprintf(buf, sizeof buf, "\\x%02X", c); s += buf; }
    }
    return s;
}

static std::vector<uint8_t> bpskToBits(const std::vector<float>& bpsk) {
    std::vector<uint8_t> bits(bpsk.size());
    for (size_t i = 0; i < bpsk.size(); i++) bits[i] = bpsk[i] < 0.0f ? 1 : 0;
    return bits;
}

static void writeWav(const std::string& path, const std::vector<float>& audio) {
    FILE* f = openOut(path);
    if (!f) return;
    const uint32_t rate = SAMPLE_RATE, n = (uint32_t)audio.size();
    const uint32_t dataBytes = n * 2, byteRate = rate * 2;
    const uint16_t channels = 1, block = 2, bits = 16, pcm = 1;
    uint32_t riffLen = 36 + dataBytes, fmtLen = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riffLen, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmtLen, 4, 1, f);
    fwrite(&pcm, 2, 1, f); fwrite(&channels, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&byteRate, 4, 1, f);
    fwrite(&block, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
    for (float s : audio) {
        float c = s < -1.0f ? -1.0f : (s > 1.0f ? 1.0f : s);
        int16_t v = (int16_t)(c * 32767.0f);
        fwrite(&v, 2, 1, f);
    }
    fclose(f);
}

// ── alist (MacKay format, 1-based indices) ─────────────────────────────────

static void writeAlist(const std::string& path, const FEC& fec) {
    const auto& H = fec.parityCheckMatrix();  // [96][192]
    FILE* f = openOut(path);
    if (!f) return;

    std::vector<std::vector<int>> colRows(LDPC_N), rowCols(LDPC_M);
    for (int r = 0; r < LDPC_M; r++)
        for (int c = 0; c < LDPC_N; c++)
            if (H[r][c]) { colRows[c].push_back(r + 1); rowCols[r].push_back(c + 1); }

    size_t maxCol = 0, maxRow = 0;
    for (auto& v : colRows) maxCol = std::max(maxCol, v.size());
    for (auto& v : rowCols) maxRow = std::max(maxRow, v.size());

    fprintf(f, "%d %d\n", LDPC_N, LDPC_M);
    fprintf(f, "%zu %zu\n", maxCol, maxRow);
    for (int c = 0; c < LDPC_N; c++) fprintf(f, "%zu ", colRows[c].size());
    fprintf(f, "\n");
    for (int r = 0; r < LDPC_M; r++) fprintf(f, "%zu ", rowCols[r].size());
    fprintf(f, "\n");
    for (int c = 0; c < LDPC_N; c++) {
        for (size_t i = 0; i < maxCol; i++)
            fprintf(f, "%d ", i < colRows[c].size() ? colRows[c][i] : 0);
        fprintf(f, "\n");
    }
    for (int r = 0; r < LDPC_M; r++) {
        for (size_t i = 0; i < maxRow; i++)
            fprintf(f, "%d ", i < rowCols[r].size() ? rowCols[r][i] : 0);
        fprintf(f, "\n");
    }
    fclose(f);
}

// ── generator mapping ──────────────────────────────────────────────────────
// G derived behaviorally: encode each unit message e_k; codeword bit p of
// e_k's encoding = G[p][k]. Linear code, so this fully characterizes the
// encoder with zero dependence on internal representation.

static void writeGenerator(const std::string& path, const FEC& fec) {
    // G[p] = set of message-bit indices XORed into codeword position p
    std::vector<std::vector<int>> G(LDPC_N);
    for (int k = 0; k < LDPC_K; k++) {
        std::vector<uint8_t> msg(LDPC_BYTES_PER_BLOCK, 0);
        msg[k / 8] = (uint8_t)(1u << (7 - (k % 8)));   // MSB-first bit k
        auto bits = bpskToBits(fec.encodeBlock(msg));
        for (int p = 0; p < LDPC_N; p++)
            if (bits[p]) G[p].push_back(k);
    }

    const auto& perm = fec.systematicColumnOrder();

    // Cross-check: codeword position perm[k] must be exactly message bit k.
    for (int k = 0; k < LDPC_K; k++) {
        if (G[perm[k]].size() != 1 || G[perm[k]][0] != k) {
            printf("FAIL: systematic position check k=%d\n", k);
            g_failures++;
        }
    }

    FILE* f = openOut(path);
    if (!f) return;
    fprintf(f,
        "HAVEN-FSK LDPC(192,96) encoder mapping\n"
        "Generated from the reference implementation (tools/spec_artifacts).\n"
        "\n"
        "Message bits are the 96 bits of a 12-byte block, MSB first within\n"
        "each byte (message bit index = byte_index*8 + (7 - bit_in_byte)).\n"
        "Codeword positions 0..191 are in transmitted order (before the\n"
        "payload interleaver, spec section 5.6).\n"
        "\n"
        "SECTION 1 - systematic positions\n"
        "MSG_POSITION[k] = codeword position carrying message bit k verbatim.\n"
        "A decoder recovers the message by reading these 96 positions.\n\n");
    for (int k = 0; k < LDPC_K; k++)
        fprintf(f, "MSG_POSITION[%d] = %d\n", k, perm[k]);
    fprintf(f,
        "\nSECTION 2 - full mapping\n"
        "For each codeword position p: 'M k' means it carries message bit k;\n"
        "'P k1^k2^...' means it is the XOR (GF(2) sum) of those message bits.\n\n");
    for (int p = 0; p < LDPC_N; p++) {
        if (G[p].size() == 1 && perm[G[p][0]] == p) {
            fprintf(f, "cw[%d] = M %d\n", p, G[p][0]);
        } else {
            fprintf(f, "cw[%d] = P ", p);
            for (size_t i = 0; i < G[p].size(); i++)
                fprintf(f, "%s%d", i ? "^" : "", G[p][i]);
            fprintf(f, "\n");
        }
    }
    fclose(f);
}

// ── test vectors ───────────────────────────────────────────────────────────

static void writeVector(const std::string& dir, const std::string& name,
                        const std::string& description, const std::string& text)
{
    Frame frame;
    FEC fec;

    // Reproduce assemble()'s data path stage by stage (audio comes from the
    // real assemble() itself below).
    std::string padded = text + " ";
    std::vector<uint8_t> payload(padded.begin(), padded.end());
    auto enc = fec.encodeMessage(payload);

    std::array<uint8_t, 2> hdr = { (uint8_t)0x21, (uint8_t)enc.nBlocks };
    std::vector<uint8_t> crcInput;
    crcInput.push_back(hdr[0]); crcInput.push_back(hdr[1]);
    crcInput.insert(crcInput.end(), payload.begin(), payload.end());
    uint16_t crc = Frame::crc16(crcInput);
    std::vector<uint8_t> crcBytes = { (uint8_t)(crc >> 8), (uint8_t)(crc & 0xFF) };

    auto codedBits   = bpskToBits(enc.bpsk);                       // pre-interleave
    auto interleaved = bpskToBits(Interleaver::interleave(enc.bpsk, enc.nBlocks));
    auto payloadBytes = FEC::packBits(interleaved);

    // Tone sequence: preamble tones are RAW indices (not Gray-coded);
    // everything after is byte -> two Gray-coded tones, high nibble first
    // (mirrors Modulator::bytesToSymbols).
    std::vector<int> tones(PREAMBLE_SYMBOLS, PREAMBLE_SYMBOLS + PREAMBLE_LENGTH);
    std::vector<uint8_t> wireBytes;
    for (int c = 0; c < 3; c++) { wireBytes.push_back(hdr[0]); wireBytes.push_back(hdr[1]); }
    wireBytes.insert(wireBytes.end(), crcBytes.begin(), crcBytes.end());
    wireBytes.insert(wireBytes.end(), payloadBytes.begin(), payloadBytes.end());
    for (uint8_t b : wireBytes) {
        tones.push_back(grayEncode((b >> 4) & 0x0F));
        tones.push_back(grayEncode(b & 0x0F));
    }

    // Reference audio from the real TX path.
    std::vector<float> audio = frame.assemble(text);

    // Round-trip self-check through the real RX path.
    Demodulator demod;
    auto soft = demod.demodulateToSoft(audio,
                                       PREAMBLE_LENGTH * SAMPLES_PER_SYMBOL);
    ParseResult rx = frame.parse(soft);
    bool ok = rx.crcOk && rx.text == text;
    printf("%s round-trip: %s\n", name.c_str(), ok ? "OK" : "FAIL");
    if (!ok) { g_failures++; return; }

    FILE* f = openOut(dir + "/" + name + ".txt");
    if (!f) return;
    fprintf(f, "HAVEN-FSK protocol v2 test vector: %s\n", name.c_str());
    fprintf(f, "%s\n", description.c_str());
    fprintf(f, "Generated from the reference implementation (tools/spec_artifacts).\n");
    fprintf(f, "All byte values hex, MSB-first bit order within bytes.\n\n");
    fprintf(f, "TEXT (\\xNN = raw byte)     : %s\n", escaped(text).c_str());
    fprintf(f, "PAYLOAD (text + 1 space)   : %s\n", hex(payload).c_str());
    fprintf(f, "PAYLOAD LENGTH             : %d bytes\n", (int)payload.size());
    fprintf(f, "NBLOCKS                    : %d\n", enc.nBlocks);
    fprintf(f, "HEADER (one copy, sent 3x) : %s\n",
            hex({hdr[0], hdr[1]}).c_str());
    fprintf(f, "CRC-16/CCITT-FALSE         : %04X (wire bytes %s, big-endian)\n",
            crc, hex(crcBytes).c_str());
    for (int b = 0; b < enc.nBlocks; b++) {
        std::vector<uint8_t> blockBits(codedBits.begin() + b * LDPC_N,
                                       codedBits.begin() + (b + 1) * LDPC_N);
        fprintf(f, "CODEWORD BLOCK %-2d (pre-interleave, 192 bits): %s\n",
                b, hex(FEC::packBits(blockBits)).c_str());
    }
    fprintf(f, "INTERLEAVED PAYLOAD BYTES  : %s\n", hex(payloadBytes).c_str());
    fprintf(f, "\nTONE SEQUENCE (%d symbols: 16 preamble [raw, not Gray-coded]\n"
               "+ 12 header + 4 CRC + %d payload; all non-preamble tones are\n"
               "Gray-coded nibbles, high nibble of each byte first):\n",
            (int)tones.size(), enc.nBlocks * (LDPC_N / BITS_PER_SYMBOL));
    for (size_t i = 0; i < tones.size(); i++)
        fprintf(f, "%d%s", tones[i], (i + 1) % 16 ? " " : "\n");
    if (tones.size() % 16) fprintf(f, "\n");
    fprintf(f, "\nREFERENCE AUDIO: %s.wav — 48000 Hz mono 16-bit PCM.\n"
               "Phase starts at 0 and is continuous across all symbol\n"
               "boundaries (CPMFSK); peak-normalized; raised-cosine\n"
               "amplitude ramps (~3.2 ms, 153 samples) at the very start\n"
               "and end of the whole transmission only.\n", name.c_str());
    fclose(f);

    writeWav(dir + "/" + name + ".wav", audio);
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: spec_artifacts <repo-root>\n"); return 2; }
    std::string root = argv[1];
    std::string spec = root + "/spec";
    std::string tv   = spec + "/test_vectors";

    // CRC sanity per spec section 4.3
    const char* tvstr = "123456789";
    if (Frame::crc16((const uint8_t*)tvstr, 9) != 0x29B1) {
        printf("FAIL: CRC self-test\n"); return 1;
    }

    FEC fec;
    writeAlist(spec + "/ldpc_h_192_96.alist", fec);
    writeGenerator(spec + "/ldpc_generator_192_96.txt", fec);

    writeVector(tv, "tv1_single_block",
        "Minimal case: payload fits one LDPC block; interleaver is a no-op.",
        "HELLO");
    writeVector(tv, "tv2_two_blocks",
        "Two LDPC blocks; exercises the payload interleaver and final-block "
        "null padding.",
        "CQ CQ DE WD9N WD9N K");
    writeVector(tv, "tv3_markers_multiblock",
        "Inline field markers (spec section 6.1): 0x1F <id> value 0x1E "
        "spans inside the visible text; three LDPC blocks.",
        std::string("\x1F") + "cN8SDR\x1E DE \x1F" + "dWD9N\x1E UR \x1F"
        + "r52\x1E INTO \x1F" + "pUS-1017\x1E K");

    printf(g_failures ? "%d FAILURES\n" : "ALL ARTIFACTS WRITTEN\n", g_failures);
    return g_failures ? 1 : 0;
}
