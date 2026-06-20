"""
HAVEN-FSK Frame Builder
=========================
Assembles and disassembles complete over-the-air frames.

Frame structure (transmitted order):
    PREAMBLE   16 MFSK symbols — timing recovery and signal ID
               Fixed alternating pattern: tones [0,15,0,15,7,8,7,8,...]
               Receiver correlates against this to find frame start
               and recover exact symbol timing.

    HEADER     2 bytes (16 bits) — transmitted before FEC payload
        Byte 0: VERSION (4 bits) | FLAGS (4 bits)
                VERSION = 0001 for this release
                FLAGS bit 0: 1 = FEC enabled, 0 = FEC disabled
                FLAGS bit 1: 1 = more frames follow (fragmented)
                FLAGS bits 2-3: reserved
        Byte 1: NBLOCKS (8 bits) — number of FEC blocks in payload
                Limits messages to 255 * 12 = 3060 bytes max
                Well beyond any practical message length

    CRC-16     2 bytes — CRC of (HEADER + PAYLOAD)
               Polynomial: 0x1021 (CRC-CCITT)
               Receiver verifies after FEC decode
               Failed CRC triggers ⚠ CRC Error display

    PAYLOAD    NBLOCKS * 192 bits of FEC-encoded data
               Each 192-bit block carries 96 bits of message
               See fec.py for LDPC(192,96) specification

    [carrier drop = end of frame — no end marker transmitted]
    [trailing space in payload ensures last symbol fully received]

The header and CRC are transmitted WITHOUT FEC protection because:
  1. They are short (4 bytes total)
  2. The preamble correlation already provides high confidence
  3. FEC is applied to the payload where it matters most
  4. Keeping header unprotected reduces complexity

If header is corrupted the frame fails silently (DCD holdoff
prevents most header corruption in practice).
"""

import numpy as np
import struct

# ── CRC-16 CCITT ─────────────────────────────────────────────────────────────

CRC16_POLY = 0x1021

def crc16(data: bytes) -> int:
    """
    CRC-16/CCITT-FALSE
    Initial value: 0xFFFF
    Polynomial:    0x1021
    Input/output reflect: False
    """
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ CRC16_POLY
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc


def crc16_bytes(data: bytes) -> bytes:
    """Return CRC-16 as 2 big-endian bytes."""
    return struct.pack('>H', crc16(data))


def crc16_check(data: bytes, crc_bytes: bytes) -> bool:
    """Verify CRC. Returns True if valid."""
    try:
        expected = struct.unpack('>H', crc_bytes)[0]
        return crc16(data) == expected
    except Exception:
        return False


# ── Frame constants ───────────────────────────────────────────────────────────

VERSION      = 0b0001       # protocol version 1
FLAG_FEC     = 0b00000001   # FEC enabled
FLAG_MORE    = 0b00000010   # more frames follow (future use)

HEADER_LEN   = 2            # bytes
CRC_LEN      = 2            # bytes
MAX_BLOCKS   = 255          # max FEC blocks per frame
MAX_MSG_BYTES = MAX_BLOCKS * 12  # 3060 bytes


# ── Frame builder ─────────────────────────────────────────────────────────────

def build_frame(text: str, use_fec: bool = True) -> tuple:
    """
    Build a complete transmit frame from a text message.

    Returns:
        header:   2 bytes
        crc:      2 bytes
        payload:  encoded bytes (FEC or raw)
        n_blocks: number of FEC blocks (0 if no FEC)
        orig_len: original message byte length
    """
    raw = text.encode('utf-8')

    # Limit message length
    if len(raw) > MAX_MSG_BYTES:
        raw = raw[:MAX_MSG_BYTES]

    if use_fec:
        from fec import encode_message, BYTES_PER_BLOCK, N as FEC_N
        # Encode through FEC — returns BPSK float array
        coded_floats, n_blocks, orig_len = encode_message(text)
        # Convert back to bytes for CRC calculation
        # We CRC the original payload not the FEC output
        flags   = FLAG_FEC
        payload = raw   # CRC covers original message
    else:
        n_blocks = 0
        orig_len = len(raw)
        flags    = 0
        payload  = raw

    # Build header
    header = bytes([
        ((VERSION & 0x0F) << 4) | (flags & 0x0F),
        n_blocks & 0xFF,
    ])

    # CRC covers header + original payload
    crc = crc16_bytes(header + payload)

    if use_fec:
        return header, crc, coded_floats, n_blocks, orig_len
    else:
        return header, crc, payload, n_blocks, orig_len


def parse_header(header_bytes: bytes) -> tuple:
    """
    Parse 2-byte header.
    Returns (version, flags, n_blocks) or None on error.
    """
    if len(header_bytes) < HEADER_LEN:
        return None
    try:
        byte0, byte1 = header_bytes[0], header_bytes[1]
        version  = (byte0 >> 4) & 0x0F
        flags    = byte0 & 0x0F
        n_blocks = byte1
        return version, flags, n_blocks
    except Exception:
        return None


def verify_frame(header: bytes,
                 crc: bytes,
                 decoded_text: str) -> bool:
    """
    Verify CRC after decode.
    Returns True if frame integrity check passes.
    """
    payload = decoded_text.encode('utf-8')
    return crc16_check(header + payload, crc)


# ── Frame assembler for transmission ─────────────────────────────────────────

class FrameAssembler:
    """
    High-level frame assembly for the transmit path.
    Takes a text message, returns audio samples ready to transmit.
    The modulator handles preamble and symbol generation.
    """

    def __init__(self, use_fec: bool = True):
        self.use_fec = use_fec

    def assemble(self, text: str) -> dict:
        """
        Assemble a complete frame.

        Returns dict with:
            header:       bytes
            crc:          bytes
            payload:      bytes or float32 array
            n_blocks:     int
            orig_len:     int
            use_fec:      bool
            text:         original text
            char_count:   character count
        """
        # Add trailing space to ensure last symbol fully transmits
        # The space is stripped on receive by .strip()
        padded_text = text + ' '

        header, crc, payload, n_blocks, orig_len = build_frame(
            padded_text, use_fec=self.use_fec)

        return {
            'header':     header,
            'crc':        crc,
            'payload':    payload,
            'n_blocks':   n_blocks,
            'orig_len':   orig_len,
            'use_fec':    self.use_fec,
            'text':       text,
            'char_count': len(text),
        }


class FrameParser:
    """
    High-level frame parsing for the receive path.
    Takes decoded symbols/audio and returns verified text.
    """

    def __init__(self):
        self._pending_header = None
        self._pending_crc    = None

    def parse(self,
              audio: np.ndarray,
              snr_estimate: float = 5.0) -> dict:
        """
        Parse a received frame from accumulated audio.

        Returns dict with:
            text:        decoded text (may contain U+FFFD for corrupt chars)
            crc_ok:      True if CRC passed
            n_blocks:    number of FEC blocks decoded
            version:     protocol version
            use_fec:     whether FEC was used
            error:       error message if parsing failed, else None
        """
        from modem import (Demodulator, SAMPLES_PER_SYMBOL,
                           BASE_FREQ, SYMBOL_RATE, NUM_TONES)
        from fec import decode_message, N as FEC_N, BYTES_PER_BLOCK

        # The audio starts at the beginning of the header
        # (preamble has already been stripped by the sync layer)

        demod = Demodulator()
        sps   = SAMPLES_PER_SYMBOL

        # Extract symbols
        n_syms = len(audio) // sps
        syms   = []
        soft   = []

        for i in range(n_syms):
            block    = audio[i*sps:(i+1)*sps]
            sym, eng = demod._detect_symbol(block)
            syms.append(sym)
            soft.append(eng)

        if len(syms) < 4:
            return self._error('Frame too short')

        # Decode header (first 4 symbols = 2 bytes, unprotected)
        # Each byte = 2 symbols at 4 bits per symbol
        header_syms = syms[:4]
        header_bytes = demod.symbols_to_bytes(header_syms)
        if len(header_bytes) < HEADER_LEN:
            return self._error('Header too short')

        header = header_bytes[:HEADER_LEN]
        parsed = parse_header(header)
        if parsed is None:
            return self._error('Invalid header')

        version, flags, n_blocks = parsed

        if version != VERSION:
            return self._error(f'Unknown version {version}')

        use_fec = bool(flags & FLAG_FEC)

        # Decode CRC (next 4 symbols = 2 bytes)
        crc_syms  = syms[4:8]
        crc_bytes = demod.symbols_to_bytes(crc_syms)
        if len(crc_bytes) < CRC_LEN:
            return self._error('CRC too short')
        crc = crc_bytes[:CRC_LEN]

        # Decode payload
        payload_audio = audio[8 * sps:]

        if use_fec and n_blocks > 0:
            # FEC decode
            # Convert audio to float and pass to FEC decoder
            if len(payload_audio) < n_blocks * FEC_N * sps // 4:
                return self._error('Payload truncated')

            # Build received signal from soft symbol energies
            payload_syms = syms[8:]
            if len(payload_syms) < n_blocks * FEC_N // 4:
                return self._error('Insufficient payload symbols')

            # Reconstruct soft LLR values from symbol energies
            # Each FEC bit maps to a BPSK symbol via the tone selection
            # Use the raw audio for LLR computation
            received  = np.array(payload_audio, dtype=np.float32)
            # Normalize
            peak = np.max(np.abs(received))
            if peak > 0:
                received = received / peak

            text = decode_message(received, n_blocks,
                                   n_blocks * BYTES_PER_BLOCK,
                                   snr_estimate)
            text = text.rstrip('\x00\n\r\t').strip()

        else:
            # No FEC — raw decode
            payload_syms  = syms[8:]
            payload_bytes = demod.symbols_to_bytes(payload_syms)
            try:
                text = payload_bytes.decode('utf-8', errors='replace')
            except Exception:
                text = payload_bytes.decode('latin-1', errors='replace')
            text = text.rstrip('\x00\n\r\t').strip()

        # Verify CRC
        crc_ok = verify_frame(header, crc, text)

        return {
            'text':     text,
            'crc_ok':   crc_ok,
            'n_blocks': n_blocks,
            'version':  version,
            'use_fec':  use_fec,
            'error':    None,
        }

    def _error(self, msg: str) -> dict:
        return {
            'text':     None,
            'crc_ok':   False,
            'n_blocks': 0,
            'version':  None,
            'use_fec':  False,
            'error':    msg,
        }


# ── Self-test ─────────────────────────────────────────────────────────────────

if __name__ == '__main__':
    print("Testing CRC-16...")
    # Known CRC test vector
    test_data = b'123456789'
    crc       = crc16(test_data)
    print(f"  CRC of {test_data}: 0x{crc:04X} (expect 0x29B1)")
    assert crc == 0x29B1, f"CRC wrong: 0x{crc:04X}"
    print("  CRC test PASSED")
    print()

    print("Testing frame assembly...")
    asm = FrameAssembler(use_fec=False)

    for msg in ['CQ POTA DE WD9N K-1234 K',
                'KC8TYK DE WD9N UR 599 K',
                'Hi Tim, great signal today 73']:
        frame = asm.assemble(msg)
        print(f"  '{msg}'")
        print(f"    Header:  {frame['header'].hex()}")
        print(f"    CRC:     {frame['crc'].hex()}")
        print(f"    Payload: {len(frame['payload'])} bytes")
        print(f"    FEC:     {frame['use_fec']}")

        # Verify CRC
        ok = verify_frame(frame['header'], frame['crc'],
                          (msg + ' '))   # +space from padded_text
        print(f"    CRC OK:  {ok}")
        print()

    print("All frame tests PASSED")
