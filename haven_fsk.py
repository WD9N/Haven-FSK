#!/usr/bin/env python3
"""
HAVEN-FSK v0.1.0-alpha — Digital HF Chat Mode
==========================================
16-tone MFSK with LDPC(192,96) FEC, CRC-16, frame sync, DCD collision avoidance.

Requirements:
    pip install numpy scipy sounddevice matplotlib
    Linux: sudo apt install python3-tk libportaudio2

Usage:
    python3 haven_fsk.py

Commands in the message box:
    /cls or /clear    Clear the chat window
    /help             Show available commands
"""

import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox, filedialog
import json
import threading
import queue
import time
import random
import numpy as np
from scipy.io import wavfile
import sys
import os

HERE        = os.path.dirname(os.path.abspath(__file__))
CONFIG_FILE = os.path.join(HERE, "haven_fsk_config.json")
sys.path.insert(0, HERE)

# ── Load sample rate from config BEFORE importing modem ───────────────────────
# SAMPLE_RATE must be set in modem before any objects are created.
# We read it from config here so the correct value is used from startup.
def _get_configured_sample_rate():
    try:
        if os.path.exists(CONFIG_FILE):
            with open(CONFIG_FILE) as f:
                cfg = json.load(f)
            sr = int(cfg.get('sample_rate', 48000))
            if sr in (48000, 44100, 22050, 16000, 8000):
                return sr
    except Exception:
        pass
    return 48000

import modem as _modem_module
_configured_sr = _get_configured_sample_rate()
if _configured_sr != _modem_module.SAMPLE_RATE:
    _modem_module.SAMPLE_RATE         = _configured_sr
    _modem_module.SAMPLES_PER_SYMBOL  = int(_configured_sr / _modem_module.SYMBOL_RATE)
    _modem_module.RAMP_SAMPLES        = max(4, _modem_module.SAMPLES_PER_SYMBOL // 10)

from modem import (
    Modulator, Demodulator, DCD,
    SAMPLE_RATE, NUM_TONES, SYMBOL_RATE, BASE_FREQ,
    SAMPLES_PER_SYMBOL, TONE_FREQS, tx_delay
)

# FEC and frame modules — imported lazily to avoid slow startup blocking UI
try:
    from fec import encode_message as fec_encode, decode_message as fec_decode
    from fec import BYTES_PER_BLOCK, N as FEC_N
    from frame import crc16_bytes, crc16_check, build_frame
    FEC_AVAILABLE = True
except Exception as _fec_err:
    FEC_AVAILABLE = False
    print(f"FEC not available: {_fec_err}")

try:
    import sounddevice as sd
    AUDIO_AVAILABLE = True
except Exception:
    AUDIO_AVAILABLE = False

try:
    import matplotlib
    matplotlib.use('TkAgg')
    from matplotlib.figure import Figure
    from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
    MATPLOTLIB_AVAILABLE = True
except Exception:
    MATPLOTLIB_AVAILABLE = False

# ── Frame sync parameters ─────────────────────────────────────────────────────
# Preamble: alternating low/high tones in a distinctive pattern
# The receiver scans for this pattern to find the start of each frame.
# Using tones 0, 15, 7, 8 gives maximum frequency separation.
SYNC_SYMS    = [0,15,0,15, 7,8,7,8, 0,15,0,15, 7,8,7,8]   # 16 symbols
END_SYMS     = [0, 0, 0, 0, 0, 0, 0, 0]                      # 8 symbols — tone 0 = null byte, never in text
SYNC_THRESH  = 1.4    # minimum correlation score to accept preamble
SEARCH_LIMIT = 4      # search this many preamble-lengths from start of audio

def _build_tone_seq(syms):
    t = np.arange(SAMPLES_PER_SYMBOL) / SAMPLE_RATE
    return np.concatenate([
        np.sin(2*np.pi*(BASE_FREQ + s*SYMBOL_RATE)*t).astype(np.float32)
        for s in syms
    ])

PREAMBLE_AUDIO = _build_tone_seq(SYNC_SYMS)
END_AUDIO      = _build_tone_seq(END_SYMS)
PREAMBLE_LEN   = len(PREAMBLE_AUDIO)

def preamble_score(audio, start):
    """Correlation score of SYNC_SYMS pattern at position start."""
    score = 0.0
    for i, sym in enumerate(SYNC_SYMS):
        begin = start + i * SAMPLES_PER_SYMBOL
        end_  = begin + SAMPLES_PER_SYMBOL
        if end_ > len(audio):
            return 0.0
        block    = audio[begin:end_] * np.hanning(SAMPLES_PER_SYMBOL)
        spectrum = np.abs(np.fft.rfft(block, n=SAMPLES_PER_SYMBOL))**2
        bin_idx  = [int(round((BASE_FREQ + s*SYMBOL_RATE)/SYMBOL_RATE))
                    for s in range(NUM_TONES)]
        energies = np.array([spectrum[b] for b in bin_idx])
        expected = energies[sym]
        others   = np.sum(energies) - expected
        score   += expected / (others + 1e-10)
    return score / len(SYNC_SYMS)

def find_preamble(audio):
    """
    Scan audio for the preamble pattern.
    Returns (position, score) of best match, or (-1, 0) if not found.
    Steps by 4 samples (~0.5ms) for good timing resolution.
    """
    search_end  = min(len(audio), PREAMBLE_LEN * SEARCH_LIMIT)
    best_score  = 0.0
    best_pos    = -1
    for start in range(0, search_end - PREAMBLE_LEN, 4):
        s = preamble_score(audio, start)
        if s > best_score:
            best_score = s
            best_pos   = start
    if best_score < SYNC_THRESH:
        return -1, best_score
    return best_pos, best_score

def _detect_symbol_robust(block):
    """
    Detect MFSK symbol using zoomed FFT for sub-bin frequency resolution.
    Uses 8x zero padding so each tone bin is 3.9 Hz wide instead of 31.25 Hz.
    Sums energy over a small guard window around each tone to tolerate
    minor frequency drift from the audio path without misidentifying tones.
    """
    ZOOM    = 8
    GUARD   = 3                          # sub-bins each side of centre
    n_fft   = SAMPLES_PER_SYMBOL * ZOOM
    windowed = block * np.hanning(len(block))
    spectrum = np.abs(np.fft.rfft(windowed, n=n_fft)) ** 2
    freqs    = np.fft.rfftfreq(n_fft, 1.0 / SAMPLE_RATE)
    energies = np.zeros(NUM_TONES, dtype=np.float32)
    for tone_idx in range(NUM_TONES):
        tone_hz = BASE_FREQ + tone_idx * SYMBOL_RATE
        centre  = int(np.argmin(np.abs(freqs - tone_hz)))
        lo      = max(0, centre - GUARD)
        hi      = min(len(spectrum), centre + GUARD + 1)
        energies[tone_idx] = np.sum(spectrum[lo:hi])
    return int(np.argmax(energies)), energies


def _syms_to_text(syms):
    """
    Convert symbol list to text, truncating cleanly at the end marker.
    End marker = 2+ consecutive tone-0 symbols (null bytes, never in text).
    Using 2 instead of 3 so one corrupted end-marker symbol doesn't cause
    the whole end marker to be missed.
    """
    cut = len(syms)
    for i in range(len(syms) - 1):
        if syms[i] == 0 and syms[i+1] == 0:
            cut = i
            break
    demod = Demodulator()
    raw   = demod.symbols_to_bytes(syms[:cut])
    try:
        text = raw.decode('utf-8', errors='replace')
    except Exception:
        text = raw.decode('latin-1', errors='replace')
    text = text.rstrip('\ufffd\n\r\t').strip()
    if not text:
        return None
    # Reject if more than 20% non-printable — indicates noise decode
    printable = sum(1 for c in text if 32 <= ord(c) < 127)
    if len(text) > 4 and printable / len(text) < 0.8:
        return None
    return text


def decode_frame(audio):
    """
    Locate preamble, decode symbol stream with robust detector,
    find end marker, return clean text. Returns None if no valid frame.
    """
    pos, score = find_preamble(audio)
    if pos < 0:
        return None
    payload = audio[pos + PREAMBLE_LEN:]
    syms    = []
    for i in range(len(payload) // SAMPLES_PER_SYMBOL):
        block  = payload[i*SAMPLES_PER_SYMBOL : (i+1)*SAMPLES_PER_SYMBOL]
        sym, _ = _detect_symbol_robust(block)
        syms.append(sym)
    return _syms_to_text(syms)

# ── Constants ─────────────────────────────────────────────────────────────────
APP_NAME    = "HAVEN-FSK"
APP_VERSION = "0.1.0-alpha"
AUDIO_CHUNK = 2048
DCD_THRESH  = 12.0
TX_DELAY_MIN = 300
TX_DELAY_MAX = 1800
WATERFALL_HZ = 4000   # Nyquist limit at 8000Hz sample rate
WATERFALL_ROWS = 80

DEFAULT_MACROS = [
    ["CQ",   "CQ CQ CQ DE <CALL> <CALL> HAVEN-FSK PSE K"],
    ["QRZ?", "QRZ? DE <CALL>"],
    ["73",   "73 DE <CALL> SK"],
    ["MSG4", ""],
    ["MSG5", ""],
    ["MSG6", ""],
]

BG        = "#1a1a2e"
BG2       = "#16213e"
ACCENT    = "#0f3460"
GREEN     = "#00ff88"
AMBER     = "#ffaa00"
RED       = "#ff4444"
TEXT_FG   = "#e0e0e0"
TEXT_BG   = "#0d1117"
FONT_MONO = ("Courier", 11)
FONT_UI   = ("Helvetica", 10)

# ── Audio engine ──────────────────────────────────────────────────────────────

class AudioEngine:
    def __init__(self, in_device=None, out_device=None):
        self.in_device   = in_device
        self.out_device  = out_device
        # Separate queues: RX decode never competes with waterfall
        self.rx_queue    = queue.Queue(maxsize=400)
        self.wf_queue    = queue.Queue(maxsize=400)
        self._running    = False
        self._rx_stream  = None   # separate input stream
        self._tx_stream  = None   # separate output stream
        self._tx_buf     = np.array([], dtype=np.float32)
        self._tx_lock    = threading.Lock()
        self._rx_hw_rate = SAMPLE_RATE
        self._tx_hw_rate = SAMPLE_RATE
        self.output_gain = 1.0

    def start(self):
        if not AUDIO_AVAILABLE:
            return False, "sounddevice not available"

        self._running = True
        rx_ok, rx_msg = self._start_rx()
        tx_ok, tx_msg = self._start_tx()

        if rx_ok and tx_ok:
            msg = f"RX: {rx_msg}  TX: {tx_msg}"
            return True, msg
        elif rx_ok:
            return True, f"RX: {rx_msg}  (TX: {tx_msg})"
        else:
            self._running = False
            return False, f"RX failed: {rx_msg}"

    def _start_rx(self):
        """Open input stream, trying multiple sample rates."""
        # Query the device's native rate first and try that
        native_rate = SAMPLE_RATE
        try:
            if self.in_device is not None:
                info = sd.query_devices(self.in_device)
                native_rate = int(info['default_samplerate'])
        except Exception:
            pass

        # Try native rate first, then common rates
        rates = [native_rate] + [r for r in
                 [8000, 44100, 48000, 96000, 22050, 16000]
                 if r != native_rate]
        rates = list(dict.fromkeys(rates))  # deduplicate preserving order

        last_err = "unknown error"
        for rate in rates:
            try:
                bs = max(AUDIO_CHUNK, int(AUDIO_CHUNK * rate / SAMPLE_RATE))
                self._rx_stream = sd.InputStream(
                    samplerate = rate,
                    blocksize  = bs,
                    device     = self.in_device,
                    channels   = 1,
                    dtype      = np.float32,
                    callback   = self._rx_callback,
                )
                self._rx_stream.start()
                self._rx_hw_rate = rate
                # Record actual device index — query the stream directly
                if self.in_device is None:
                    try:
                        self.in_device = int(
                            self._rx_stream.device[0]
                            if hasattr(self._rx_stream, 'device')
                            else sd.default.device[0])
                    except Exception:
                        self.in_device = 0
                msg = f"{rate}Hz"
                if rate != SAMPLE_RATE:
                    msg += f"→{SAMPLE_RATE}Hz"
                return True, msg
            except Exception as e:
                last_err = str(e)
                if self._rx_stream:
                    try: self._rx_stream.close()
                    except Exception: pass
                    self._rx_stream = None
        return False, last_err

    def _start_tx(self):
        """Open output stream, trying multiple sample rates."""
        native_rate = SAMPLE_RATE
        try:
            if self.out_device is not None:
                info = sd.query_devices(self.out_device)
                native_rate = int(info['default_samplerate'])
        except Exception:
            pass

        rates = [native_rate] + [r for r in
                 [8000, 44100, 48000, 96000, 22050, 16000]
                 if r != native_rate]
        rates = list(dict.fromkeys(rates))

        last_err = "unknown error"
        for rate in rates:
            try:
                bs = max(AUDIO_CHUNK, int(AUDIO_CHUNK * rate / SAMPLE_RATE))
                self._tx_stream = sd.OutputStream(
                    samplerate = rate,
                    blocksize  = bs,
                    device     = self.out_device,
                    channels   = 1,
                    dtype      = np.float32,
                    callback   = self._tx_callback,
                )
                self._tx_stream.start()
                self._tx_hw_rate = rate
                # Record actual device index — query the stream directly
                if self.out_device is None:
                    try:
                        self.out_device = int(
                            self._tx_stream.device[1]
                            if hasattr(self._tx_stream, 'device')
                            else sd.default.device[1])
                    except Exception:
                        self.out_device = 0
                msg = f"{rate}Hz"
                if rate != SAMPLE_RATE:
                    msg += f"→{SAMPLE_RATE}Hz"
                return True, msg
            except Exception as e:
                last_err = str(e)
                if self._tx_stream:
                    try: self._tx_stream.close()
                    except Exception: pass
                    self._tx_stream = None
        return False, last_err

    def _resample_to_modem_rate(self, chunk, hw_rate):
        if hw_rate == SAMPLE_RATE:
            return chunk
        from scipy.signal import resample_poly
        from math import gcd
        g = gcd(SAMPLE_RATE, hw_rate)
        return resample_poly(chunk,
                             SAMPLE_RATE // g,
                             hw_rate // g).astype(np.float32)

    def _resample_to_hw_rate(self, audio, hw_rate):
        if hw_rate == SAMPLE_RATE:
            return audio
        from scipy.signal import resample_poly
        from math import gcd
        g = gcd(SAMPLE_RATE, hw_rate)
        return resample_poly(audio,
                             hw_rate // g,
                             SAMPLE_RATE // g).astype(np.float32)

    def stop(self):
        self._running = False
        for stream in (self._rx_stream, self._tx_stream):
            if stream:
                try:
                    stream.stop()
                    stream.close()
                except Exception:
                    pass
        self._rx_stream = None
        self._tx_stream = None

    def _rx_callback(self, indata, frames, time_info, status):
        """Input-only callback — receive audio from radio."""
        chunk = indata[:, 0].copy()
        chunk = self._resample_to_modem_rate(chunk, self._rx_hw_rate)
        try:
            self.rx_queue.put_nowait(chunk)
        except queue.Full:
            pass
        try:
            self.wf_queue.put_nowait(chunk)
        except queue.Full:
            pass

    def _tx_callback(self, outdata, frames, time_info, status):
        """Output-only callback — send audio to radio."""
        with self._tx_lock:
            if len(self._tx_buf) >= frames:
                outdata[:, 0] = self._tx_buf[:frames]
                self._tx_buf  = self._tx_buf[frames:]
            else:
                out = np.zeros(frames, dtype=np.float32)
                out[:len(self._tx_buf)] = self._tx_buf
                outdata[:, 0] = out
                self._tx_buf  = np.array([], dtype=np.float32)
        outdata[:, 0] *= self.output_gain

    def transmit(self, audio: np.ndarray):
        """Queue audio for transmission. Resamples to TX hardware rate."""
        resampled = self._resample_to_hw_rate(audio, self._tx_hw_rate)
        with self._tx_lock:
            self._tx_buf = np.concatenate([self._tx_buf, resampled])

    def play_wav(self, audio: np.ndarray, sample_rate: int):
        """Play audio through output — use with Stereo Mix for loopback."""
        if sample_rate != SAMPLE_RATE:
            from scipy.signal import resample_poly
            from math import gcd
            g     = gcd(SAMPLE_RATE, sample_rate)
            audio = resample_poly(audio, SAMPLE_RATE//g,
                                  sample_rate//g).astype(np.float32)
        resampled = self._resample_to_hw_rate(audio, self._tx_hw_rate)
        with self._tx_lock:
            self._tx_buf = np.concatenate([self._tx_buf, resampled])

    def inject_loopback(self, audio: np.ndarray, sample_rate: int):
        """Software loopback — inject directly into both RX queues."""
        def _feed():
            if sample_rate != SAMPLE_RATE:
                from scipy.signal import resample_poly
                from math import gcd
                g    = gcd(SAMPLE_RATE, sample_rate)
                data = resample_poly(audio, SAMPLE_RATE//g,
                                     sample_rate//g).astype(np.float32)
            else:
                data = audio.astype(np.float32)
            n = len(data) // AUDIO_CHUNK
            for i in range(n):
                chunk = data[i*AUDIO_CHUNK:(i+1)*AUDIO_CHUNK]
                for q in (self.rx_queue, self.wf_queue):
                    try:
                        q.put(chunk, timeout=1.0)
                    except queue.Full:
                        pass
                time.sleep(AUDIO_CHUNK / SAMPLE_RATE)
            tail = data[n*AUDIO_CHUNK:]
            if len(tail):
                for q in (self.rx_queue, self.wf_queue):
                    try:
                        q.put(tail, timeout=1.0)
                    except queue.Full:
                        pass
        threading.Thread(target=_feed, daemon=True).start()

    def is_transmitting(self):
        with self._tx_lock:
            return len(self._tx_buf) > 0

    def tx_duration_remaining(self):
        """Estimated seconds of audio remaining in TX buffer."""
        with self._tx_lock:
            if self._tx_hw_rate > 0:
                return len(self._tx_buf) / self._tx_hw_rate
            return 0.0

    @staticmethod
    def list_devices():
        if not AUDIO_AVAILABLE:
            return []
        return [
            {'index': i, 'name': d['name'],
             'inputs': d['max_input_channels'],
             'outputs': d['max_output_channels'],
             'rate': int(d['default_samplerate'])}
            for i, d in enumerate(sd.query_devices())
        ]

# ── Modulator wrapper with frame sync ─────────────────────────────────────────

class FrameModulator:
    """
    Assembles complete transmit frames.
    With FEC available: preamble + header + CRC + LDPC-coded payload
    Without FEC:        preamble + raw payload (fallback)
    """
    def __init__(self):
        self._mod = Modulator()

    def modulate_frame(self, text: str) -> np.ndarray:
        # Add trailing space so last symbol fully transmits
        padded = text + ' '

        if FEC_AVAILABLE:
            return self._modulate_with_fec(padded)
        else:
            return self._modulate_raw(padded)

    def _modulate_with_fec(self, text: str) -> np.ndarray:
        """Encode with LDPC FEC + CRC header."""
        try:
            # FEC encode payload
            coded_floats, n_blocks, orig_len = fec_encode(text)

            # Build header + CRC
            version = 0b0001
            flags   = 0b0001  # FEC enabled
            header  = bytes([(version << 4) | flags, n_blocks & 0xFF])
            crc     = crc16_bytes(header + text.encode('utf-8'))

            # Modulate header+CRC as raw MFSK bytes
            hdr_audio = self._mod.modulate(header + crc)

            # Convert FEC BPSK floats (+1/-1) to bits then modulate
            fec_bits  = ((1.0 - coded_floats) / 2.0 + 0.5).astype(np.uint8)
            n_bytes   = (len(fec_bits) // 8)
            fec_bytes = np.packbits(fec_bits[:n_bytes * 8]).tobytes()
            fec_audio = self._mod.modulate(fec_bytes)

            return np.concatenate([PREAMBLE_AUDIO, hdr_audio, fec_audio])
        except Exception as e:
            # Fall back to raw on any FEC error
            return self._modulate_raw(text)

    def _modulate_raw(self, text: str) -> np.ndarray:
        """Raw MFSK without FEC (fallback)."""
        payload = self._mod.modulate_text(text)
        return np.concatenate([PREAMBLE_AUDIO, payload])

    def write_wav(self, filename, text):
        audio = self.modulate_frame(text)
        int16 = (audio * 32767).astype(np.int16)
        wavfile.write(filename, SAMPLE_RATE, int16)
        dur = len(audio) / SAMPLE_RATE
        fec_str = "FEC" if FEC_AVAILABLE else "no FEC"
        print(f"WAV: {filename}  {dur:.2f}s  [{fec_str}]")

# ── Waterfall ─────────────────────────────────────────────────────────────────

class Waterfall:
    def __init__(self, parent):
        self._data     = np.full((WATERFALL_ROWS, 512), -60.0, dtype=np.float32)
        self._hz_max   = WATERFALL_HZ   # current upper frequency limit
        self._freq     = np.linspace(0, self._hz_max, 512)
        self._built    = False
        self._build(parent)

    def _build(self, parent):
        if not MATPLOTLIB_AVAILABLE:
            tk.Label(parent, text="Install matplotlib for waterfall",
                     bg=BG2, fg=AMBER, font=FONT_UI).pack(
                     fill=tk.BOTH, expand=True)
            return

        self._fig = Figure(figsize=(7, 2.6), dpi=88,
                           facecolor=BG2, tight_layout=True)
        self._ax  = self._fig.add_subplot(111)
        self._ax.set_facecolor('#000008')
        self._ax.tick_params(colors=TEXT_FG, labelsize=8)
        for spine in self._ax.spines.values():
            spine.set_color(ACCENT)

        self._im = self._ax.imshow(
            self._data, aspect='auto', origin='upper',
            cmap='inferno', vmin=-60, vmax=40,
            extent=[0, self._hz_max, WATERFALL_ROWS, 0],
            interpolation='nearest',
        )
        self._ax.set_xlabel("Hz", color=TEXT_FG, fontsize=8)
        self._ax.set_yticks([])

        # Initial 100 Hz frequency tick marks
        ticks = list(range(0, self._hz_max + 1, 100))
        self._ax.set_xticks(ticks)
        self._ax.set_xticklabels(
            [str(t) if t % 500 == 0 else '' for t in ticks],
            color=TEXT_FG, fontsize=7)
        self._ax.set_xlim(0, self._hz_max)

        # Signal band markers
        lo = BASE_FREQ
        hi = BASE_FREQ + NUM_TONES * SYMBOL_RATE
        for x, label in [(lo, f'{lo:.0f} Hz'), (hi, f'{hi:.0f} Hz')]:
            self._ax.axvline(x=x, color=GREEN, lw=0.7,
                             alpha=0.7, linestyle='--')
            self._ax.text(x+8, 4, label, color=GREEN, fontsize=7, va='top')

        self._canvas = FigureCanvasTkAgg(self._fig, master=parent)
        self._canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)

        # ── Bandwidth slider below waterfall ──────────────────────────────────
        ctrl = tk.Frame(parent, bg=BG2)
        ctrl.pack(fill=tk.X, padx=4, pady=(0,2))

        tk.Label(ctrl, text="BW:", bg=BG2, fg=TEXT_FG,
                 font=("Helvetica", 8)).pack(side=tk.LEFT, padx=(4,2))

        self._bw_var = tk.IntVar(value=self._hz_max)
        self._bw_slider = tk.Scale(
            ctrl,
            from_=2000, to=4000,
            orient=tk.HORIZONTAL,
            variable=self._bw_var,
            resolution=100,
            length=300,
            showvalue=False,
            bg=BG2, fg=GREEN,
            troughcolor=ACCENT,
            activebackground=GREEN,
            highlightthickness=0,
            command=self._on_bw_changed,
        )
        self._bw_slider.pack(side=tk.LEFT, padx=(0,4))

        self._bw_label = tk.Label(ctrl, text=f"{self._hz_max} Hz",
                                  bg=BG2, fg=GREEN,
                                  font=("Courier", 9), width=9)
        self._bw_label.pack(side=tk.LEFT, padx=(0,8))

        self._built = True

    def _on_bw_changed(self, val):
        """Bandwidth slider moved — update waterfall frequency range."""
        self._hz_max = int(val)
        self._freq   = np.linspace(0, self._hz_max, 512)
        self._bw_label.configure(text=f"{self._hz_max} Hz")

        # Update the image extent and axis limits
        self._im.set_extent([0, self._hz_max, WATERFALL_ROWS, 0])
        self._ax.set_xlim(0, self._hz_max)

        # Rebuild 100 Hz frequency tick marks
        ticks = list(range(0, self._hz_max + 1, 100))
        self._ax.set_xticks(ticks)
        self._ax.set_xticklabels(
            [str(t) if t % 500 == 0 else '' for t in ticks],
            color=TEXT_FG, fontsize=7)

        # Minor tick marks at every 100 Hz
        self._ax.tick_params(axis='x', which='both',
                             bottom=True, colors=TEXT_FG, labelsize=7)

        self._canvas.draw_idle()

    def push(self, chunk: np.ndarray):
        if not self._built:
            return
        n       = min(len(chunk), 8192)
        spec    = np.abs(np.fft.rfft(chunk[:n] * np.hanning(n)))
        freqs   = np.fft.rfftfreq(n, 1.0/SAMPLE_RATE)
        row     = np.interp(self._freq, freqs,
                            20*np.log10(spec + 1e-10))
        self._data     = np.roll(self._data, 1, axis=0)
        self._data[0]  = row
        self._im.set_data(self._data)
        self._canvas.draw_idle()

# ── LED indicator ─────────────────────────────────────────────────────────────

class LED(tk.Canvas):
    COLOURS = {
        'clear': ('#00aa55', '#00ff88'),
        'busy':  ('#aa2200', '#ff4444'),
        'tx':    ('#aa7700', '#ffaa00'),
        'idle':  ('#1a1a1a', '#333333'),
    }
    def __init__(self, parent, size=16):
        super().__init__(parent, width=size, height=size,
                         bg=BG, highlightthickness=0)
        self._oval = self.create_oval(2, 2, size-2, size-2,
                                      fill='#1a1a1a', outline='#333333')
    def set(self, state):
        fill, outline = self.COLOURS.get(state, self.COLOURS['idle'])
        self.itemconfig(self._oval, fill=fill, outline=outline)

# ── Main application ──────────────────────────────────────────────────────────

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(f"{APP_NAME}  v{APP_VERSION}  —  HAVEN-FSK  500 Hz  500HJ2D")
        self.configure(bg=BG)
        self.minsize(700, 600)

        self._callsign     = tk.StringVar(value="NOCALL")
        self._callsign.trace_add('write', lambda *a: self._on_callsign_changed())
        self._dcd_active   = False
        self._transmitting = False
        self._running      = True

        # RX accumulation buffer — collects audio until carrier drops
        self._rx_acc       = np.array([], dtype=np.float32)
        self._rx_recording = False

        self._mod   = FrameModulator()
        self._demod = Demodulator()
        self._dcd   = DCD(threshold_db=DCD_THRESH)
        self._audio = AudioEngine()

        # Config — load persisted settings
        self._config = self._load_config()
        saved_cs     = self._config.get('callsign', 'NOCALL')
        self._callsign.set(saved_cs)

        # TCI radio control
        self._tci = None
        self._ptt = None
        self._tci_frequency_hz = 0
        self._tci_mode      = tk.StringVar(value='---')
        self._tci_connected = False
        self._led_tci       = None  # created in _make_toolbar
        self._init_tci()

        # Station information — used for macro variable substitution
        self._station_info = self._config.get('station_info', {
            'qth':    '',
            'grid':   '',
            'pwr':    '',
            'county': '',
            'park':   '',
        })

        # Macro buttons — loaded from config, fall back to defaults
        self._macros = [list(m) for m in
                        self._config.get('macros', DEFAULT_MACROS)]

        # Level meter state
        self._rx_gain      = 1.0     # RX input gain multiplier
        self._tx_gain      = 0.8     # TX output gain multiplier
        self._rx_level_db  = -60.0   # current RX level in dB
        self._tx_level_db  = -60.0   # current TX peak level in dB
        self._tx_peak_hold = 0.0     # time of last TX peak

        self._build_ui()
        self._start_audio()
        self._start_workers()
        self._tick()

        self.protocol("WM_DELETE_WINDOW", self._quit)
        self.bind('<Control-q>', lambda e: self._quit())

        bw  = int(NUM_TONES * SYMBOL_RATE)
        bps = int(125 * 0.5)
        self._sys(f"HAVEN-FSK v{APP_VERSION} ready — "
                  f"{NUM_TONES}-FSK  {bw} Hz BW  ~{bps} bps net")
        self._sys(f"Signal band: {BASE_FREQ:.0f} – "
                  f"{BASE_FREQ + NUM_TONES*SYMBOL_RATE:.0f} Hz")
        self._sys(f"Frame sync: {len(SYNC_SYMS)}-symbol preamble  "
                  f"({PREAMBLE_LEN/SAMPLE_RATE*1000:.0f}ms)")
        if FEC_AVAILABLE:
            self._sys("FEC: LDPC(192,96) rate 1/2  +  CRC-16")
        else:
            self._sys("FEC: not available — pip install ldpc", 'warn')
        self._sys("Type /help for commands")
        if not AUDIO_AVAILABLE:
            self._sys("No audio hardware — WAV file mode", 'warn')

    # ── Build UI ──────────────────────────────────────────────────────────────

    def _build_ui(self):
        self._make_menubar()
        self._make_toolbar()
        self._make_waterfall()
        self._make_chat()
        self._make_macros()
        self._make_input()
        self._make_levels()
        self._make_statusbar()

    def _make_menubar(self):
        menubar = tk.Menu(self, bg=BG2, fg=TEXT_FG,
                          activebackground=ACCENT,
                          activeforeground=GREEN,
                          relief=tk.FLAT)
        self.configure(menu=menubar)

        # ── Settings menu ─────────────────────────────────────────────────────
        settings_menu = tk.Menu(menubar, tearoff=0,
                                bg=BG2, fg=TEXT_FG,
                                activebackground=ACCENT,
                                activeforeground=GREEN)
        menubar.add_cascade(label="Settings", menu=settings_menu)
        settings_menu.add_command(label="Audio Devices...",
                                  command=self._show_devices)
        settings_menu.add_command(label="Sample Rate...",
                                  command=self._show_sample_rate_dialog)
        settings_menu.add_separator()
        settings_menu.add_command(label="About HAVEN-FSK",
                                  command=self._show_about)

        # ── Station menu ──────────────────────────────────────────────────────
        station_menu = tk.Menu(menubar, tearoff=0,
                               bg=BG2, fg=TEXT_FG,
                               activebackground=ACCENT,
                               activeforeground=GREEN)
        menubar.add_cascade(label="Station", menu=station_menu)
        station_menu.add_command(label="Station Info...",
                                 command=self._show_station_info_dialog)

    def _make_toolbar(self):
        bar = tk.Frame(self, bg=ACCENT, pady=5)
        bar.pack(fill=tk.X)

        def lbl(t):
            tk.Label(bar, text=t, bg=ACCENT,
                     fg=TEXT_FG, font=FONT_UI).pack(side=tk.LEFT, padx=(6,2))

        lbl("Call:")
        tk.Entry(bar, textvariable=self._callsign, width=9,
                 font=FONT_MONO, bg=TEXT_BG, fg=GREEN,
                 insertbackground=GREEN).pack(side=tk.LEFT, padx=(0,10))
        lbl("DCD:")
        self._led_dcd = LED(bar)
        self._led_dcd.pack(side=tk.LEFT, padx=(2,10))
        lbl("TX:")
        self._led_tx = LED(bar)
        self._led_tx.pack(side=tk.LEFT, padx=(2,6))
        lbl("TCI:")
        self._led_tci = LED(bar)
        self._led_tci.pack(side=tk.LEFT, padx=(2,10))

        bw  = int(NUM_TONES * SYMBOL_RATE)
        bps = int(125 * 0.5)
        tk.Label(bar,
                 text=f"  {NUM_TONES}-FSK  |  {bw} Hz  |  ~{bps} bps  ",
                 bg=BG2, fg=AMBER, font=("Courier", 9),
                 relief=tk.FLAT).pack(side=tk.LEFT, padx=4)

        # TCI frequency display — digit-by-digit with mousewheel tuning
        freq_frame = tk.Frame(bar, bg=BG2)
        freq_frame.pack(side=tk.LEFT, padx=(8, 4))
        self._freq_digit_vars   = []
        self._freq_digit_labels = []
        # Format: MM.KKK.HHH  (2 MHz digits, 3 kHz digits, 3 Hz digits)
        # Steps per digit left-to-right: 10M 1M . 100k 10k 1k . 100 10 1
        _steps = [10_000_000, 1_000_000, 100_000, 10_000, 1_000, 100, 10, 1]
        _digit = 0
        for _pos in range(10):            # 10 chars: 8 digits + 2 dots
            if _pos in (2, 6):            # dot separators
                tk.Label(freq_frame, text='.', bg=BG2, fg='#00ccff',
                         font=("Courier", 11, 'bold')).pack(side=tk.LEFT)
            else:
                _var = tk.StringVar(value='-')
                _lbl = tk.Label(freq_frame, textvariable=_var,
                                bg=BG2, fg='#00ccff',
                                font=("Courier", 11, 'bold'),
                                width=1, cursor='sb_v_double_arrow')
                _lbl.pack(side=tk.LEFT)
                _step = _steps[_digit]
                _lbl.bind('<MouseWheel>',
                          lambda e, s=_step: self._freq_scroll(e.delta, s))
                _lbl.bind('<Enter>',
                          lambda e, l=_lbl: l.configure(bg='#1a3a5a'))
                _lbl.bind('<Leave>',
                          lambda e, l=_lbl: l.configure(bg=BG2))
                self._freq_digit_vars.append(_var)
                self._freq_digit_labels.append(_lbl)
                _digit += 1
        tk.Label(freq_frame, text=' MHz', bg=BG2, fg='#00ccff',
                 font=("Courier", 9)).pack(side=tk.LEFT)

        tk.Label(bar, textvariable=self._tci_mode,
                 bg=BG2, fg='#00ccff', font=("Courier", 10),
                 width=5).pack(side=tk.LEFT, padx=(0,6))

        # ── Second row: audio device dropdowns ────────────────────────────────
        dev_bar = tk.Frame(self, bg=BG2, pady=3)
        dev_bar.pack(fill=tk.X)

        # Build device lists
        self._inp_devs = []
        self._out_devs = []
        self._rx_dev_var = tk.StringVar(value="RX Input: (default)")
        self._tx_dev_var = tk.StringVar(value="TX Output: (default)")

        # Labels
        tk.Label(dev_bar, text="RX:", bg=BG2, fg=TEXT_FG,
                 font=FONT_UI).pack(side=tk.LEFT, padx=(8,2))

        rx_frame = tk.Frame(dev_bar, bg=BG2)
        rx_frame.pack(side=tk.LEFT, padx=(0,10))
        self._rx_combo = tk.Entry(rx_frame,
                                  textvariable=self._rx_dev_var,
                                  state='readonly',
                                  readonlybackground=TEXT_BG,
                                  fg=GREEN,
                                  font=("Helvetica", 9),
                                  width=36,
                                  relief=tk.SUNKEN, bd=1)
        self._rx_combo.pack(side=tk.LEFT)
        tk.Button(rx_frame, text='▼', command=self._pick_rx_device,
                  bg=ACCENT, fg=GREEN, font=("Helvetica", 8),
                  relief=tk.FLAT, padx=2).pack(side=tk.LEFT)

        tk.Label(dev_bar, text="TX:", bg=BG2, fg=TEXT_FG,
                 font=FONT_UI).pack(side=tk.LEFT, padx=(0,2))

        tx_frame = tk.Frame(dev_bar, bg=BG2)
        tx_frame.pack(side=tk.LEFT, padx=(0,6))
        self._tx_combo = tk.Entry(tx_frame,
                                  textvariable=self._tx_dev_var,
                                  state='readonly',
                                  readonlybackground=TEXT_BG,
                                  fg=GREEN,
                                  font=("Helvetica", 9),
                                  width=36,
                                  relief=tk.SUNKEN, bd=1)
        self._tx_combo.pack(side=tk.LEFT)
        tk.Button(tx_frame, text='▼', command=self._pick_tx_device,
                  bg=ACCENT, fg=GREEN, font=("Helvetica", 8),
                  relief=tk.FLAT, padx=2).pack(side=tk.LEFT)

        # Populate device lists after a short delay
        # (so the window is visible first)
        self.after(200, self._populate_device_combos)
        self.after(800, self._apply_saved_config)
        self.after(1000, self._sync_combo_to_engine)

    def _make_waterfall(self):
        frame = tk.LabelFrame(self, text=" Waterfall ",
                              bg=BG2, fg=AMBER, font=FONT_UI,
                              bd=1, relief=tk.GROOVE)
        frame.pack(fill=tk.BOTH, padx=6, pady=(4,2))
        self._wf = Waterfall(frame)

    def _make_chat(self):
        frame = tk.LabelFrame(self, text=" Received ",
                              bg=BG, fg=AMBER, font=FONT_UI,
                              bd=1, relief=tk.GROOVE)
        frame.pack(fill=tk.BOTH, expand=True, padx=6, pady=2)
        self._chat = scrolledtext.ScrolledText(
            frame, font=FONT_MONO, bg=TEXT_BG, fg=TEXT_FG,
            insertbackground=GREEN, wrap=tk.WORD,
            state=tk.DISABLED, height=9,
        )
        self._chat.pack(fill=tk.BOTH, expand=True)
        self._chat.tag_config('rx',   foreground=GREEN)
        self._chat.tag_config('tx',   foreground=AMBER)
        self._chat.tag_config('sys',  foreground='#666688')
        self._chat.tag_config('warn', foreground=RED)
        self._chat.tag_config('ts',   foreground='#444466')

    def _make_macros(self):
        """Clear button on left; macro buttons grouped on the right."""
        frame = tk.Frame(self, bg=BG2, pady=3)
        frame.pack(fill=tk.X, padx=6, pady=(2, 0))

        # Clear button — far left
        tk.Button(frame, text="Clear Log", command=self._clear_chat,
                  bg=ACCENT, fg=AMBER, font=FONT_UI,
                  relief=tk.FLAT, padx=10, pady=2).pack(side=tk.LEFT, padx=(0, 4))

        # Macros group — packed as a unit to the right edge
        macro_grp = tk.Frame(frame, bg=BG2)
        macro_grp.pack(side=tk.RIGHT)

        tk.Label(macro_grp, text="Macros:", bg=BG2, fg=TEXT_FG,
                 font=FONT_UI).pack(side=tk.LEFT, padx=(0, 6))

        self._macro_buttons = []
        for i, (label, text) in enumerate(self._macros):
            btn = tk.Button(
                macro_grp,
                text=label,
                bg=ACCENT, fg=GREEN, font=FONT_UI,
                relief=tk.FLAT, padx=10, pady=2,
                command=lambda t=text: self._send_macro(t) if t else None,
            )
            btn.pack(side=tk.LEFT, padx=2)
            btn.bind('<Button-3>', lambda e, idx=i: self._edit_macro(idx))
            self._macro_buttons.append(btn)

        tk.Label(macro_grp, text="(right-click to edit)",
                 bg=BG2, fg='#444466', font=("Helvetica", 8)).pack(
                 side=tk.LEFT, padx=(8, 0))

    def _edit_macro(self, idx):
        """Popup editor for a single macro button."""
        label, text = self._macros[idx]
        win = tk.Toplevel(self)
        win.title(f"Edit Macro {idx + 1}")
        win.configure(bg=BG)
        win.resizable(False, False)
        win.grab_set()

        tk.Label(win, text="Button label:", bg=BG,
                 fg=TEXT_FG, font=FONT_UI).pack(padx=16, pady=(12, 2), anchor=tk.W)
        lbl_var = tk.StringVar(value=label)
        tk.Entry(win, textvariable=lbl_var, bg=TEXT_BG, fg=GREEN,
                 font=FONT_MONO, width=14).pack(padx=16, fill=tk.X)

        tk.Label(win, text="Message  (<CALL> = your callsign):",
                 bg=BG, fg=TEXT_FG, font=FONT_UI).pack(padx=16, pady=(8, 2), anchor=tk.W)
        txt_var = tk.StringVar(value=text)
        tk.Entry(win, textvariable=txt_var, bg=TEXT_BG, fg=GREEN,
                 font=FONT_MONO, width=52).pack(padx=16, fill=tk.X)

        def save():
            new_label = lbl_var.get().strip() or f"MSG{idx + 1}"
            new_text  = txt_var.get().strip()
            self._macros[idx] = [new_label, new_text]
            self._macro_buttons[idx].configure(
                text=new_label,
                command=lambda t=new_text: self._send_macro(t) if t else None,
            )
            self._save_config()
            win.destroy()

        bf = tk.Frame(win, bg=BG)
        bf.pack(pady=10)
        tk.Button(bf, text="Save", command=save,
                  bg=ACCENT, fg=GREEN, font=FONT_UI,
                  relief=tk.FLAT, padx=12).pack(side=tk.LEFT, padx=4)
        tk.Button(bf, text="Cancel", command=win.destroy,
                  bg=BG2, fg=TEXT_FG, font=FONT_UI,
                  relief=tk.FLAT, padx=12).pack(side=tk.LEFT, padx=4)

    def _make_input(self):
        f = tk.Frame(self, bg=BG, pady=4)
        f.pack(fill=tk.X, padx=6)
        tk.Label(f, text="Message:", bg=BG,
                 fg=TEXT_FG, font=FONT_UI).pack(side=tk.LEFT, padx=(0,4))
        self._entry = tk.Entry(f, font=FONT_MONO, bg=TEXT_BG,
                               fg=GREEN, insertbackground=GREEN)
        self._entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0,6))
        self._entry.bind('<Return>', lambda e: self._send())
        self._entry.focus_set()
        tk.Button(f, text="  Send  ", command=self._send,
                  bg=ACCENT, fg=GREEN, font=FONT_UI,
                  relief=tk.FLAT).pack(side=tk.LEFT)

    def _make_statusbar(self):
        self._status = tk.StringVar(value="Ready")
        tk.Label(self, textvariable=self._status,
                 bg=BG2, fg='#556677', font=("Helvetica", 9),
                 anchor=tk.W, padx=8).pack(fill=tk.X, side=tk.BOTTOM)

    def _make_levels(self):
        """
        Level meter strip — two rows, RX and TX.
        Each row: label | slider (fader) | bar meter | dB readout | status
        RX meter updates continuously from incoming audio.
        TX meter peaks during transmission, holds 2s then decays.
        """
        METER_W  = 300   # meter bar canvas width
        METER_H  = 14    # meter bar canvas height
        SLIDER_W = 160   # slider width

        outer = tk.Frame(self, bg=BG2, pady=3)
        outer.pack(fill=tk.X, side=tk.BOTTOM, padx=0)

        # Colour zones for meter bar
        # Green: -40 to -12 dB (good operating range)
        # Amber: -12 to -6 dB  (getting hot)
        # Red:   -6  to  0 dB  (clipping risk)
        def db_to_x(db, width=METER_W):
            """Convert dB value to pixel position. Range -60 to 0 dB."""
            clamped = max(-60.0, min(0.0, db))
            return int((clamped + 60.0) / 60.0 * width)

        def meter_color(db):
            if db < -12:  return '#00bb55'   # green
            elif db < -6: return '#ddaa00'   # amber
            else:         return '#dd3333'   # red

        def status_text(db, is_tx=False):
            if is_tx and db < -55:
                return 'idle'
            if db < -30:   return 'quiet'
            elif db < -12: return 'good'
            elif db < -6:  return 'hot'
            else:          return 'CLIP'

        # ── RX row ────────────────────────────────────────────────────────────
        rx_row = tk.Frame(outer, bg=BG2)
        rx_row.pack(fill=tk.X, padx=6, pady=1)

        tk.Label(rx_row, text="RX", bg=BG2, fg='#88aacc',
                 font=("Courier", 9, 'bold'), width=3).pack(side=tk.LEFT)

        self._rx_slider = tk.Scale(
            rx_row, from_=0, to=200, orient=tk.HORIZONTAL,
            length=SLIDER_W, showvalue=False,
            bg=BG2, fg=GREEN, troughcolor=ACCENT,
            activebackground=GREEN, highlightthickness=0,
            command=self._rx_slider_changed,
        )
        self._rx_slider.set(100)   # 100 = unity gain
        self._rx_slider.pack(side=tk.LEFT, padx=(2,6))

        # Meter canvas
        self._rx_canvas = tk.Canvas(rx_row, width=METER_W, height=METER_H,
                                    bg='#050505', highlightthickness=1,
                                    highlightbackground='#333344')
        self._rx_canvas.pack(side=tk.LEFT, padx=(0,6))
        # Zone markers
        for db, label in [(-12,''), (-6,'')]:
            x = db_to_x(db)
            self._rx_canvas.create_line(x, 0, x, METER_H,
                                        fill='#555566', width=1)

        self._rx_db_var = tk.StringVar(value='-60.0 dB')
        tk.Label(rx_row, textvariable=self._rx_db_var,
                 bg=BG2, fg='#88aacc', font=("Courier", 9),
                 width=9, anchor=tk.E).pack(side=tk.LEFT)

        self._rx_status_var = tk.StringVar(value='quiet')
        tk.Label(rx_row, textvariable=self._rx_status_var,
                 bg=BG2, fg='#556677', font=("Courier", 9),
                 width=5, anchor=tk.W).pack(side=tk.LEFT, padx=(4,0))

        # ── TX row ────────────────────────────────────────────────────────────
        tx_row = tk.Frame(outer, bg=BG2)
        tx_row.pack(fill=tk.X, padx=6, pady=1)

        tk.Label(tx_row, text="TX", bg=BG2, fg='#ccaa44',
                 font=("Courier", 9, 'bold'), width=3).pack(side=tk.LEFT)

        self._tx_slider = tk.Scale(
            tx_row, from_=0, to=200, orient=tk.HORIZONTAL,
            length=SLIDER_W, showvalue=False,
            bg=BG2, fg=AMBER, troughcolor=ACCENT,
            activebackground=AMBER, highlightthickness=0,
            command=self._tx_slider_changed,
        )
        self._tx_slider.set(80)    # 80 = -2dB, safe starting point
        self._tx_slider.pack(side=tk.LEFT, padx=(2,6))

        self._tx_canvas = tk.Canvas(tx_row, width=METER_W, height=METER_H,
                                    bg='#050505', highlightthickness=1,
                                    highlightbackground='#333344')
        self._tx_canvas.pack(side=tk.LEFT, padx=(0,6))
        for db in [-12, -6]:
            x = db_to_x(db)
            self._tx_canvas.create_line(x, 0, x, METER_H,
                                        fill='#555566', width=1)

        self._tx_db_var = tk.StringVar(value='  idle')
        tk.Label(tx_row, textvariable=self._tx_db_var,
                 bg=BG2, fg='#ccaa44', font=("Courier", 9),
                 width=9, anchor=tk.E).pack(side=tk.LEFT)

        self._tx_status_var = tk.StringVar(value='idle')
        tk.Label(tx_row, textvariable=self._tx_status_var,
                 bg=BG2, fg='#556677', font=("Courier", 9),
                 width=5, anchor=tk.W).pack(side=tk.LEFT, padx=(4,0))

        # Store constants for meter update
        self._meter_w     = METER_W
        self._meter_h     = METER_H
        self._db_to_x     = db_to_x
        self._meter_color = meter_color
        self._status_text = status_text

    # ── Level slider callbacks ─────────────────────────────────────────────────

    def _rx_slider_changed(self, val):
        """RX gain fader moved. 0-200 maps to 0.0-4.0x gain (-inf to +12dB)."""
        pct = float(val) / 100.0   # 0.0 to 2.0
        self._rx_gain = pct
        # Update label to show approximate dB
        if pct <= 0.01:
            db_str = "-∞"
        else:
            import math
            db = 20 * math.log10(pct)
            db_str = f"{db:+.1f}"

    def _tx_slider_changed(self, val):
        """TX gain fader moved. 0-200 maps to 0.0-2.0x gain."""
        pct = float(val) / 100.0
        self._tx_gain = pct
        self._audio.output_gain = pct

    # ── Meter update (called from _tick) ──────────────────────────────────────

    def _update_rx_meter(self, chunk: np.ndarray):
        """Update RX meter from a chunk of audio. Called from wf_loop."""
        if not hasattr(self, '_rx_canvas'):
            return
        import math
        # Apply RX gain
        if self._rx_gain != 1.0:
            chunk = chunk * self._rx_gain

        # RMS level
        rms = float(np.sqrt(np.mean(chunk ** 2) + 1e-10))
        db  = 20 * math.log10(rms)
        db  = max(-60.0, min(0.0, db))

        # Smooth the meter (fast attack, slow decay)
        if db > self._rx_level_db:
            self._rx_level_db = db                      # fast attack
        else:
            self._rx_level_db = self._rx_level_db * 0.85 + db * 0.15  # slow decay

        self.after(0, lambda d=self._rx_level_db: self._draw_meter(
            self._rx_canvas, d,
            self._rx_db_var, self._rx_status_var, is_tx=False))

    def _update_tx_meter(self, chunk: np.ndarray):
        """Update TX meter during transmission. Called from audio callback."""
        if not hasattr(self, '_tx_canvas'):
            return
        import math
        rms = float(np.sqrt(np.mean(chunk ** 2) + 1e-10))
        db  = 20 * math.log10(rms)
        db  = max(-60.0, min(0.0, db))

        # Peak hold — update if new peak or hold expired
        now = time.monotonic()
        if db > self._tx_level_db or (now - self._tx_peak_hold) > 2.0:
            self._tx_level_db  = db
            self._tx_peak_hold = now

        self.after(0, lambda d=self._tx_level_db: self._draw_meter(
            self._tx_canvas, d,
            self._tx_db_var, self._tx_status_var, is_tx=True))

    def _tx_meter_clear(self):
        """Decay TX meter back to idle after transmission ends."""
        if not hasattr(self, '_tx_canvas'):
            return
        self._tx_level_db  = -60.0
        self._tx_peak_hold = 0.0
        self.after(2000, lambda: self._draw_meter(
            self._tx_canvas, -60.0,
            self._tx_db_var, self._tx_status_var, is_tx=True))

    def _draw_meter(self, canvas, db, db_var, status_var, is_tx=False):
        """Draw the meter bar on a canvas widget."""
        if not hasattr(self, '_meter_w'):
            return
        W = self._meter_w
        H = self._meter_h
        x = self._db_to_x(db, W)
        color = self._meter_color(db)

        canvas.delete('bar')
        if x > 0:
            canvas.create_rectangle(0, 1, x, H-1,
                                    fill=color, outline='',
                                    tags='bar')
        # Zone dividers on top of bar
        for div_db in [-12, -6]:
            dx = self._db_to_x(div_db, W)
            canvas.create_line(dx, 0, dx, H,
                               fill='#555566', width=1, tags='bar')

        if is_tx and db < -55:
            db_var.set('   idle')
            status_var.set('idle')
        else:
            db_var.set(f"{db:+6.1f} dB")
            status_var.set(self._status_text(db, is_tx))

    # ── Audio workers ─────────────────────────────────────────────────────────

    def _start_audio(self):
        if not AUDIO_AVAILABLE:
            self._setstatus("No audio — WAV file mode")
            return
        ok, msg = self._audio.start()
        self._setstatus(msg)
        if not ok:
            self._sys(f"Audio error: {msg}", 'warn')
            self._sys("Use the RX/TX dropdowns to select your audio interface", 'sys')
        # Sync after populate runs at 200ms
        self.after(400, self._sync_combo_to_engine)

    def _populate_device_combos(self):
        """Build device lists for the picker dialogs."""
        if not AUDIO_AVAILABLE:
            return
        try:
            devs = sd.query_devices()
            self._inp_devs = []
            self._out_devs = []

            for i, d in enumerate(devs):
                entry = {
                    'index':   i,
                    'name':    d['name'],
                    'inputs':  d['max_input_channels'],
                    'outputs': d['max_output_channels'],
                    'rate':    int(d['default_samplerate']),
                }
                if entry['inputs']  > 0:
                    self._inp_devs.append(entry)
                if entry['outputs'] > 0:
                    self._out_devs.append(entry)

            # Show currently active devices
            self._show_devices_in_combos()

        except Exception as e:
            self._sys(f"Could not list audio devices: {e}", 'warn')

    def _show_devices_in_combos(self):
        """
        Read current device indices from audio engine and display names.
        Called any time a device selection changes.
        """
        if not AUDIO_AVAILABLE:
            return
        try:
            devs    = sd.query_devices()
            in_idx  = self._audio.in_device
            out_idx = self._audio.out_device

            # If still None, try to get from the open stream directly
            if in_idx is None and self._audio._rx_stream is not None:
                try:
                    in_idx = self._audio._rx_stream.device
                    if isinstance(in_idx, (list, tuple)):
                        in_idx = in_idx[0]
                except Exception:
                    pass

            if out_idx is None and self._audio._tx_stream is not None:
                try:
                    out_idx = self._audio._tx_stream.device
                    if isinstance(out_idx, (list, tuple)):
                        out_idx = out_idx[1]
                except Exception:
                    pass

            # Set RX display
            if in_idx is not None:
                try:
                    name = devs[int(in_idx)]['name']
                    rate = getattr(self._audio, '_rx_hw_rate', SAMPLE_RATE)
                    tag  = f' [{rate}Hz]' if rate != SAMPLE_RATE else ''
                    self._rx_dev_var.set(f"{name}{tag}")
                except Exception as e:
                    self._rx_dev_var.set(f"Device {in_idx}")
            else:
                self._rx_dev_var.set("(default)")

            # Set TX display
            if out_idx is not None:
                try:
                    name = devs[int(out_idx)]['name']
                    rate = getattr(self._audio, '_tx_hw_rate', SAMPLE_RATE)
                    tag  = f' [{rate}Hz]' if rate != SAMPLE_RATE else ''
                    self._tx_dev_var.set(f"{name}{tag}")
                except Exception as e:
                    self._tx_dev_var.set(f"Device {out_idx}")
            else:
                self._tx_dev_var.set("(default)")

        except Exception as e:
            self._rx_dev_var.set("(error reading devices)")
            self._tx_dev_var.set("(error reading devices)")

    def _sync_combo_to_engine(self):
        """Poll device display — called periodically to keep display current."""
        self._show_devices_in_combos()
        if self._running:
            self.after(1000, self._sync_combo_to_engine)

    def _pick_rx_device(self):
        """Show RX device picker dialog."""
        self._pick_device('RX Input', self._inp_devs,
                          self._audio.in_device,
                          self._rx_device_selected)

    def _pick_tx_device(self):
        """Show TX device picker dialog."""
        self._pick_device('TX Output', self._out_devs,
                          self._audio.out_device,
                          self._tx_device_selected)

    def _pick_device(self, title, devs, current_idx, callback):
        """Generic device picker popup."""
        if not devs:
            messagebox.showinfo(title, "No audio devices found.")
            return
        win = tk.Toplevel(self)
        win.title(f"Select {title} Device")
        win.configure(bg=BG)
        win.resizable(False, False)

        tk.Label(win, text=f"Select {title}:",
                 bg=BG, fg=TEXT_FG, font=FONT_UI).pack(padx=12, pady=(10,4))

        lb = tk.Listbox(win, bg=TEXT_BG, fg=GREEN,
                        selectbackground=ACCENT, selectforeground=GREEN,
                        font=("Helvetica", 9), width=55, height=min(len(devs),16))
        lb.pack(padx=12, pady=4)

        for d in devs:
            rate   = d['rate']
            resamp = f' [{rate}Hz]' if rate != SAMPLE_RATE else ''
            lb.insert(tk.END, f"{d['name']}{resamp}")
            if d['index'] == current_idx:
                lb.selection_set(tk.END)
                lb.see(tk.END)

        def select():
            sel = lb.curselection()
            if sel:
                callback(devs[sel[0]])
            win.destroy()

        tk.Button(win, text="Select", command=select,
                  bg=ACCENT, fg=GREEN, font=FONT_UI,
                  relief=tk.FLAT, padx=10).pack(pady=8)
        win.grab_set()

    def _rx_device_selected(self, dev):
        """RX device chosen from picker."""
        self._audio.in_device = dev['index']
        threading.Thread(target=self._restart_audio, daemon=True).start()

    def _tx_device_selected(self, dev):
        """TX device chosen from picker."""
        self._audio.out_device = dev['index']
        threading.Thread(target=self._restart_audio, daemon=True).start()

    def _rx_device_changed(self, event=None):
        """Legacy — kept for compatibility."""
        pass

    def _tx_device_changed(self, event=None):
        """Legacy — kept for compatibility."""
        pass

    def _restart_audio(self, preserve_tx_dev=None, preserve_rx_dev=None):
        """Restart audio engine then sync combos to what actually opened."""
        self._audio.stop()
        time.sleep(0.3)
        ok, msg = self._audio.start()
        if ok:
            self.after(0, lambda: self._sys(f"Audio: {msg}"))
        else:
            self.after(0, lambda: self._sys(
                f"Audio restart failed: {msg}", 'warn'))
        # Show what devices are now active
        self.after(200, self._show_devices_in_combos)

    def _update_device_labels(self):
        """Redirect to _sync_combo_to_engine — kept for compatibility."""
        self._sync_combo_to_engine()

    def _start_workers(self):
        threading.Thread(target=self._rx_loop, daemon=True).start()
        threading.Thread(target=self._wf_loop, daemon=True).start()

    def _wf_loop(self):
        """Dedicated thread drains wf_queue into waterfall — never blocks RX."""
        while self._running:
            try:
                chunk = self._audio.wf_queue.get(timeout=0.1)
            except queue.Empty:
                continue

            # Suppress waterfall and RX meter during TX
            # prevents VAC echo from showing on waterfall
            if self._transmitting:
                continue

            # Apply RX gain before waterfall and meter
            if self._rx_gain != 1.0:
                chunk = chunk * self._rx_gain
            self.after(0, lambda c=chunk: self._wf.push(c))
            self._update_rx_meter(chunk)

    def _rx_loop(self):
        """
        Accumulate audio while DCD is active, decode when carrier drops.
        RX is muted during transmission to prevent VAC echo feedback —
        when using Thetis VAC the TX audio loops back into the RX path.
        """
        DCD_HOLDOFF   = 4
        holdoff_count = 0

        while self._running:
            try:
                chunk = self._audio.rx_queue.get(timeout=0.1)
            except queue.Empty:
                continue

            # Discard RX audio while we are transmitting
            # Prevents VAC loopback from triggering DCD or corrupt decode
            if self._transmitting:
                self._rx_acc      = np.array([], dtype=np.float32)
                self._rx_recording = False
                holdoff_count     = 0
                self._dcd_active  = False
                self.after(0, lambda: self._led_dcd.set('idle'))
                continue

            was_active       = self._dcd_active
            self._dcd_active = self._dcd.update(chunk)

            if self._dcd_active:
                self._rx_acc       = np.concatenate([self._rx_acc, chunk])
                self._rx_recording = True
                holdoff_count      = 0
            elif self._rx_recording:
                holdoff_count += 1
                self._rx_acc = np.concatenate([self._rx_acc, chunk])
                if holdoff_count >= DCD_HOLDOFF:
                    audio_copy         = self._rx_acc.copy()
                    self._rx_acc       = np.array([], dtype=np.float32)
                    self._rx_recording = False
                    holdoff_count      = 0
                    threading.Thread(target=self._decode_buffer,
                                     args=(audio_copy,), daemon=True).start()
            else:
                self._rx_acc  = np.array([], dtype=np.float32)
                holdoff_count = 0

    def _decode_buffer(self, audio: np.ndarray):
        """
        Find preamble, decode frame, display result.
        Handles three outcomes:
          - Clean decode with CRC pass  → green text
          - Decode with CRC fail         → text + warning marker
          - HAVEN-FSK preamble found but payload unreadable → log warning
          - No preamble (FT8, voice, RTTY, noise) → silent status update only
        """
        dur = len(audio) / SAMPLE_RATE
        self.after(0, lambda: self._setstatus("Decoding..."))

        # Check for HAVEN-FSK preamble before attempting full decode.
        # If no preamble is found the signal is something else entirely
        # (FT8, voice, RTTY, noise) — update status bar only, no chat log entry.
        pos, score = find_preamble(audio)
        if pos < 0:
            self.after(0, lambda: self._setstatus(
                f"RX: {dur:.1f}s signal — no HAVEN-FSK preamble"))
            return

        if FEC_AVAILABLE:
            result = self._decode_with_fec(audio)
        else:
            result = self._decode_raw(audio)

        if result is None:
            # Preamble confirmed but payload couldn't be recovered
            self.after(0, lambda: self._log(
                'sys', '  HAVEN-FSK preamble detected — payload unreadable'))
            self.after(0, lambda: self._setstatus(
                f"RX: preamble found, could not decode ({dur:.1f}s)"))
        else:
            text, crc_ok = result
            if crc_ok:
                self.after(0, lambda t=text: self._log('rx', t))
                self.after(0, lambda: self._setstatus(
                    f"RX decoded {len(text)} chars in {dur:.1f}s"))
            else:
                # CRC failed — show text with warning marker
                self.after(0, lambda t=text: self._log_crc_error(t))
                self.after(0, lambda: self._setstatus(
                    f"RX {len(text)} chars in {dur:.1f}s — CRC error"))

    def _decode_with_fec(self, audio: np.ndarray):
        """Decode frame using FEC pipeline."""
        try:
            pos, score = find_preamble(audio)
            if pos < 0:
                return None
            payload_audio = audio[pos + PREAMBLE_LEN:]
            if len(payload_audio) < SAMPLES_PER_SYMBOL * 8:
                return None

            # Decode header (4 symbols = 2 bytes)
            syms = []
            for i in range(4):
                b = payload_audio[i*SAMPLES_PER_SYMBOL:(i+1)*SAMPLES_PER_SYMBOL]
                s, _ = _detect_symbol_robust(b)
                syms.append(s)
            hdr_bytes = Demodulator().symbols_to_bytes(syms)
            if len(hdr_bytes) < 2:
                return None

            byte0, byte1 = hdr_bytes[0], hdr_bytes[1]
            version  = (byte0 >> 4) & 0x0F
            flags    = byte0 & 0x0F
            n_blocks = byte1
            use_fec  = bool(flags & 0x01)

            if version != 1 or n_blocks == 0:
                # Not a v1 FEC frame — try raw decode
                return self._decode_raw(audio)

            # Decode CRC (next 4 symbols = 2 bytes)
            crc_syms = []
            for i in range(4, 8):
                b = payload_audio[i*SAMPLES_PER_SYMBOL:(i+1)*SAMPLES_PER_SYMBOL]
                s, _ = _detect_symbol_robust(b)
                crc_syms.append(s)
            crc_bytes = Demodulator().symbols_to_bytes(crc_syms)
            if len(crc_bytes) < 2:
                return None
            crc = crc_bytes[:2]

            # Decode FEC payload
            fec_audio = payload_audio[8 * SAMPLES_PER_SYMBOL:]
            # Get raw bytes from modem
            fec_decoded = Demodulator().demodulate(fec_audio)
            fec_bits    = np.unpackbits(
                np.frombuffer(fec_decoded, dtype=np.uint8))
            n_fec_bits  = n_blocks * FEC_N
            if len(fec_bits) < n_fec_bits:
                fec_bits = np.pad(fec_bits, (0, n_fec_bits - len(fec_bits)))
            fec_floats  = (1.0 - 2.0 * fec_bits[:n_fec_bits]).astype(np.float32)

            # FEC decode with SNR estimate
            orig_len = n_blocks * BYTES_PER_BLOCK
            text     = fec_decode(fec_floats, n_blocks, orig_len, snr_db=5.0)
            text     = text.rstrip('\x00\n\r\t ').strip()

            if not text:
                return None

            # Verify CRC
            hdr = bytes([byte0, byte1])
            crc_ok = crc16_check(hdr + (text + ' ').encode('utf-8'), crc)

            return text, crc_ok

        except Exception as e:
            # Fall back to raw decode on any error
            return self._decode_raw(audio)

    def _decode_raw(self, audio: np.ndarray):
        """Decode frame without FEC (fallback)."""
        text = decode_frame(audio)
        if text is None:
            return None
        # No CRC available — assume ok
        return text, True

    def _log_crc_error(self, text: str):
        """Display received text with CRC error warning marker."""
        ts = time.strftime("%H:%M:%S")
        self._chat.configure(state=tk.NORMAL)
        self._chat.insert(tk.END, f"[{ts}] ", 'ts')
        self._chat.insert(tk.END, f"{text}", 'rx')
        self._chat.insert(tk.END, "  \u26a0 CRC Error\n", 'warn')
        self._chat.see(tk.END)
        self._chat.configure(state=tk.DISABLED)

    # ── Send ──────────────────────────────────────────────────────────────────

    def _validate_callsign(self) -> str:
        """
        Validate callsign before any transmission.
        Returns callsign string if valid, None if not.
        Pops a dialog and focuses the callsign field if invalid.
        """
        cs = self._callsign.get().strip().upper()
        if not cs or cs == 'NOCALL':
            messagebox.showwarning(
                "Callsign Required",
                "Please enter your callsign before transmitting.\n\n"
                "FCC Part 97 requires station identification."
            )
            # Focus the callsign entry field
            self.focus_force()
            # Find and focus callsign entry
            for widget in self.winfo_children():
                for child in widget.winfo_children():
                    if isinstance(child, tk.Entry):
                        child.focus_set()
                        child.select_range(0, tk.END)
                        break
            return None
        return cs

    def _build_tx_message(self, text: str, cs: str) -> str:
        """
        Build the final transmit message with smart callsign handling.
        If callsign already appears in the message, send as-is.
        If not, prepend CALLSIGN: to identify the origin.
        This ensures every message has an identifiable source
        without redundantly repeating the callsign.
        """
        if cs.upper() in text.upper():
            return text          # callsign already in message
        return f"{cs}: {text}"   # prepend for identification

    def _send(self):
        text = self._entry.get().strip()
        if not text:
            return

        # Handle slash commands — no callsign needed
        if text.startswith('/'):
            self._entry.delete(0, tk.END)
            self._handle_command(text)
            return

        # Validate callsign before transmitting
        cs = self._validate_callsign()
        if cs is None:
            return

        # Build message with smart callsign prefix
        msg = self._build_tx_message(text, cs)
        self._entry.delete(0, tk.END)

        # Show in chat log immediately so operator has a record
        self._log('tx', msg)

        threading.Thread(target=self._tx_worker,
                         args=(msg,), daemon=True).start()

    def _expand_macro_vars(self, text: str, cs: str) -> str:
        """Replace all <VARIABLE> tokens with current station values."""
        si = self._station_info
        subs = {
            'CALL':   cs,
            'QTH':    si.get('qth',    ''),
            'GRID':   si.get('grid',   ''),
            'PWR':    si.get('pwr',    ''),
            'COUNTY': si.get('county', ''),
            'PARK':   si.get('park',   ''),
        }
        for key, val in subs.items():
            text = text.replace(f'<{key}>', val)
            text = text.replace(f'<{key.lower()}>', val)
        return text

    def _send_macro(self, text: str):
        """
        Send a macro message verbatim — no callsign modification.
        Operator is responsible for including their callsign in macros.
        Validates callsign is set before transmitting.
        """
        if not text:
            return

        cs = self._validate_callsign()
        if cs is None:
            return

        msg = self._expand_macro_vars(text, cs)

        self._log('tx', msg)

        threading.Thread(target=self._tx_worker,
                         args=(msg,), daemon=True).start()

    def _handle_command(self, cmd):
        c = cmd.lower().strip()
        if c in ('/cls', '/clear'):
            self._clear_chat()
        elif c == '/help':
            self._sys("Commands: /cls  /clear  /help")
        else:
            self._sys(f"Unknown command: {cmd}", 'warn')

    def _clear_chat(self):
        self._chat.configure(state=tk.NORMAL)
        self._chat.delete('1.0', tk.END)
        self._chat.configure(state=tk.DISABLED)
        self._sys("Chat cleared")

    def _tx_worker(self, msg: str):
        self._setstatus("Waiting for channel...")
        deadline = time.monotonic() + 15.0
        while time.monotonic() < deadline:
            if not self._dcd_active:
                break
            time.sleep(0.05)
        else:
            self._setstatus("TX cancelled — channel busy for 15s")
            return

        delay = tx_delay(TX_DELAY_MIN, TX_DELAY_MAX)
        self._setstatus(f"Channel clear — TX in {delay*1000:.0f}ms...")
        time.sleep(delay)

        if self._dcd_active:
            self._tx_worker(msg)
            return

        self._transmitting = True
        audio = self._mod.modulate_frame(msg)

        # Assert PTT before sending audio
        ptt_ok = self._ptt_on()
        if not ptt_ok and self._tci_connected:
            self._transmitting = False
            self._setstatus("TX aborted — PTT failed")
            self._sys("PTT failed — check TCI connection", 'warn')
            return

        # Use try/finally to GUARANTEE PTT releases even if something crashes
        try:
            # Small delay after PTT — radio needs time to switch to TX
            time.sleep(0.05)
            self._setstatus("Transmitting...")

            if AUDIO_AVAILABLE:
                self._audio.transmit(audio)
                # Use wall clock for timeout — buffer drains at HW rate
                # which may differ from modem rate
                tx_dur_s    = len(audio) / SAMPLE_RATE
                deadline_tx = time.monotonic() + tx_dur_s + 2.0
                while self._audio.is_transmitting():
                    remaining = self._audio.tx_duration_remaining()
                    if time.monotonic() > deadline_tx and remaining < 0.1:
                        break
                    if time.monotonic() > deadline_tx + 3.0:
                        self._sys("TX timeout — forcing PTT release", 'warn')
                        break
                    self._update_tx_meter(audio[:min(2048, len(audio))] * self._audio.output_gain)
                    time.sleep(0.05)
            else:
                fname = os.path.join(HERE, f"rmfsk_{int(time.time())}.wav")
                int16 = (audio * 32767).astype(np.int16)
                wavfile.write(fname, SAMPLE_RATE, int16)
                self.after(0, lambda f=fname: self._sys(f"WAV written: {f}"))
                time.sleep(len(audio) / SAMPLE_RATE)

            # Small delay after audio — ensure last symbol transmits
            time.sleep(0.1)

        finally:
            # ALWAYS release PTT — even if an exception occurred above
            self._ptt_off()
            self._tx_meter_clear()
            self._transmitting = False

        dur = len(audio) / SAMPLE_RATE
        tci_str = f"  [{self._tci.frequency_display}]" \
                  if self._tci and self._tci.connected else ""
        self._setstatus(f"TX done — {len(msg)} chars in {dur:.1f}s{tci_str}")

    # ── Tick ──────────────────────────────────────────────────────────────────

    def _tick(self):
        if not self._running:
            return
        if self._transmitting:
            self._led_dcd.set('tx')
            self._led_tx.set('tx')
        elif self._dcd_active:
            self._led_dcd.set('busy')
            self._led_tx.set('idle')
        else:
            self._led_dcd.set('clear')
            self._led_tx.set('idle')
        self.after(100, self._tick)

    # ── Chat helpers ──────────────────────────────────────────────────────────

    def _log(self, kind, text):
        ts = time.strftime("%H:%M:%S")
        self._chat.configure(state=tk.NORMAL)
        self._chat.insert(tk.END, f"[{ts}] ", 'ts')
        self._chat.insert(tk.END, f"{text}\n", kind)
        self._chat.see(tk.END)
        self._chat.configure(state=tk.DISABLED)

    def _sys(self, text, level='sys'):
        self._log(level, f"  {text}")

    def _setstatus(self, text):
        self.after(0, lambda: self._status.set(text))

    # ── WAV Test ──────────────────────────────────────────────────────────────

    def _wav_test(self):
        cs   = self._callsign.get().strip().upper() or "NOCALL"
        msg  = f"CQ CQ DE {cs} HAVEN-FSK 500Hz TEST"
        path = os.path.join(HERE, "haven_fsk_test.wav")
        self._mod.write_wav(path, msg)
        self._sys(f"WAV written: {path}")
        messagebox.showinfo("WAV Test",
            f"Written to:\n{path}\n\n"
            f"Message:\n{msg}\n\n"
            f"Contains {len(SYNC_SYMS)}-symbol preamble "
            f"({PREAMBLE_LEN/SAMPLE_RATE*1000:.0f}ms) before the text.\n\n"
            "Load in fldigi or any spectrum analyzer\n"
            f"to see tones from {BASE_FREQ:.0f} to "
            f"{BASE_FREQ+NUM_TONES*SYMBOL_RATE:.0f} Hz.")

    # ── Play WAV ──────────────────────────────────────────────────────────────

    def _play_wav(self):
        path = filedialog.askopenfilename(
            title="Open WAV file to play and decode",
            initialdir=HERE,
            filetypes=[("WAV files", "*.wav"), ("All files", "*.*")],
        )
        if not path:
            return

        win = tk.Toplevel(self)
        win.title("Play WAV")
        win.configure(bg=BG)
        win.resizable(False, False)
        win.grab_set()

        tk.Label(win, text=f"File:  {os.path.basename(path)}",
                 bg=BG, fg=TEXT_FG, font=FONT_MONO).pack(padx=20, pady=(16,4))
        tk.Label(win, text="How do you want to receive it?",
                 bg=BG, fg=AMBER, font=FONT_UI).pack(pady=(4,12))

        def loopback():
            win.destroy()
            self._do_play_wav(path, loopback=True)

        def stereo():
            win.destroy()
            self._do_play_wav(path, loopback=False)

        btn = dict(bg=ACCENT, fg=GREEN, font=FONT_UI,
                   relief=tk.FLAT, padx=12, pady=6, width=30)

        tk.Button(win, text="Software loopback  (always works)",
                  command=loopback, **btn).pack(padx=20, pady=4)
        tk.Label(win,
                 text="Injects directly into decoder. No speakers.",
                 bg=BG, fg='#666688', font=("Helvetica", 9)).pack(
                 padx=28, pady=(0,10))

        tk.Button(win, text="Play through speakers  (needs Stereo Mix)",
                  command=stereo, **btn).pack(padx=20, pady=4)
        tk.Label(win,
                 text="Plays audio out loud. Requires Stereo Mix\n"
                      "or a virtual audio cable as input device.",
                 bg=BG, fg='#666688', font=("Helvetica", 9)).pack(
                 padx=28, pady=(0,16))

        tk.Button(win, text="Cancel", command=win.destroy,
                  bg=BG2, fg=TEXT_FG, font=FONT_UI,
                  relief=tk.FLAT, padx=12).pack(pady=(0,12))

    def _do_play_wav(self, path, loopback):
        try:
            rate, data = wavfile.read(path)
        except Exception as e:
            self._sys(f"Cannot read WAV: {e}", 'warn')
            return
        if data.ndim > 1:
            data = data[:, 0]
        if data.dtype == np.int16:
            audio = data.astype(np.float32) / 32768.0
        elif data.dtype == np.int32:
            audio = data.astype(np.float32) / 2147483648.0
        else:
            audio = data.astype(np.float32)
        dur  = len(audio) / rate
        mode = "software loopback" if loopback else "speakers"
        self._sys(f"Playing {os.path.basename(path)}  "
                  f"({rate}Hz, {dur:.1f}s)  [{mode}]")
        self._setstatus(f"Playing {os.path.basename(path)}...")
        if loopback:
            self._audio.inject_loopback(audio, rate)
        else:
            self._audio.play_wav(audio, rate)
        self.after(int(dur*1000)+500,
                   lambda: self._setstatus(
                       f"Playback complete — {os.path.basename(path)}"))

    # ── Decode WAV ────────────────────────────────────────────────────────────

    def _decode_wav(self):
        path = filedialog.askopenfilename(
            title="Open WAV file to decode",
            initialdir=HERE,
            filetypes=[("WAV files", "*.wav"), ("All files", "*.*")],
        )
        if not path:
            return
        threading.Thread(target=self._decode_wav_worker,
                         args=(path,), daemon=True).start()

    def _decode_wav_worker(self, path):
        try:
            rate, data = wavfile.read(path)
        except Exception as e:
            self.after(0, lambda: self._sys(f"Cannot read WAV: {e}", 'warn'))
            return
        fname = os.path.basename(path)
        self.after(0, lambda: self._sys(
            f"Decoding {fname}  ({rate}Hz, {len(data)/rate:.1f}s)..."))
        if data.ndim > 1:
            data = data[:, 0]
        if data.dtype == np.int16:
            audio = data.astype(np.float32) / 32768.0
        elif data.dtype == np.int32:
            audio = data.astype(np.float32) / 2147483648.0
        else:
            audio = data.astype(np.float32)
        if rate != SAMPLE_RATE:
            from scipy.signal import resample_poly
            from math import gcd
            g     = gcd(SAMPLE_RATE, rate)
            audio = resample_poly(audio, SAMPLE_RATE//g,
                                  rate//g).astype(np.float32)
            self.after(0, lambda: self._sys(
                f"  Resampled {rate} Hz -> {SAMPLE_RATE} Hz"))

        # Push to waterfall in chunks for visual feedback
        for i in range(0, len(audio), AUDIO_CHUNK):
            chunk = audio[i:i+AUDIO_CHUNK]
            self.after(0, lambda c=chunk: self._wf.push(c))
            time.sleep(AUDIO_CHUNK / SAMPLE_RATE * 0.3)

        text = decode_frame(audio)
        if text:
            self.after(0, lambda t=text: self._log('rx', f"[WAV] {t}"))
            self.after(0, lambda: self._setstatus(
                f"Decoded: {len(text)} chars"))
        else:
            self.after(0, lambda: self._sys(
                "No valid frame found — is this a HAVEN-FSK file?", 'warn'))
            self.after(0, lambda: self._setstatus("Decode: no frame found"))

    # ── Station Info ──────────────────────────────────────────────────────────

    def _show_station_info_dialog(self):
        """Settings dialog for station information used in macro substitution."""
        win = tk.Toplevel(self)
        win.title("Station Information")
        win.configure(bg=BG)
        win.resizable(False, False)
        win.grab_set()

        tk.Label(win, text="Station Information",
                 bg=BG, fg=AMBER,
                 font=("Helvetica", 11, 'bold')).pack(padx=20, pady=(14, 2))
        tk.Label(win,
                 text="These values are substituted into macros when transmitted.",
                 bg=BG, fg='#666688',
                 font=("Helvetica", 9)).pack(padx=20, pady=(0, 10))

        fields = [
            ('qth',    'QTH',            '<QTH>',    'City, State / Location'),
            ('grid',   'Grid Square',    '<GRID>',   'Maidenhead grid (e.g. EN61)'),
            ('pwr',    'Power',          '<PWR>',    'e.g. 100W'),
            ('county', 'County',         '<COUNTY>', 'County name for activations'),
            ('park',   'Park Reference', '<PARK>',   'POTA reference (e.g. K-1234)'),
        ]

        entry_vars = {}
        for key, label, token, hint in fields:
            row = tk.Frame(win, bg=BG)
            row.pack(fill=tk.X, padx=20, pady=3)

            tk.Label(row, text=f"{label}:", bg=BG, fg=TEXT_FG,
                     font=FONT_UI, width=14, anchor=tk.W).pack(side=tk.LEFT)

            var = tk.StringVar(value=self._station_info.get(key, ''))
            tk.Entry(row, textvariable=var, bg=TEXT_BG, fg=GREEN,
                     font=FONT_MONO, width=28,
                     insertbackground=GREEN).pack(side=tk.LEFT, padx=(0, 8))

            tk.Label(row, text=f"{token}  —  {hint}",
                     bg=BG, fg='#444466',
                     font=("Helvetica", 8)).pack(side=tk.LEFT)

            entry_vars[key] = var

        def save():
            for key, var in entry_vars.items():
                self._station_info[key] = var.get().strip()
            self._save_config()
            win.destroy()

        bf = tk.Frame(win, bg=BG)
        bf.pack(pady=12)
        tk.Button(bf, text="Save", command=save,
                  bg=ACCENT, fg=GREEN, font=FONT_UI,
                  relief=tk.FLAT, padx=14).pack(side=tk.LEFT, padx=6)
        tk.Button(bf, text="Cancel", command=win.destroy,
                  bg=BG2, fg=TEXT_FG, font=FONT_UI,
                  relief=tk.FLAT, padx=14).pack(side=tk.LEFT, padx=6)

    # ── Devices ───────────────────────────────────────────────────────────────

    def _show_sample_rate_dialog(self):
        """Settings dialog for audio sample rate selection."""
        win = tk.Toplevel(self)
        win.title("Sample Rate Settings")
        win.configure(bg=BG)
        win.resizable(False, False)
        win.grab_set()

        tk.Label(win, text="Audio Sample Rate",
                 bg=BG, fg=AMBER,
                 font=("Helvetica", 11, 'bold')).pack(padx=20, pady=(14,4))

        tk.Label(win,
                 text="Higher rates match modern USB radios natively.\n"
                      "Lower rates reduce CPU load on slow hardware.\n"
                      "Change takes effect on next program launch.",
                 bg=BG, fg=TEXT_FG, font=("Helvetica", 9),
                 justify=tk.LEFT).pack(padx=20, pady=(0,10))

        rates = [
            (48000, "48000 Hz — Default. Matches most USB radios and SDR VAC."),
            (44100, "44100 Hz — CD quality. Some older audio interfaces."),
            (22050, "22050 Hz — Half rate. Reduced CPU, narrower waterfall."),
            (16000, "16000 Hz — Low rate. Old hardware or Raspberry Pi."),
            ( 8000, " 8000 Hz — Minimum. Very slow hardware only."),
        ]

        sr_var = tk.IntVar(value=SAMPLE_RATE)
        for rate, desc in rates:
            row = tk.Frame(win, bg=BG)
            row.pack(fill=tk.X, padx=20, pady=2)
            tk.Radiobutton(row, variable=sr_var, value=rate,
                           bg=BG, fg=GREEN,
                           activebackground=BG,
                           activeforeground=GREEN,
                           selectcolor=BG2).pack(side=tk.LEFT)
            tk.Label(row, text=desc, bg=BG, fg=TEXT_FG,
                     font=("Helvetica", 9),
                     anchor=tk.W).pack(side=tk.LEFT, padx=(4,0))

        tk.Label(win,
                 text=f"Current: {SAMPLE_RATE} Hz  "
                      f"(waterfall 0–{SAMPLE_RATE//2} Hz)",
                 bg=BG, fg='#666688',
                 font=("Helvetica", 9)).pack(pady=(8,2))

        def apply():
            new_rate = sr_var.get()
            if new_rate == SAMPLE_RATE:
                win.destroy()
                return
            try:
                cfg = {}
                if os.path.exists(CONFIG_FILE):
                    with open(CONFIG_FILE) as f:
                        cfg = json.load(f)
                cfg['sample_rate'] = new_rate
                with open(CONFIG_FILE, 'w') as f:
                    json.dump(cfg, f, indent=2)
                messagebox.showinfo(
                    "Sample Rate Changed",
                    f"Sample rate set to {new_rate} Hz.\n\n"
                    "Restart HAVEN-FSK for the change to take effect.",
                    parent=win)
            except Exception as e:
                messagebox.showerror("Error",
                    f"Could not save setting: {e}", parent=win)
            win.destroy()

        btn_frame = tk.Frame(win, bg=BG)
        btn_frame.pack(pady=12)
        tk.Button(btn_frame, text="Apply & Close", command=apply,
                  bg=ACCENT, fg=GREEN, font=FONT_UI,
                  relief=tk.FLAT, padx=12).pack(side=tk.LEFT, padx=6)
        tk.Button(btn_frame, text="Cancel", command=win.destroy,
                  bg=BG2, fg=TEXT_FG, font=FONT_UI,
                  relief=tk.FLAT, padx=12).pack(side=tk.LEFT, padx=6)

    def _show_about(self):
        """About dialog."""
        messagebox.showinfo(
            "About HAVEN-FSK",
            f"HAVEN-FSK v{APP_VERSION}\n"
            f"16-tone MFSK HF Digital Mode\n"
            f"Emission designator: 500HJ2D\n\n"
            f"Modulation:   16-tone MFSK\n"
            f"Bandwidth:    500 Hz\n"
            f"Tone range:   500 – 968.75 Hz\n"
            f"Tone spacing: 31.25 Hz\n"
            f"Symbol:       32ms\n"
            f"FEC:          LDPC(192,96) rate 1/2\n"
            f"Net rate:     ~62 bps\n"
            f"Sample rate:  {SAMPLE_RATE} Hz\n\n"
            f"Author: WD9N\n"
            f"Specification: [GitHub — TBD]"
        )

    def _show_devices(self):
        if not AUDIO_AVAILABLE:
            messagebox.showwarning("No Audio",
                                   "sounddevice not available.\n"
                                   "pip install sounddevice")
            return
        devs     = AudioEngine.list_devices()

        # Build display strings showing name and native sample rate
        # For virtual cables, also show if rate needs resampling
        def dev_label(d):
            rate    = d['rate']
            resamp  = '' if rate == SAMPLE_RATE else f'  [resample from {rate}Hz]'
            default = ' ◀ default' if d['index'] == sd.default.device[0] else ''
            return f"[{d['index']}] {d['name']}  {rate}Hz{resamp}{default}"

        inp      = [dev_label(d) for d in devs if d['inputs']  > 0]
        out      = [dev_label(d) for d in devs if d['outputs'] > 0]
        inp_devs = [d for d in devs if d['inputs']  > 0]
        out_devs = [d for d in devs if d['outputs'] > 0]

        win = tk.Toplevel(self)
        win.title("Audio Device Selection")
        win.configure(bg=BG)
        win.geometry("620x420")

        in_lb = out_lb = None
        for label, items in [("Input  (RX — from radio / Stereo Mix)", inp),
                              ("Output (TX — to radio)",                out)]:
            tk.Label(win, text=label, bg=BG,
                     fg=AMBER, font=FONT_UI).pack(
                     anchor=tk.W, padx=10, pady=(8,2))
            lb = tk.Listbox(win,
                            listvariable=tk.StringVar(value=items),
                            selectmode=tk.SINGLE,
                            bg=TEXT_BG, fg=TEXT_FG,
                            font=("Courier", 9), height=5,
                            selectbackground=ACCENT)
            lb.pack(fill=tk.X, padx=10)
            if in_lb is None:
                in_lb = lb
            else:
                out_lb = lb

        def apply():
            if in_lb and in_lb.curselection():
                self._audio.in_device  = inp_devs[in_lb.curselection()[0]]['index']
            if out_lb and out_lb.curselection():
                self._audio.out_device = out_devs[out_lb.curselection()[0]]['index']
            self._audio.stop()
            time.sleep(0.3)
            ok, msg = self._audio.start()
            if ok:
                self._sys(f"Audio restarted — {msg}")
                # Update device status labels in toolbar
                self._update_device_labels()
            else:
                self._sys(f"Audio restart failed: {msg}", 'warn')
                self._sys("Check that Thetis/SDR software is running "
                          "and the virtual cable is active", 'sys')
            win.destroy()

        def test_selected():
            """Try to open selected devices and report what works."""
            in_idx  = self._inp_devs[in_lb.curselection()[0]]['index'] \
                      if in_lb and in_lb.curselection() else None
            out_idx = self._out_devs[out_lb.curselection()[0]]['index'] \
                      if out_lb and out_lb.curselection() else None
            results = []
            for rate in [8000, 16000, 22050, 44100, 48000, 96000]:
                # Test input
                try:
                    s = sd.InputStream(samplerate=rate, blocksize=512,
                                       device=in_idx, channels=1,
                                       dtype=np.float32)
                    s.close()
                    in_ok = f"IN:{rate}Hz OK"
                except Exception as e:
                    in_ok = f"IN:{rate}Hz FAIL"
                # Test output
                try:
                    s = sd.OutputStream(samplerate=rate, blocksize=512,
                                        device=out_idx, channels=1,
                                        dtype=np.float32)
                    s.close()
                    out_ok = f"OUT:{rate}Hz OK"
                except Exception:
                    out_ok = f"OUT:{rate}Hz FAIL"
                results.append(f"{in_ok}   {out_ok}")
            messagebox.showinfo("Device Test Results",
                                "\n".join(results))

        btn_frame = tk.Frame(win, bg=BG)
        btn_frame.pack(pady=8)

        tk.Button(btn_frame, text="Test Selected Devices",
                  command=test_selected,
                  bg=BG2, fg=AMBER, font=FONT_UI,
                  relief=tk.FLAT, padx=8).pack(side=tk.LEFT, padx=4)

        tk.Button(btn_frame, text="Apply & Restart Audio",
                  command=apply,
                  bg=ACCENT, fg=GREEN, font=FONT_UI,
                  relief=tk.FLAT, padx=10).pack(side=tk.LEFT, padx=4)

        tk.Label(win,
                 text="Tip: If using Thetis or SmartSDR, start the SDR software first.\n"
                      "Virtual cables appear here once the SDR is running.",
                 bg=BG, fg='#666688',
                 font=("Helvetica", 9)).pack(pady=(0, 8))

    # ── Config persistence ───────────────────────────────────────────────────

    def _load_config(self) -> dict:
        """Load settings from config file. Returns defaults if not found."""
        defaults = {
            'callsign':    'NOCALL',
            'in_device':   None,
            'out_device':  None,
            'rx_gain':     1.0,
            'tx_gain':     0.8,
            'dcd_thresh':   12.0,
            'show_snr':     False,
            'arq_auto':     False,
            'sample_rate':  48000,
            'macros':       [list(m) for m in DEFAULT_MACROS],
            'station_info': {'qth': '', 'grid': '', 'pwr': '',
                             'county': '', 'park': ''},
        }
        try:
            if os.path.exists(CONFIG_FILE):
                with open(CONFIG_FILE, 'r') as f:
                    saved = json.load(f)
                defaults.update(saved)
        except Exception:
            pass
        return defaults

    def _save_config(self):
        """Save current settings to config file."""
        try:
            config = {
                'callsign':    self._callsign.get().strip().upper(),
                'in_device':   self._audio.in_device,
                'out_device':  self._audio.out_device,
                'rx_gain':     self._rx_gain,
                'tx_gain':     self._tx_gain,
                'dcd_thresh':  DCD_THRESH,
                'show_snr':    False,
                'arq_auto':    False,
                'sample_rate':  SAMPLE_RATE,
                'macros':       self._macros,
                'station_info': self._station_info,
            }
            with open(CONFIG_FILE, 'w') as f:
                json.dump(config, f, indent=2)
        except Exception:
            pass

    def _apply_saved_config(self):
        """Apply saved audio device selections after devices are populated."""
        in_dev  = self._config.get('in_device')
        out_dev = self._config.get('out_device')
        if in_dev is not None:
            self._audio.in_device  = in_dev
        if out_dev is not None:
            self._audio.out_device = out_dev
        if in_dev is not None or out_dev is not None:
            threading.Thread(target=self._restart_audio,
                             daemon=True).start()

        # Apply saved gain values
        rx_gain = self._config.get('rx_gain', 1.0)
        tx_gain = self._config.get('tx_gain', 0.8)
        self._rx_gain = rx_gain
        self._tx_gain = tx_gain
        self._audio.output_gain = tx_gain
        if hasattr(self, '_rx_slider'):
            self._rx_slider.set(int(rx_gain * 100))
        if hasattr(self, '_tx_slider'):
            self._tx_slider.set(int(tx_gain * 100))

        # Now we know what devices are set — display them
        self._show_devices_in_combos()

    def _on_callsign_changed(self):
        """Called whenever the callsign field changes. Saves immediately."""
        cs = self._callsign.get().strip().upper()
        if cs and cs != 'NOCALL':
            self._save_config()


    # ── TCI radio control ─────────────────────────────────────────────────────

    def _init_tci(self):
        """Try to connect to TCI server. Non-blocking — runs in background."""
        try:
            from tci import TCIClient, PTTManager
            self._tci = TCIClient(
                url           = 'ws://localhost:40001',
                on_connect    = self._tci_on_connect,
                on_disconnect = self._tci_on_disconnect,
                on_status     = self._tci_on_status,
            )
            threading.Thread(target=self._tci_connect_loop,
                             daemon=True).start()
        except ImportError:
            self._tci = None

    def _tci_connect_loop(self):
        """Try TCI connection in background — retry every 10 seconds."""
        while self._running:
            if self._tci and not self._tci.connected:
                self._tci.connect(timeout=3.0)
            time.sleep(10)

    def _update_freq_display(self):
        """Refresh the digit labels from self._tci_frequency_hz."""
        hz = self._tci_frequency_hz
        if hz <= 0:
            for v in self._freq_digit_vars:
                v.set('-')
            return
        mhz_part = hz // 1_000_000
        khz_part = (hz % 1_000_000) // 1_000
        hz_part  = hz % 1_000
        digits = f"{mhz_part:02d}{khz_part:03d}{hz_part:03d}"
        for var, ch in zip(self._freq_digit_vars, digits):
            var.set(ch)

    def _freq_scroll(self, delta, step):
        """Mousewheel handler — tune frequency by step Hz per notch."""
        if not self._tci_connected or self._tci is None:
            return
        direction = 1 if delta > 0 else -1
        new_hz = self._tci_frequency_hz + direction * step
        new_hz = max(1_800_000, min(450_000_000, new_hz))
        self._tci_frequency_hz = new_hz
        self._update_freq_display()
        self._tci.set_frequency(new_hz)

    def _tci_on_connect(self):
        self._tci_connected = True
        from tci import PTTManager
        self._ptt = PTTManager(self._tci)
        # Update LED and status bar only — no chat spam
        self.after(0, lambda: self._led_tci.set('clear'))
        self.after(0, lambda: self._setstatus(
            f"TCI connected — {self._tci.frequency_display}  "
            f"{self._tci.mode}"))

    def _tci_on_disconnect(self):
        self._tci_connected = False
        self._ptt = None
        self._tci_frequency_hz = 0
        self.after(0, lambda: self._led_tci.set('idle'))
        self.after(0, self._update_freq_display)
        self.after(0, lambda: self._tci_mode.set('---'))
        self.after(0, lambda: self._setstatus("TCI disconnected"))

    def _tci_on_status(self, status: dict):
        """Update frequency and mode display from TCI status."""
        if status.get('frequency', 0) > 0:
            self._tci_frequency_hz = status['frequency']
            self.after(0, self._update_freq_display)
        if status.get('mode'):
            self.after(0, lambda: self._tci_mode.set(status['mode']))
        # Auto-fill callsign if radio has one set and ours is NOCALL
        cs = status.get('callsign', '')
        if cs and self._callsign.get() == 'NOCALL':
            self.after(0, lambda: self._callsign.set(cs))

    def _ptt_on(self):
        """Assert PTT via TCI if available."""
        if self._ptt:
            return self._ptt.ptt_on()
        return True   # No TCI — assume VOX or manual PTT

    def _ptt_off(self):
        """Release PTT via TCI if available."""
        if self._ptt:
            return self._ptt.ptt_off()
        return True

    # ── Quit ──────────────────────────────────────────────────────────────────

    def _quit(self):
        self._running = False
        # Save settings before closing
        self._save_config()
        # Always release PTT before closing
        if self._ptt:
            try:
                self._ptt.ptt_off()
            except Exception:
                pass
        if self._tci:
            try:
                self._tci.disconnect()
            except Exception:
                pass
        self._audio.stop()
        self.destroy()

if __name__ == "__main__":
    App().mainloop()
