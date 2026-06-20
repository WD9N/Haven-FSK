"""
HAVEN-FSK Forward Error Correction
=====================================
LDPC(192,96) code designed specifically for HAVEN-FSK.

Specification (publishable):
    Code:         LDPC(192, 96)
    Rate:         1/2
    Construction: Progressive Edge Growth (PEG)
    Seed:         1234
    Variable deg: 3  (each bit participates in 3 parity checks)
    Check deg:    6  (each parity check covers 6 coded bits)
    Payload:      96 bits = 12 bytes per block
    Parity:       96 bits per block
    Decoder:      Belief Propagation, min-sum algorithm
    Max iter:     200

Performance (AWGN simulation):
    Threshold:    ~+1 to +3 dB SNR (hard decision)
    Real HF est:  ~-2 to -5 dB (soft decision + interleaving)
    Improvement:  ~3-7 dB over uncoded PSK31

This module is self-contained. The H matrix is fixed and embedded
so every installation uses the identical codec — required for
interoperability and FCC Part 97 disclosure compliance.

The matrix is published as part of the HAVEN-FSK mode specification
at [URL TBD] so that anyone can implement a compatible decoder.
"""

import numpy as np
from ldpc import BpDecoder

# ── Fixed mode parameters ─────────────────────────────────────────────────────

N              = 192    # coded bits per block
K              = 96     # payload bits per block  
M              = 96     # parity bits per block
BYTES_PER_BLOCK = K // 8  # 12 bytes per block
RATE           = K / N  # 0.500

# ── Build the fixed H matrix using PEG ───────────────────────────────────────
# Seed 1234 with these parameters always produces the same matrix.
# This determinism is what makes interoperability possible.

def _build_peg_matrix(n, k, d_v=3, d_c=6, seed=1234):
    """
    Progressive Edge Growth LDPC matrix construction.
    Deterministic — same seed always produces same matrix.
    """
    np.random.seed(seed)
    m    = n - k
    H    = np.zeros((m, n), dtype=np.uint8)
    cdeg = np.zeros(m, dtype=int)

    def _reachable(H, j, depth):
        vc, vv = set(), {j}
        fr     = set(np.where(H[:, j] == 1)[0])
        vc.update(fr)
        for _ in range(depth):
            nv = set()
            for c in fr:
                nv.update(v for v in np.where(H[c, :] == 1)[0]
                          if v not in vv)
            vv.update(nv)
            nc = set()
            for v in nv:
                nc.update(c for c in np.where(H[:, v] == 1)[0]
                          if c not in vc)
            if not nc:
                break
            fr = nc
            vc.update(nc)
        return vc

    for j in range(n):
        for edge in range(d_v):
            avail = np.where(cdeg < d_c)[0]
            if edge == 0:
                mn     = cdeg[avail].min()
                chosen = np.random.choice(avail[cdeg[avail] == mn])
            else:
                reach  = _reachable(H, j, edge * 2 + 1)
                pool   = [c for c in avail if c not in reach] or list(avail)
                mn     = min(cdeg[c] for c in pool)
                chosen = np.random.choice(
                    [c for c in pool if cdeg[c] == mn])
            H[chosen, j] = 1
            cdeg[chosen] += 1
    return H


def _build_encoder(H, K):
    """
    Build systematic encoder via Gaussian elimination over GF(2).
    Returns (encoding_rows, col_perm, K_sys).
    encoding_rows: M x K matrix, parity = encoding_rows * msg mod 2
    col_perm: column permutation mapping systematic to original order
    """
    M_mat, N_mat = H.shape
    Hw           = H.astype(np.int32).copy()
    pivot_cols   = []
    row          = 0

    for col in range(N_mat):
        if row >= M_mat:
            break
        found = next((r for r in range(row, M_mat) if Hw[r, col] == 1), -1)
        if found == -1:
            continue
        Hw[[row, found]] = Hw[[found, row]]
        for r in range(M_mat):
            if r != row and Hw[r, col] == 1:
                Hw[r] = (Hw[r] + Hw[row]) % 2
        pivot_cols.append(col)
        row += 1

    free_cols    = [c for c in range(N_mat) if c not in pivot_cols]
    col_perm     = free_cols + pivot_cols
    K_sys        = len(free_cols)
    encoding_rows = Hw[:, free_cols]

    return encoding_rows, col_perm, K_sys


# ── One-time initialisation (runs at import) ──────────────────────────────────
# The matrix is deterministic (fixed seed) so we cache it to disk on first run.
# Subsequent launches load the cache in milliseconds instead of rebuilding.

import os as _os

_CACHE_FILE = _os.path.join(
    _os.path.dirname(_os.path.abspath(__file__)),
    'ldpc_matrix_cache.npz'
)

def _load_or_build():
    """Load cached matrix or build and cache it."""
    if _os.path.exists(_CACHE_FILE):
        try:
            data      = np.load(_CACHE_FILE)
            H         = data['H']
            enc_rows  = data['enc_rows']
            col_perm  = data['col_perm']
            # Quick sanity check
            assert H.shape == (M, N), "Cache shape mismatch"
            print("  [FEC] LDPC(192,96) matrix loaded from cache")
            return H, enc_rows, col_perm
        except Exception:
            # Cache corrupted — rebuild
            pass

    print("  [FEC] Building LDPC(192,96) matrix (first run, please wait)...",
          end='', flush=True)
    H                       = _build_peg_matrix(N, K, seed=1234)
    enc_rows, col_perm, _   = _build_encoder(H, K)
    # Save cache
    try:
        np.savez_compressed(_CACHE_FILE,
                            H=H,
                            enc_rows=enc_rows,
                            col_perm=np.array(col_perm))
    except Exception:
        pass  # Cache save failure is non-fatal
    print(" done")
    return H, enc_rows, col_perm

_H, _enc_rows, _col_perm_arr = _load_or_build()
_col_perm     = list(_col_perm_arr)
_col_perm_inv = np.argsort(_col_perm_arr)
_bpd          = BpDecoder(_H, error_rate=0.05, max_iter=200,
                           bp_method='min_sum', ms_scaling_factor=0.75)

# ── Public API ────────────────────────────────────────────────────────────────

def encode_block(msg_bits: np.ndarray) -> np.ndarray:
    """
    Encode K=96 message bits into N=192 coded bits.
    Returns BPSK-modulated float array: 0->+1.0, 1->-1.0
    """
    assert len(msg_bits) == K, f"Expected {K} bits, got {len(msg_bits)}"
    parity  = _enc_rows.dot(msg_bits.astype(np.int32)) % 2
    cw_perm = np.concatenate([msg_bits, parity]).astype(np.uint8)
    cw      = np.zeros(N, dtype=np.uint8)
    for new_idx, orig_idx in enumerate(_col_perm):
        cw[orig_idx] = cw_perm[new_idx]
    # Return as BPSK: 0 -> +1.0, 1 -> -1.0
    return 1.0 - 2.0 * cw.astype(np.float32)


def decode_block(llr: np.ndarray) -> np.ndarray:
    """
    Decode N=192 soft LLR values back to K=96 message bits.
    llr: float array, positive = likely 0, negative = likely 1
    Returns uint8 array of K decoded bits.
    """
    assert len(llr) == N, f"Expected {N} LLR values, got {len(llr)}"
    hard     = (llr < 0).astype(np.uint8)
    err_prob = np.clip(1.0 / (1.0 + np.exp(2.0 * np.abs(llr) + 1e-10)),
                       1e-6, 1-1e-6)
    _bpd.update_channel_probs(err_prob)
    decoded      = _bpd.decode(hard)
    decoded_perm = decoded[_col_perm]
    return decoded_perm[:K].astype(np.uint8)


def encode_message(text: str) -> tuple:
    """
    Encode a text string into a list of BPSK-modulated blocks.

    Returns:
        coded:    numpy float32 array (concatenated BPSK blocks)
        n_blocks: number of LDPC blocks
        orig_len: original byte length (for decode)
    """
    raw     = text.encode('utf-8')
    n_blks  = (len(raw) + BYTES_PER_BLOCK - 1) // BYTES_PER_BLOCK
    coded   = []
    for i in range(n_blks):
        chunk = raw[i*BYTES_PER_BLOCK : (i+1)*BYTES_PER_BLOCK]
        chunk = chunk.ljust(BYTES_PER_BLOCK, b'\x00')
        bits  = np.unpackbits(np.frombuffer(chunk, dtype=np.uint8))
        coded.append(encode_block(bits))
    return np.concatenate(coded).astype(np.float32), n_blks, len(raw)


def decode_message(received: np.ndarray,
                   n_blocks: int,
                   orig_len: int,
                   snr_db: float = 5.0) -> str:
    """
    Decode a received signal back to text.

    received: float array of received samples (after matched filter)
    n_blocks: number of LDPC blocks expected
    orig_len: original message byte length
    snr_db:   estimated channel SNR for LLR scaling

    Returns decoded text string.
    """
    sigma  = 10 ** (-snr_db / 20.0)
    result = bytearray()
    for i in range(n_blocks):
        block = received[i*N : (i+1)*N].astype(np.float64)
        llr   = 2.0 * block / (sigma**2 + 1e-10)
        bits  = decode_block(llr)
        result.extend(np.packbits(bits).tobytes())
    try:
        return result[:orig_len].decode('utf-8', errors='replace')
    except Exception:
        return result[:orig_len].decode('latin-1', errors='replace')


def interleave(bits: np.ndarray, seed: int = 42) -> np.ndarray:
    """
    Interleave bits across a block to convert burst errors to scattered.
    Uses a fixed permutation (same seed = same permutation = decodable).
    """
    rng  = np.random.default_rng(seed)
    perm = rng.permutation(len(bits))
    return bits[perm], perm


def deinterleave(bits: np.ndarray, perm: np.ndarray) -> np.ndarray:
    """Reverse the interleaving permutation."""
    result           = np.zeros_like(bits)
    result[perm]     = bits
    return result


# ── Self-test ─────────────────────────────────────────────────────────────────

def _self_test():
    """Quick sanity check — called at import in debug mode."""
    test_msgs = [
        'CQ POTA DE WD9N K-1234 K',
        'KC8TYK DE WD9N UR 599 IN US-1017 K',
    ]
    for msg in test_msgs:
        coded, nb, ol = encode_message(msg)
        # Test at good SNR (should always pass)
        noise    = np.random.normal(0, 10**(-10/20), len(coded)).astype(np.float32)
        received = coded + noise
        decoded  = decode_message(received, nb, ol, snr_db=10)
        assert decoded == msg, f"Self-test failed: {repr(decoded)} != {repr(msg)}"
    return True


if __name__ == '__main__':
    print("Running FEC self-test...")
    _self_test()
    print("Self-test PASSED")
    print()
    print(f"LDPC({N},{K}) specification:")
    print(f"  Rate:           {RATE:.3f}")
    print(f"  Bytes/block:    {BYTES_PER_BLOCK}")
    print(f"  Variable degree: 3")
    print(f"  Check degree:    6")
    print(f"  Construction:   PEG seed=1234")
    print()

    # Performance curve
    import time
    msg   = 'CQ POTA DE WD9N K-1234 K'
    coded, nb, ol = encode_message(msg)
    print(f"SNR performance ({msg}):")
    for snr in [10, 7, 5, 3, 1, 0, -2]:
        hits = 0
        t0   = time.perf_counter()
        for _ in range(10):
            noise = np.random.normal(0, 10**(-snr/20), len(coded)).astype(np.float32)
            if decode_message(coded+noise, nb, ol, snr) == msg:
                hits += 1
        ms = (time.perf_counter()-t0)*1000/10
        print(f"  SNR {snr:+3d}dB: {hits*10:3d}% success  {ms:.1f}ms/decode")
