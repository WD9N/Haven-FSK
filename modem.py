"""
HAVEN-FSK Modem — modulator and demodulator
=============================================
16-tone MFSK with orthogonal tone spacing.
Tones spaced at exactly the symbol rate so they are mathematically
orthogonal — they don't interfere with each other even though they
are close together.

Parameters (all tunable at the top of this file):
    SAMPLE_RATE   : audio sample rate in Hz
    NUM_TONES     : number of MFSK tones (power of 2)
    SYMBOL_RATE   : symbols per second = tone spacing in Hz
    BASE_FREQ     : lowest tone frequency in Hz
    BITS_PER_SYM  : log2(NUM_TONES) bits carried per symbol

Occupied bandwidth = NUM_TONES * SYMBOL_RATE
With defaults: 16 * 31.25 = 500 Hz — fits comfortably inside
any SSB passband with room to spare.

The modulator takes raw bytes and produces a numpy array of
audio samples. The demodulator takes audio samples and produces
soft symbol probabilities for the FEC layer.

Usage:
    mod = Modulator()
    samples = mod.modulate(b"hello world")

    demod = Demodulator()
    symbols = demod.demodulate(samples)
"""

import numpy as np
from scipy.signal import butter, sosfilt
from scipy.io import wavfile

# ── Modem parameters ──────────────────────────────────────────────────────────
# SAMPLE_RATE is the audio sample rate used for tone generation and detection.
# 48000 Hz is the default — matches most modern radios with USB audio (Kenwood,
# Icom, Yaesu, Xiegu) and SDR virtual audio cables (Thetis, SmartSDR) natively,
# eliminating resampling overhead and artifacts.
#
# Operators on slow hardware or old radios can reduce this in Settings.
# Valid options: 48000, 44100, 22050, 16000, 8000
# All produce identical decode results — lower rates reduce waterfall bandwidth
# and CPU load but require resampling on modern audio interfaces.

SAMPLE_RATE   = 48000     # Hz — matches modern USB radio audio interfaces
NUM_TONES     = 16        # tones — gives 4 bits per symbol (log2(16) = 4)
SYMBOL_RATE   = 31.25     # baud — tone spacing equals symbol rate for orthogonality
BASE_FREQ     = 500.0     # Hz — lowest tone, leaves room below for filtering
BITS_PER_SYM  = 4         # log2(NUM_TONES)

# Derived parameters — recalculated whenever SAMPLE_RATE changes
SAMPLES_PER_SYMBOL = int(SAMPLE_RATE / SYMBOL_RATE)   # samples per symbol
TONE_FREQS = [BASE_FREQ + i * SYMBOL_RATE for i in range(NUM_TONES)]

# ── Raised cosine shaping ─────────────────────────────────────────────────────
# Applied at start and end of each symbol to eliminate key clicks.
# Key clicks are sharp transitions that splatter energy across the band.
# The raised cosine ramp makes each tone fade in and out smoothly.
# Ramp length is 10% of symbol duration.

RAMP_SAMPLES = max(4, SAMPLES_PER_SYMBOL // 10)

def _make_ramp():
    """Half raised cosine ramp, length RAMP_SAMPLES."""
    t = np.linspace(0, np.pi / 2, RAMP_SAMPLES)
    return np.sin(t)

RAMP_UP   = _make_ramp()
RAMP_DOWN = RAMP_UP[::-1]

def _shape_symbol(samples):
    """Apply raised cosine shaping to one symbol's worth of samples."""
    shaped = samples.copy()
    shaped[:RAMP_SAMPLES]  *= RAMP_UP
    shaped[-RAMP_SAMPLES:] *= RAMP_DOWN
    return shaped

# ── Modulator ─────────────────────────────────────────────────────────────────

class Modulator:
    """
    Converts bytes to MFSK audio samples.

    Each byte is split into two 4-bit nibbles (with 16 tones).
    Each nibble selects one of the 16 tones for one symbol period.
    The tones are generated as pure sine waves with raised cosine shaping.

    The output is a numpy float32 array normalized to peak amplitude 0.25
    (-12 dBFS), leaving headroom before ALC triggers on most radios.
    Final output level is controlled by the TX gain slider in the UI.
    so it does not clip when written to a wav file or sent to a sound card.
    """

    def __init__(self,
                 sample_rate=SAMPLE_RATE,
                 num_tones=NUM_TONES,
                 symbol_rate=SYMBOL_RATE,
                 base_freq=BASE_FREQ):
        self.sample_rate = sample_rate
        self.num_tones   = num_tones
        self.symbol_rate = symbol_rate
        self.base_freq   = base_freq
        self.bits_per_sym = int(np.log2(num_tones))
        self.sps         = int(sample_rate / symbol_rate)
        self.tone_freqs  = [base_freq + i * symbol_rate for i in range(num_tones)]

        # Pre-build one full cycle table for each tone — faster than
        # computing sine for every symbol at runtime
        t = np.arange(self.sps) / sample_rate
        self._tone_table = np.array([
            np.sin(2 * np.pi * f * t).astype(np.float32)
            for f in self.tone_freqs
        ])

    def bytes_to_symbols(self, data: bytes) -> list:
        """
        Convert bytes to a list of symbol indices.
        Each byte produces (8 / bits_per_sym) symbols.
        With 4 bits per symbol: each byte → 2 symbols.
        """
        symbols = []
        for byte in data:
            # Split byte into bits_per_sym-sized chunks, MSB first
            for shift in range(8 - self.bits_per_sym, -1, -self.bits_per_sym):
                mask = (1 << self.bits_per_sym) - 1
                sym  = (byte >> shift) & mask
                symbols.append(sym)
        return symbols

    def modulate(self, data: bytes) -> np.ndarray:
        """
        Convert bytes to audio samples.
        Returns float32 numpy array at self.sample_rate.
        """
        symbols = self.bytes_to_symbols(data)
        chunks  = []

        for sym in symbols:
            raw    = self._tone_table[sym].copy()
            shaped = _shape_symbol(raw)
            chunks.append(shaped)

        if not chunks:
            return np.array([], dtype=np.float32)

        audio = np.concatenate(chunks)

        # Normalize to 0.25 peak amplitude (-12 dBFS)
        # This leaves headroom before ALC triggers on most radios.
        # The operator adjusts final level with the TX gain slider.
        # At 0.9 peak (-0.9 dBFS) ALC pumping and IMD are likely.
        peak = np.max(np.abs(audio))
        if peak > 0:
            audio = audio * (0.25 / peak)

        return audio

    def modulate_text(self, text: str) -> np.ndarray:
        """Convenience wrapper — encode UTF-8 text to audio."""
        return self.modulate(text.encode('utf-8'))

    def write_wav(self, filename: str, data: bytes):
        """Write modulated audio to a WAV file for testing."""
        audio   = self.modulate(data)
        int16   = (audio * 32767).astype(np.int16)
        wavfile.write(filename, self.sample_rate, int16)
        duration = len(audio) / self.sample_rate
        bps      = (len(data) * 8) / duration if duration > 0 else 0
        print(f"Written: {filename}")
        print(f"  Data:     {len(data)} bytes ({len(data)*8} bits)")
        print(f"  Symbols:  {len(data) * (8 // self.bits_per_sym)}")
        print(f"  Duration: {duration:.2f} seconds")
        print(f"  Raw rate: {bps:.1f} bits/second")
        print(f"  Tones:    {self.num_tones} ({self.bits_per_sym} bits/symbol)")
        print(f"  Spacing:  {self.symbol_rate} Hz")
        print(f"  BW:       {self.num_tones * self.symbol_rate:.0f} Hz")

# ── Demodulator ───────────────────────────────────────────────────────────────

class Demodulator:
    """
    Converts MFSK audio samples back to bytes.

    Uses non-coherent detection — measures the energy at each tone
    frequency using the FFT and picks the tone with the most energy.
    Non-coherent means we don't care about phase — only power.
    This makes the demodulator robust against HF phase shifts and
    Doppler-induced frequency drift.

    Produces both hard decisions (byte output) and soft decisions
    (energy values per tone) for the FEC layer to use.
    """

    def __init__(self,
                 sample_rate=SAMPLE_RATE,
                 num_tones=NUM_TONES,
                 symbol_rate=SYMBOL_RATE,
                 base_freq=BASE_FREQ):
        self.sample_rate  = sample_rate
        self.num_tones    = num_tones
        self.symbol_rate  = symbol_rate
        self.base_freq    = base_freq
        self.bits_per_sym = int(np.log2(num_tones))
        self.sps          = int(sample_rate / symbol_rate)
        self.tone_freqs   = [base_freq + i * symbol_rate for i in range(num_tones)]

        # Pre-compute the FFT bin index for each tone
        # FFT bin resolution = sample_rate / sps = symbol_rate
        # So each tone lands exactly on one FFT bin — this is the
        # orthogonality property. Tone spacing = symbol rate = bin spacing.
        self._tone_bins = [
            int(round(f / symbol_rate)) for f in self.tone_freqs
        ]

    def _detect_symbol(self, block: np.ndarray) -> tuple:
        """
        Detect one symbol from one block of samples.
        Returns (symbol_index, energy_array).
        energy_array contains the power at each tone frequency —
        these are the soft values the FEC layer uses.
        """
        # Apply a Hann window to reduce spectral leakage
        # Leakage is energy from one bin spilling into adjacent bins
        # The window reduces this at the cost of slight frequency resolution
        windowed = block * np.hanning(len(block))

        # FFT — converts time domain samples to frequency domain energy
        spectrum = np.abs(np.fft.rfft(windowed, n=self.sps)) ** 2

        # Extract energy at each tone's bin
        energies = np.array([spectrum[b] for b in self._tone_bins],
                            dtype=np.float32)

        # Hard decision — pick the tone with most energy
        symbol = int(np.argmax(energies))

        return symbol, energies

    def symbols_to_bytes(self, symbols: list) -> bytes:
        """
        Convert symbol indices back to bytes.
        Inverse of Modulator.bytes_to_symbols().
        """
        bits_per_sym = self.bits_per_sym
        syms_per_byte = 8 // bits_per_sym
        output = []

        for i in range(0, len(symbols) - (syms_per_byte - 1), syms_per_byte):
            byte = 0
            for j in range(syms_per_byte):
                byte = (byte << bits_per_sym) | symbols[i + j]
            output.append(byte)

        return bytes(output)

    def demodulate(self, audio: np.ndarray) -> bytes:
        """
        Convert audio samples to bytes.
        Simple hard-decision demodulation without FEC.
        """
        symbols    = []
        num_blocks = len(audio) // self.sps

        for i in range(num_blocks):
            block  = audio[i * self.sps : (i + 1) * self.sps]
            sym, _ = self._detect_symbol(block)
            symbols.append(sym)

        return self.symbols_to_bytes(symbols)

    def demodulate_soft(self, audio: np.ndarray) -> np.ndarray:
        """
        Convert audio samples to soft symbol energies.
        Returns array of shape (num_symbols, num_tones).
        The FEC layer uses these energy values for soft decision decoding —
        it's more reliable than hard decisions because it preserves
        the confidence level of each symbol detection.
        """
        num_blocks = len(audio) // self.sps
        soft       = np.zeros((num_blocks, self.num_tones), dtype=np.float32)

        for i in range(num_blocks):
            block       = audio[i * self.sps : (i + 1) * self.sps]
            _, energies = self._detect_symbol(block)
            soft[i]     = energies

        return soft

    def demodulate_text(self, audio: np.ndarray) -> str:
        """Convenience wrapper — demodulate and decode as UTF-8 text."""
        raw = self.demodulate(audio)
        try:
            return raw.decode('utf-8', errors='replace')
        except Exception:
            return raw.decode('latin-1', errors='replace')

# ── DCD — Data Carrier Detect ─────────────────────────────────────────────────

class DCD:
    """
    Software Data Carrier Detect.

    Monitors incoming audio and reports whether a valid MFSK signal
    is present on the channel. Used by the transmitter to implement
    listen-before-talk collision avoidance.

    The detection works by measuring total energy in the MFSK passband
    and comparing it to the noise floor outside the passband.
    A valid signal shows elevated energy specifically in the tone frequencies.

    This is the equivalent of hardware squelch in a packet TNC —
    the channel is considered busy when DCD is active, and no
    transmission should begin until DCD clears.
    """

    def __init__(self,
                 sample_rate=SAMPLE_RATE,
                 base_freq=BASE_FREQ,
                 num_tones=NUM_TONES,
                 symbol_rate=SYMBOL_RATE,
                 threshold_db=6.0):
        self.sample_rate = sample_rate
        self.base_freq   = base_freq
        self.num_tones   = num_tones
        self.symbol_rate = symbol_rate
        self.threshold   = threshold_db  # dB above noise floor to assert DCD

        # Signal band: frequencies where our tones live
        self.sig_low  = base_freq - symbol_rate
        self.sig_high = base_freq + num_tones * symbol_rate + symbol_rate

        # Noise reference band: just below our signal (clean HF noise)
        self.noise_low  = max(100, self.sig_low - 300)
        self.noise_high = self.sig_low - symbol_rate

        self._active = False

    def update(self, audio_block: np.ndarray) -> bool:
        """
        Feed a block of audio samples and get DCD state.
        Returns True if carrier detected (channel busy).
        Returns False if channel appears clear.
        """
        n       = len(audio_block)
        freqs   = np.fft.rfftfreq(n, d=1.0 / self.sample_rate)
        spectrum = np.abs(np.fft.rfft(audio_block)) ** 2

        # Energy in signal band
        sig_mask   = (freqs >= self.sig_low) & (freqs <= self.sig_high)
        sig_energy = np.mean(spectrum[sig_mask]) if sig_mask.any() else 0

        # Energy in noise reference band
        noise_mask   = (freqs >= self.noise_low) & (freqs <= self.noise_high)
        noise_energy = np.mean(spectrum[noise_mask]) if noise_mask.any() else 1e-10

        # Convert to dB above noise floor
        if noise_energy > 0 and sig_energy > 0:
            db_above = 10 * np.log10(sig_energy / noise_energy)
        else:
            db_above = 0

        self._active = db_above >= self.threshold
        return self._active

    @property
    def active(self) -> bool:
        """True if channel is currently busy."""
        return self._active

# ── TX delay — randomized backoff ────────────────────────────────────────────

import time
import random

def tx_delay(min_ms: float = 200, max_ms: float = 2000) -> float:
    """
    Return a random delay in seconds for transmit backoff.
    Called after DCD clears before beginning transmission.

    min_ms: minimum delay in milliseconds
    max_ms: maximum delay in milliseconds

    The randomization ensures that when multiple stations are all
    waiting for the channel to clear, they don't all transmit
    simultaneously the moment DCD drops.
    """
    delay = random.uniform(min_ms, max_ms) / 1000.0
    return delay

def wait_for_clear_then_delay(dcd: DCD,
                               audio_source,
                               min_ms: float = 200,
                               max_ms: float = 2000,
                               timeout: float = 30.0) -> bool:
    """
    Block until DCD clears, then apply random TX delay.
    Returns True if channel cleared within timeout.
    Returns False if timeout expired (channel never cleared).

    In real operation audio_source would be the sound card callback.
    In testing it can be any callable returning audio blocks.
    """
    start = time.monotonic()
    while time.monotonic() - start < timeout:
        block = audio_source()
        if not dcd.update(block):
            # Channel just cleared — apply random backoff
            delay = tx_delay(min_ms, max_ms)
            time.sleep(delay)
            # Check one more time after delay — someone else may have
            # grabbed the channel during our backoff window
            block = audio_source()
            if not dcd.update(block):
                return True  # Channel still clear — safe to transmit
            # Someone else got there first — go back to waiting
    return False  # Timeout

