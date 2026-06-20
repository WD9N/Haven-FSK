"""
HAVEN-FSK TCI Client
======================
Connects to Thetis or ExpertSDR via the TCI (Transceiver Control Interface)
WebSocket protocol for PTT control and radio status.

TCI is an open WebSocket protocol originally developed by Expert Electronics.
Thetis implements it at ws://localhost:40001 by default.

In Thetis Setup → Serial/Network/Midi CAT → Network:
  - TCI Server must show "Server Running"
  - "Emulate ExpertSDR3 protocol" must be checked for TX audio to work

This module handles:
  - PTT on/off
  - Reading current VFO frequency
  - Reading current mode
  - Reading callsign from radio
  - Connection status monitoring
  - Automatic reconnection on disconnect

Usage:
    tci = TCIClient('ws://localhost:40001')
    tci.connect()
    tci.ptt_on(trx=0)
    tci.ptt_off(trx=0)
    freq = tci.frequency   # Hz
    mode = tci.mode        # e.g. 'DIGU'
    tci.disconnect()
"""

import threading
import time
import json
import queue
import re

try:
    import websocket
    WEBSOCKET_AVAILABLE = True
except ImportError:
    WEBSOCKET_AVAILABLE = False

# ── TCI command strings ───────────────────────────────────────────────────────
# TCI protocol uses plain text commands separated by semicolons

CMD_PTT_ON    = "trx:{trx},true;"       # PTT on  for transceiver {trx}
CMD_PTT_OFF   = "trx:{trx},false;"      # PTT off for transceiver {trx}
CMD_VFO_GET   = "vfo:{trx},{channel};"  # Request VFO frequency
CMD_READY     = "ready;"                # Tell server we are ready

# ── TCI Client ────────────────────────────────────────────────────────────────

class TCIClient:
    """
    WebSocket client for TCI protocol.
    Runs receive loop in a background thread.
    Thread-safe PTT control from any thread.
    """

    def __init__(self,
                 url:         str   = 'ws://localhost:40001',
                 trx:         int   = 0,
                 on_connect:  object = None,
                 on_disconnect: object = None,
                 on_status:   object = None):
        """
        url:           WebSocket URL of TCI server
        trx:           Transceiver index (0 = first/only radio)
        on_connect:    callback() called when connection established
        on_disconnect: callback() called when connection lost
        on_status:     callback(dict) called when radio status updates
                       dict keys: frequency, mode, callsign, ptt
        """
        if not WEBSOCKET_AVAILABLE:
            raise ImportError(
                "websocket-client not installed.\n"
                "Run: pip install websocket-client")

        self.url            = url
        self.trx            = trx
        self.on_connect     = on_connect
        self.on_disconnect  = on_disconnect
        self.on_status      = on_status

        # Radio state
        self.frequency      = 0        # Hz
        self.mode           = ''
        self.callsign       = ''
        self.ptt_active     = False
        self.connected      = False

        self._ws            = None
        self._thread        = None
        self._running       = False
        self._lock          = threading.Lock()
        self._ready         = threading.Event()

    # ── Connection management ─────────────────────────────────────────────────

    def connect(self, timeout: float = 5.0) -> bool:
        """
        Connect to TCI server. Returns True if connected within timeout.
        Starts background receive thread.
        """
        if not WEBSOCKET_AVAILABLE:
            return False

        self._running = True
        self._ready.clear()
        self._thread  = threading.Thread(
            target=self._run_loop, daemon=True)
        self._thread.start()

        # Wait for connection
        return self._ready.wait(timeout=timeout)

    def disconnect(self):
        """Disconnect from TCI server cleanly."""
        self._running = False
        if self.ptt_active:
            self.ptt_off()
        if self._ws:
            try:
                self._ws.close()
            except Exception:
                pass
        self.connected  = False
        self.ptt_active = False

    def reconnect(self, delay: float = 3.0):
        """Reconnect after a delay. Called automatically on disconnect."""
        time.sleep(delay)
        if self._running:
            self.connect()

    # ── PTT control ───────────────────────────────────────────────────────────

    def ptt_on(self, trx: int = None) -> bool:
        """
        Assert PTT — put radio into transmit.
        Returns True if command sent successfully.
        """
        if trx is None:
            trx = self.trx
        ok = self._send(f"trx:{trx},true;")
        if ok:
            self.ptt_active = True
        return ok

    def ptt_off(self, trx: int = None) -> bool:
        """
        Release PTT — return radio to receive.
        Returns True if command sent successfully.
        Always attempts to send even if not marked as transmitting.
        """
        if trx is None:
            trx = self.trx
        ok = self._send(f"trx:{trx},false;")
        self.ptt_active = False   # Mark as off regardless
        return ok

    # ── Radio control ─────────────────────────────────────────────────────────

    def set_frequency(self, freq_hz: int, trx: int = None) -> bool:
        """Set VFO frequency in Hz."""
        if trx is None:
            trx = self.trx
        return self._send(f"vfo:{trx},0,{freq_hz};")

    def set_mode(self, mode: str, trx: int = None) -> bool:
        """
        Set operating mode.
        Common modes: USB, LSB, CW, AM, FM, DIGU, DIGL
        DIGU = Digital USB (correct for our mode on HF)
        """
        if trx is None:
            trx = self.trx
        return self._send(f"modulation:{trx},{mode};")

    def request_status(self):
        """Request current radio status from server."""
        self._send("vfo:0,0;")
        self._send("modulation:0;")

    # ── Internal ──────────────────────────────────────────────────────────────

    def _send(self, command: str) -> bool:
        """Send a TCI command. Thread-safe."""
        if not self.connected or self._ws is None:
            return False
        try:
            with self._lock:
                self._ws.send(command)
            return True
        except Exception:
            self.connected = False
            return False

    def _run_loop(self):
        """Background WebSocket receive loop with auto-reconnect."""
        while self._running:
            try:
                self._ws = websocket.WebSocketApp(
                    self.url,
                    on_open    = self._on_open,
                    on_message = self._on_message,
                    on_error   = self._on_error,
                    on_close   = self._on_close,
                )
                self._ws.run_forever(ping_interval=20, ping_timeout=10)
            except Exception as e:
                pass

            if self._running:
                self.connected = False
                if self.on_disconnect:
                    try:
                        self.on_disconnect()
                    except Exception:
                        pass
                # Wait before reconnecting
                for _ in range(30):   # 3 second wait in 100ms steps
                    if not self._running:
                        break
                    time.sleep(0.1)

    def _on_open(self, ws):
        """Called when WebSocket connection is established."""
        self.connected = True
        # Send ready command to start receiving status updates
        try:
            ws.send(CMD_READY)
            ws.send("vfo:0,0;")         # Request current frequency
            ws.send("modulation:0;")    # Request current mode
        except Exception:
            pass
        self._ready.set()
        if self.on_connect:
            try:
                self.on_connect()
            except Exception:
                pass

    def _on_message(self, ws, message):
        """
        Parse incoming TCI messages and update radio state.
        TCI messages are plain text, semicolon terminated.
        Multiple messages may arrive in one WebSocket frame.
        """
        if not message:
            return

        # Split on semicolons — multiple commands can arrive together
        parts = [p.strip() for p in message.split(';') if p.strip()]

        status_changed = False

        for part in parts:
            part_lower = part.lower()

            # VFO frequency: vfo:trx,channel,frequency_hz
            if part_lower.startswith('vfo:'):
                try:
                    fields = part.split(':')[1].split(',')
                    if len(fields) >= 3:
                        self.frequency = int(fields[2])
                        status_changed = True
                except Exception:
                    pass

            # Mode: modulation:trx,mode
            elif part_lower.startswith('modulation:'):
                try:
                    fields = part.split(':')[1].split(',')
                    if len(fields) >= 2:
                        self.mode = fields[1].strip()
                        status_changed = True
                except Exception:
                    pass

            # PTT state: trx:trx,state
            elif part_lower.startswith('trx:'):
                try:
                    fields = part.split(':')[1].split(',')
                    if len(fields) >= 2:
                        state = fields[1].strip().lower()
                        self.ptt_active = state == 'true'
                        status_changed  = True
                except Exception:
                    pass

            # Callsign: own_callsign:callsign or start:callsign,...
            elif 'callsign' in part_lower or part_lower.startswith('start:'):
                try:
                    if ':' in part:
                        val = part.split(':', 1)[1].strip()
                        # start: message contains multiple fields
                        # callsign is typically the last field
                        if ',' in val:
                            fields = val.split(',')
                            for f in fields:
                                f = f.strip()
                                # Callsign pattern: letters and digits
                                if re.match(r'^[A-Z0-9]{3,8}$', f):
                                    self.callsign = f
                                    status_changed = True
                                    break
                        else:
                            if re.match(r'^[A-Z0-9/]{3,10}$',
                                        val.upper()):
                                self.callsign = val.upper()
                                status_changed = True
                except Exception:
                    pass

        if status_changed and self.on_status:
            try:
                self.on_status({
                    'frequency': self.frequency,
                    'mode':      self.mode,
                    'callsign':  self.callsign,
                    'ptt':       self.ptt_active,
                    'connected': self.connected,
                })
            except Exception:
                pass

    def _on_error(self, ws, error):
        """Called on WebSocket error."""
        self.connected = False
        self._ready.set()   # Unblock connect() timeout

    def _on_close(self, ws, close_status_code, close_msg):
        """Called when WebSocket closes."""
        self.connected  = False
        self.ptt_active = False
        self._ready.set()   # Unblock connect() timeout

    # ── Status properties ─────────────────────────────────────────────────────

    @property
    def frequency_mhz(self) -> float:
        return self.frequency / 1e6

    @property
    def frequency_display(self) -> str:
        """Formatted frequency string e.g. '14.074 MHz'"""
        if self.frequency == 0:
            return '---'
        mhz = self.frequency / 1e6
        return f"{mhz:.3f} MHz"

    @property
    def status_summary(self) -> str:
        if not self.connected:
            return "TCI: Not connected"
        return (f"TCI: {self.frequency_display}  "
                f"{self.mode}  "
                f"{'TX' if self.ptt_active else 'RX'}")

    def __repr__(self):
        return (f"TCIClient(url={self.url!r}, "
                f"connected={self.connected}, "
                f"freq={self.frequency_display}, "
                f"mode={self.mode!r})")


# ── PTT safety wrapper ────────────────────────────────────────────────────────

class PTTManager:
    """
    Safe PTT management with watchdog timer.
    Automatically releases PTT if software crashes or hangs.
    FCC regulations require the control operator to be able to
    immediately cease transmission — the watchdog enforces this.
    """

    WATCHDOG_TIMEOUT = 120   # seconds — max TX time before auto-release

    def __init__(self, tci: TCIClient):
        self._tci      = tci
        self._timer    = None
        self._tx_start = None

    def ptt_on(self) -> bool:
        """Assert PTT with watchdog timer."""
        if not self._tci.connected:
            return False
        ok = self._tci.ptt_on()
        if ok:
            self._tx_start = time.monotonic()
            self._start_watchdog()
        return ok

    def ptt_off(self) -> bool:
        """Release PTT and cancel watchdog."""
        self._cancel_watchdog()
        self._tx_start = None
        return self._tci.ptt_off()

    def _start_watchdog(self):
        self._cancel_watchdog()
        self._timer = threading.Timer(
            self.WATCHDOG_TIMEOUT, self._watchdog_fire)
        self._timer.daemon = True
        self._timer.start()

    def _cancel_watchdog(self):
        if self._timer:
            self._timer.cancel()
            self._timer = None

    def _watchdog_fire(self):
        """Emergency PTT release — called if TX runs too long."""
        self._tci.ptt_off()
        self._tx_start = None

    @property
    def tx_duration(self) -> float:
        """Seconds since PTT was asserted, or 0 if not transmitting."""
        if self._tx_start is None:
            return 0.0
        return time.monotonic() - self._tx_start


# ── Self-test ─────────────────────────────────────────────────────────────────

if __name__ == '__main__':
    print("TCI Client test — connecting to ws://localhost:40001")
    print("(Thetis must be running with TCI server enabled)")
    print()

    def on_connect():
        print("Connected to TCI server")

    def on_disconnect():
        print("Disconnected from TCI server")

    def on_status(s):
        print(f"  Frequency: {s['frequency']/1e6:.3f} MHz")
        print(f"  Mode:      {s['mode']}")
        print(f"  Callsign:  {s['callsign']}")
        print(f"  PTT:       {'TX' if s['ptt'] else 'RX'}")
        print()

    tci = TCIClient(
        on_connect    = on_connect,
        on_disconnect = on_disconnect,
        on_status     = on_status,
    )

    ok = tci.connect(timeout=5.0)
    if not ok:
        print("Could not connect — is Thetis running?")
        print("Check Setup → Serial/Network → Network → TCI Server Running")
    else:
        print(f"Status: {tci.status_summary}")
        print()
        print("Waiting 3 seconds for status updates...")
        time.sleep(3)
        print(f"Final status: {tci.status_summary}")
        tci.disconnect()
