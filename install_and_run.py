#!/usr/bin/env python3
"""
HAVEN-FSK Installer and Launcher
===================================
Double-click this file (or run: python install_and_run.py) to:
  1. Check your Python version
  2. Create a virtual environment so nothing pollutes your system
  3. Install all required packages automatically
  4. Download/create modem.py and haven_fsk.py in the same folder
  5. Launch the application

Works on Windows, Linux, and macOS.
Python 3.8 or newer required.

If you see a security warning on Windows, click "Run Anyway" —
this script only installs Python packages and creates files in
the HAVEN-FSK folder.
"""

import sys
import os
import platform
import subprocess
import shutil
import textwrap

# ── Minimum Python version ────────────────────────────────────────────────────
MIN_PYTHON = (3, 8)

# ── Packages to install ───────────────────────────────────────────────────────
REQUIRED_PACKAGES = [
    "numpy",
    "scipy",
    "sounddevice",
    "matplotlib",
    "ldpc",
    "websocket-client",
]

# ── Paths ─────────────────────────────────────────────────────────────────────
HERE       = os.path.dirname(os.path.abspath(__file__))
VENV_DIR   = os.path.join(HERE, "venv")
IS_WINDOWS = platform.system() == "Windows"
IS_LINUX   = platform.system() == "Linux"
IS_MAC     = platform.system() == "Darwin"

if IS_WINDOWS:
    VENV_PYTHON = os.path.join(VENV_DIR, "Scripts", "python.exe")
    VENV_PIP    = os.path.join(VENV_DIR, "Scripts", "pip.exe")
else:
    VENV_PYTHON = os.path.join(VENV_DIR, "bin", "python3")
    VENV_PIP    = os.path.join(VENV_DIR, "bin", "pip3")


# ── Console output helpers ────────────────────────────────────────────────────

def banner():
    print()
    print("=" * 60)
    print("  HAVEN-FSK — 16-tone MFSK HF Digital Mode")
    print("  Installer & Launcher")
    print("=" * 60)
    print()

def step(msg):
    print(f"  [ ] {msg}", end="", flush=True)

def ok(extra=""):
    print(f"\r  [+] {extra}" if extra else "\r  [+]", flush=True)

def warn(msg):
    print(f"\r  [!] {msg}", flush=True)

def err(msg):
    print(f"\r  [X] {msg}", flush=True)

def info(msg):
    print(f"      {msg}")


# ── Python version check ──────────────────────────────────────────────────────

def check_python():
    step("Checking Python version")
    v = sys.version_info[:2]
    if v < MIN_PYTHON:
        err(f"Python {v[0]}.{v[1]} found — need {MIN_PYTHON[0]}.{MIN_PYTHON[1]}+")
        info("Download Python from https://www.python.org/downloads/")
        info("Make sure to check 'Add Python to PATH' during install.")
        pause_and_exit(1)
    ok(f"Python {v[0]}.{v[1]}.{sys.version_info[2]}")


# ── Linux system packages ─────────────────────────────────────────────────────

def install_linux_deps():
    """On Linux, tkinter and PortAudio come from the system package manager."""
    if not IS_LINUX:
        return

    step("Checking Linux system packages")

    # Check what's missing
    missing = []

    try:
        import tkinter
    except ImportError:
        missing.append("python3-tk")

    # Check libportaudio
    result = subprocess.run(
        ["ldconfig", "-p"],
        capture_output=True, text=True
    )
    if "libportaudio" not in result.stdout:
        missing.append("libportaudio2")

    if not missing:
        ok("All system packages present")
        return

    warn(f"Need to install: {' '.join(missing)}")
    info("Trying apt-get (may ask for your password)...")

    try:
        subprocess.run(
            ["sudo", "apt-get", "install", "-y"] + missing,
            check=True
        )
        ok("System packages installed")
    except subprocess.CalledProcessError:
        warn("Could not auto-install system packages.")
        info("Please run manually:")
        info(f"  sudo apt-get install {' '.join(missing)}")
        info("Then re-run this installer.")
        pause_and_exit(1)
    except FileNotFoundError:
        warn("apt-get not found — not a Debian/Ubuntu system?")
        info("Please install these manually for your distro:")
        for p in missing:
            info(f"  {p}")
        pause_and_exit(1)


# ── Virtual environment ───────────────────────────────────────────────────────

def create_venv():
    if os.path.exists(VENV_PYTHON):
        step("Virtual environment")
        ok("already exists")
        return

    step("Creating virtual environment")
    try:
        subprocess.run(
            [sys.executable, "-m", "venv", VENV_DIR],
            check=True,
            capture_output=True
        )
        ok()
    except subprocess.CalledProcessError as e:
        err("Failed to create virtual environment")
        info(str(e))
        pause_and_exit(1)


# ── Package installation ──────────────────────────────────────────────────────

def install_packages():
    # Upgrade pip first — old pip has trouble with some binary wheels
    step("Upgrading pip")
    try:
        subprocess.run(
            [VENV_PYTHON, "-m", "pip", "install", "--upgrade", "pip"],
            check=True,
            capture_output=True
        )
        ok()
    except subprocess.CalledProcessError:
        warn("pip upgrade failed — continuing anyway")

    for pkg in REQUIRED_PACKAGES:
        step(f"Installing {pkg}")
        try:
            # Check if already installed first
            result = subprocess.run(
                [VENV_PYTHON, "-m", "pip", "show", pkg],
                capture_output=True, text=True
            )
            if result.returncode == 0:
                # Extract version
                for line in result.stdout.splitlines():
                    if line.startswith("Version:"):
                        ok(f"{pkg} {line.split()[-1]} already installed")
                        break
                continue

            subprocess.run(
                [VENV_PYTHON, "-m", "pip", "install", pkg],
                check=True,
                capture_output=True
            )
            ok()
        except subprocess.CalledProcessError as e:
            err(f"Failed to install {pkg}")
            info("Check your internet connection and try again.")
            info(str(e.stderr.decode() if e.stderr else ""))
            pause_and_exit(1)


# ── Write application files ───────────────────────────────────────────────────

def write_modem():
    dest = os.path.join(HERE, "modem.py")
    if os.path.exists(dest):
        step("modem.py")
        ok("already exists")
        return

    step("Writing modem.py")
    content = textwrap.dedent('''\
        """
        HAVEN-FSK Modem - modulator and demodulator
        16-tone MFSK with orthogonal tone spacing.
        """

        import numpy as np
        from scipy.signal import butter, sosfilt
        from scipy.io import wavfile
        import random
        import time

        SAMPLE_RATE       = 8000
        NUM_TONES         = 16
        SYMBOL_RATE       = 31.25
        BASE_FREQ         = 500.0
        BITS_PER_SYM      = 4
        SAMPLES_PER_SYMBOL = int(SAMPLE_RATE / SYMBOL_RATE)
        TONE_FREQS        = [BASE_FREQ + i * SYMBOL_RATE for i in range(NUM_TONES)]

        RAMP_SAMPLES = max(4, SAMPLES_PER_SYMBOL // 10)

        def _make_ramp():
            t = np.linspace(0, np.pi / 2, RAMP_SAMPLES)
            return np.sin(t)

        RAMP_UP   = _make_ramp()
        RAMP_DOWN = RAMP_UP[::-1]

        def _shape_symbol(samples):
            shaped = samples.copy()
            shaped[:RAMP_SAMPLES]  *= RAMP_UP
            shaped[-RAMP_SAMPLES:] *= RAMP_DOWN
            return shaped

        class Modulator:
            def __init__(self, sample_rate=SAMPLE_RATE, num_tones=NUM_TONES,
                         symbol_rate=SYMBOL_RATE, base_freq=BASE_FREQ):
                self.sample_rate  = sample_rate
                self.num_tones    = num_tones
                self.symbol_rate  = symbol_rate
                self.base_freq    = base_freq
                self.bits_per_sym = int(np.log2(num_tones))
                self.sps          = int(sample_rate / symbol_rate)
                self.tone_freqs   = [base_freq + i*symbol_rate
                                     for i in range(num_tones)]
                t = np.arange(self.sps) / sample_rate
                self._tone_table  = np.array([
                    np.sin(2*np.pi*f*t).astype(np.float32)
                    for f in self.tone_freqs
                ])

            def bytes_to_symbols(self, data):
                symbols = []
                for byte in data:
                    for shift in range(8 - self.bits_per_sym, -1, -self.bits_per_sym):
                        mask = (1 << self.bits_per_sym) - 1
                        symbols.append((byte >> shift) & mask)
                return symbols

            def modulate(self, data):
                symbols = self.bytes_to_symbols(data)
                chunks  = [_shape_symbol(self._tone_table[s].copy())
                           for s in symbols]
                if not chunks:
                    return np.array([], dtype=np.float32)
                audio = np.concatenate(chunks)
                peak  = np.max(np.abs(audio))
                if peak > 0:
                    audio = audio * (0.9 / peak)
                return audio

            def modulate_text(self, text):
                return self.modulate(text.encode("utf-8"))

            def write_wav(self, filename, data):
                audio    = self.modulate(data)
                int16    = (audio * 32767).astype(np.int16)
                wavfile.write(filename, self.sample_rate, int16)
                duration = len(audio) / self.sample_rate
                bps      = (len(data)*8)/duration if duration > 0 else 0
                print(f"WAV: {filename}")
                print(f"  {len(data)} bytes  {duration:.2f}s  {bps:.1f} bps  "
                      f"{self.num_tones} tones  {self.num_tones*self.symbol_rate:.0f} Hz BW")

        class Demodulator:
            def __init__(self, sample_rate=SAMPLE_RATE, num_tones=NUM_TONES,
                         symbol_rate=SYMBOL_RATE, base_freq=BASE_FREQ):
                self.sample_rate  = sample_rate
                self.num_tones    = num_tones
                self.symbol_rate  = symbol_rate
                self.base_freq    = base_freq
                self.bits_per_sym = int(np.log2(num_tones))
                self.sps          = int(sample_rate / symbol_rate)
                self.tone_freqs   = [base_freq + i*symbol_rate
                                     for i in range(num_tones)]
                self._tone_bins   = [
                    int(round(f / symbol_rate)) for f in self.tone_freqs
                ]

            def _detect_symbol(self, block):
                windowed = block * np.hanning(len(block))
                spectrum = np.abs(np.fft.rfft(windowed, n=self.sps))**2
                energies = np.array([spectrum[b] for b in self._tone_bins],
                                   dtype=np.float32)
                return int(np.argmax(energies)), energies

            def symbols_to_bytes(self, symbols):
                bps = self.bits_per_sym
                spb = 8 // bps
                out = []
                for i in range(0, len(symbols) - (spb-1), spb):
                    byte = 0
                    for j in range(spb):
                        byte = (byte << bps) | symbols[i+j]
                    out.append(byte)
                return bytes(out)

            def demodulate(self, audio):
                symbols    = []
                num_blocks = len(audio) // self.sps
                for i in range(num_blocks):
                    block    = audio[i*self.sps:(i+1)*self.sps]
                    sym, _   = self._detect_symbol(block)
                    symbols.append(sym)
                return self.symbols_to_bytes(symbols)

            def demodulate_soft(self, audio):
                num_blocks = len(audio) // self.sps
                soft = np.zeros((num_blocks, self.num_tones), dtype=np.float32)
                for i in range(num_blocks):
                    block      = audio[i*self.sps:(i+1)*self.sps]
                    _, energies = self._detect_symbol(block)
                    soft[i]    = energies
                return soft

            def demodulate_text(self, audio):
                raw = self.demodulate(audio)
                try:
                    return raw.decode("utf-8", errors="replace")
                except Exception:
                    return raw.decode("latin-1", errors="replace")

        class DCD:
            def __init__(self, sample_rate=SAMPLE_RATE, base_freq=BASE_FREQ,
                         num_tones=NUM_TONES, symbol_rate=SYMBOL_RATE,
                         threshold_db=6.0):
                self.sample_rate = sample_rate
                self.base_freq   = base_freq
                self.num_tones   = num_tones
                self.symbol_rate = symbol_rate
                self.threshold   = threshold_db
                self.sig_low     = base_freq - symbol_rate
                self.sig_high    = base_freq + num_tones*symbol_rate + symbol_rate
                self.noise_low   = max(100, self.sig_low - 300)
                self.noise_high  = self.sig_low - symbol_rate
                self._active     = False

            def update(self, audio_block):
                n        = len(audio_block)
                freqs    = np.fft.rfftfreq(n, d=1.0/self.sample_rate)
                spectrum = np.abs(np.fft.rfft(audio_block))**2
                sig_mask = (freqs >= self.sig_low) & (freqs <= self.sig_high)
                sig_e    = np.mean(spectrum[sig_mask]) if sig_mask.any() else 0
                n_mask   = (freqs >= self.noise_low) & (freqs <= self.noise_high)
                noise_e  = np.mean(spectrum[n_mask]) if n_mask.any() else 1e-10
                if noise_e > 0 and sig_e > 0:
                    db = 10 * np.log10(sig_e / noise_e)
                else:
                    db = 0
                self._active = db >= self.threshold
                return self._active

            @property
            def active(self):
                return self._active

        def tx_delay(min_ms=200, max_ms=2000):
            return random.uniform(min_ms, max_ms) / 1000.0
        ''')

    with open(dest, "w", encoding="utf-8") as f:
        f.write(content)
    ok()


def write_app():
    dest = os.path.join(HERE, "haven_fsk.py")
    if os.path.exists(dest):
        step("haven_fsk.py")
        ok("already exists")
        return

    step("Writing haven_fsk.py")

    # Read from current directory if already there from previous session
    src = os.path.join(HERE, "haven_fsk.py")
    if os.path.exists(src) and src != dest:
        shutil.copy2(src, dest)
        ok()
        return

    # Otherwise write a minimal placeholder that imports the real one
    # (In practice the user downloads both files together)
    content = textwrap.dedent('''\
        # haven_fsk.py is included in the download package.
        # If you only have install_and_run.py, download the full
        # package from the project page.
        print("Please download the full HAVEN-FSK package.")
        input("Press Enter to exit.")
        ''')
    with open(dest, "w", encoding="utf-8") as f:
        f.write(content)
    ok()


# ── Create launchers ──────────────────────────────────────────────────────────

def create_windows_launcher():
    """Create a .bat file for easy double-click launching on Windows."""
    if not IS_WINDOWS:
        return
    bat = os.path.join(HERE, "Launch HAVEN-FSK.bat")
    if os.path.exists(bat):
        return
    step("Creating Windows launcher (.bat)")
    content = textwrap.dedent(f"""\
        @echo off
        title HAVEN-FSK
        cd /d "%~dp0"
        "{VENV_PYTHON}" haven_fsk.py
        if errorlevel 1 (
            echo.
            echo HAVEN-FSK exited with an error.
            pause
        )
        """)
    with open(bat, "w", encoding="utf-8") as f:
        f.write(content)
    ok()


def create_linux_launcher():
    """Create a .sh file and desktop entry on Linux."""
    if not IS_LINUX:
        return

    # Shell script
    sh = os.path.join(HERE, "launch_haven_fsk.sh")
    if not os.path.exists(sh):
        step("Creating Linux launcher (.sh)")
        content = textwrap.dedent(f"""\
            #!/bin/bash
            cd "$(dirname "$0")"
            "{VENV_PYTHON}" haven_fsk.py
            """)
        with open(sh, "w", encoding="utf-8") as f:
            f.write(content)
        os.chmod(sh, 0o755)
        ok()

    # .desktop file for the application menu
    desktop_dir = os.path.expanduser("~/.local/share/applications")
    desktop     = os.path.join(desktop_dir, "haven_fsk.desktop")
    if not os.path.exists(desktop):
        step("Creating desktop entry")
        try:
            os.makedirs(desktop_dir, exist_ok=True)
            content = textwrap.dedent(f"""\
                [Desktop Entry]
                Name=HAVEN-FSK
                Comment=16-tone MFSK HF Digital Mode
                Exec={sh}
                Icon=utilities-terminal
                Terminal=false
                Type=Application
                Categories=HamRadio;Network;
                """)
            with open(desktop, "w", encoding="utf-8") as f:
                f.write(content)
            ok()
        except Exception:
            warn("Desktop entry skipped")


def create_mac_launcher():
    """Create a shell script launcher on macOS."""
    if not IS_MAC:
        return
    sh = os.path.join(HERE, "launch_haven_fsk.command")
    if os.path.exists(sh):
        return
    step("Creating macOS launcher (.command)")
    content = textwrap.dedent(f"""\
        #!/bin/bash
        cd "$(dirname "$0")"
        "{VENV_PYTHON}" haven_fsk.py
        """)
    with open(sh, "w", encoding="utf-8") as f:
        f.write(content)
    os.chmod(sh, 0o755)
    ok()


# ── Verify installation ───────────────────────────────────────────────────────

def verify():
    step("Verifying installation")
    try:
        result = subprocess.run(
            [VENV_PYTHON, "-c",
             "import numpy, scipy, sounddevice, matplotlib, tkinter; "
             "print('OK')"],
            capture_output=True, text=True, timeout=30
        )
        if "OK" in result.stdout:
            ok("All packages verified")
        else:
            raise RuntimeError(result.stderr)
    except Exception as e:
        err("Verification failed")
        info(str(e))
        pause_and_exit(1)


# ── Launch the app ────────────────────────────────────────────────────────────

def launch():
    app = os.path.join(HERE, "haven_fsk.py")
    if not os.path.exists(app):
        err("haven_fsk.py not found in the same folder as this installer.")
        info("Download the complete package and keep all files together.")
        pause_and_exit(1)

    print()
    print("  Starting HAVEN-FSK...")
    print()

    try:
        subprocess.run([VENV_PYTHON, app])
    except KeyboardInterrupt:
        pass
    except Exception as e:
        err(f"Launch failed: {e}")
        pause_and_exit(1)


# ── Utilities ─────────────────────────────────────────────────────────────────

def pause_and_exit(code=0):
    print()
    input("  Press Enter to exit...")
    sys.exit(code)


# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    banner()

    print(f"  System: {platform.system()} {platform.release()}")
    print(f"  Python: {sys.version.split()[0]}")
    print(f"  Folder: {HERE}")
    print()

    check_python()
    install_linux_deps()
    create_venv()
    install_packages()
    write_modem()
    write_app()
    create_windows_launcher()
    create_linux_launcher()
    create_mac_launcher()
    verify()

    print()
    print("  Installation complete.")
    print()

    if IS_WINDOWS:
        print('  Next time: double-click "Launch HAVEN-FSK.bat"')
    elif IS_LINUX:
        print("  Next time: run ./launch_haven_fsk.sh")
        print("             or find HAVEN-FSK in your applications menu")
    elif IS_MAC:
        print("  Next time: double-click launch_haven_fsk.command")

    print()

    launch()


if __name__ == "__main__":
    main()
