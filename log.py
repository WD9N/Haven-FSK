"""
HAVEN-FSK Log Manager
======================
QSO logging, ADIF export, session persistence, and smart detection
of callsigns and park references from decoded messages.
"""

import copy
from dataclasses import dataclass, field, asdict
from datetime import datetime, timezone
import json
import os
import re
import socket
import struct

# ── Detection patterns ────────────────────────────────────────────────────────
CALLSIGN_RE = re.compile(
    r'\b([A-Z]{1,2}[0-9][A-Z]{1,3}|[0-9][A-Z]{2,3}[0-9][A-Z]{1,3})\b'
)
PARK_RE          = re.compile(r'\b([A-Z]{1,2}-\d{4,5})\b')
_PARK_PARTIAL_RE = re.compile(r'^[A-Z]{0,2}(-\d{0,5})?$')   # still typing
_PARK_COMPLETE_RE= re.compile(r'^[A-Z]{1,2}-\d{4,5}$')       # fully formed


def is_valid_park_ref(val: str) -> bool:
    """True if val is empty, a valid partial, or a complete park reference."""
    return not val or bool(_PARK_PARTIAL_RE.match(val.strip().upper()))


def is_complete_park_ref(val: str) -> bool:
    """True only if val is a fully-formed park reference (e.g. US-1234)."""
    return bool(_PARK_COMPLETE_RE.match(val.strip().upper()))


def validate_park_list(raw: str) -> tuple:
    """
    Validate a comma-separated list of park references.
    Returns (all_ok, all_complete).
      all_ok       — every part is empty, partial, or complete (no red)
      all_complete — every non-empty part is a complete reference
    """
    if not raw.strip():
        return True, False
    parts = [p.strip() for p in raw.split(',') if p.strip()]
    ok    = all(is_valid_park_ref(p) for p in parts)
    done  = ok and bool(parts) and all(is_complete_park_ref(p) for p in parts)
    return ok, done

# ── Band map ──────────────────────────────────────────────────────────────────
BAND_MAP = [
    (1.8,    2.0,    '160M'),
    (3.5,    4.0,    '80M'),
    (5.3,    5.4,    '60M'),
    (7.0,    7.3,    '40M'),
    (10.1,   10.15,  '30M'),
    (14.0,   14.35,  '20M'),
    (18.068, 18.168, '17M'),
    (21.0,   21.45,  '15M'),
    (24.89,  24.99,  '12M'),
    (28.0,   29.7,   '10M'),
    (50.0,   54.0,   '6M'),
]

BAND_LIST = [b for _, _, b in BAND_MAP]


def freq_to_band(freq_mhz: float) -> str:
    for lo, hi, band in BAND_MAP:
        if lo <= freq_mhz <= hi:
            return band
    return ''


# ── Session ───────────────────────────────────────────────────────────────────
@dataclass
class Session:
    """Station/activation context passed to export functions."""
    station_callsign: str  = ''
    my_parks:         list = field(default_factory=list)  # POTA parks
    my_state:         str  = ''
    my_summit:        str  = ''    # SOTA summit ref
    my_gridsquare:    str  = ''
    tx_power:         str  = ''
    fd_class:         str  = ''
    fd_section:       str  = ''
    fd_power:         str  = 'LOW'
    entries:          list = field(default_factory=list)


# ── Validation ────────────────────────────────────────────────────────────────
def validate_station_info(info: dict) -> list:
    """Returns list of error messages. Empty list = valid."""
    errors = []
    grid = info.get('gridsquare', '')
    if grid and not re.match(r'^[A-R]{2}[0-9]{2}([A-X]{2})?$', grid.upper()):
        errors.append("Grid square format: 4 chars (EM69) or 6 chars (EM69ab)")
    for park in info.get('my_parks', []):
        if park and not re.match(r'^[A-Z]{1,2}-\d{4,5}$', park.strip().upper()):
            errors.append(f"Invalid park reference: {park}  (format: US-1234)")
    summit = info.get('my_summit', '')
    if summit and not re.match(r'^[A-Z0-9]+/[A-Z0-9]+-\d{3,4}$',
                               summit.strip().upper()):
        errors.append(f"Invalid summit: {summit}  (format: W9/IN-001)")
    fd_class = info.get('fd_class', '')
    if fd_class and not re.match(r'^\d[A-F]$', fd_class.strip().upper()):
        errors.append(f"Invalid FD class: {fd_class}  (format: 1E, 2A)")
    return errors


# ── LogEntry ──────────────────────────────────────────────────────────────────
@dataclass
class LogEntry:
    station_callsign: str = ''
    call:             str = ''
    qso_date:         str = ''     # YYYYMMDD UTC
    time_on:          str = ''     # HHMM UTC
    band:             str = ''
    freq:           float = 0.0
    mode:             str = 'MFSK'
    submode:          str = 'HAVEN-FSK'
    rst_sent:         str = '599'
    rst_rcvd:         str = '599'
    my_sig:           str = ''
    my_sig_info:      str = ''
    my_state:         str = ''
    sig:              str = ''
    sig_info:         str = ''
    comment:          str = ''
    my_gridsquare:    str = ''
    gridsquare:       str = ''
    tx_pwr:           str = ''
    name:             str = ''
    my_sota_ref:      str = ''    # MY_SOTA_REF — our summit
    sota_ref:         str = ''    # SOTA_REF    — their summit (S2S)
    activity:         str = 'General Chat'
    confirmed:       bool = False
    timestamp:      float = 0.0

    @property
    def is_s2s(self) -> bool:
        return bool(self.sota_ref)

    @property
    def is_p2p(self) -> bool:
        return bool(self.sig_info)

    @property
    def their_parks(self) -> list:
        return [p.strip() for p in self.sig_info.split(',') if p.strip()]


# ── ActivityManager ───────────────────────────────────────────────────────────
class ActivityManager:
    """
    Determines operating context from station info.
    POTA/SOTA activation vs hunting is auto-detected from whether parks/summit
    are filled in. Menu activity handles Field Day / General Contest distinction.
    """

    def __init__(self, session: Session, menu_activity: str = 'General Chat'):
        self.session       = session
        self.menu_activity = menu_activity

    @property
    def is_pota_activating(self) -> bool:
        return len(self.session.my_parks) > 0

    @property
    def is_pota_hunting(self) -> bool:
        return self.menu_activity == 'POTA' and not self.is_pota_activating

    @property
    def is_sota_activating(self) -> bool:
        return bool(self.session.my_summit)

    @property
    def is_sota_hunting(self) -> bool:
        return self.menu_activity == 'SOTA' and not self.is_sota_activating

    @property
    def is_combo(self) -> bool:
        return self.is_pota_activating and self.is_sota_activating

    @property
    def is_field_day(self) -> bool:
        return self.menu_activity == 'Field Day'

    @property
    def effective_activity(self) -> str:
        parts = []
        if self.is_pota_activating:
            parks = ', '.join(self.session.my_parks)
            parts.append(f"POTA Activating {parks}")
        elif self.is_pota_hunting:
            parts.append("POTA Hunting")
        if self.is_sota_activating:
            parts.append(f"SOTA Activating {self.session.my_summit}")
        elif self.is_sota_hunting:
            parts.append("SOTA Chasing")
        if self.is_field_day:
            cl = self.session.fd_class
            sec = self.session.fd_section
            parts.append(f"Field Day {cl} {sec}".strip())
        return ' + '.join(parts) if parts else 'General Chat'


# ── ADIF export ───────────────────────────────────────────────────────────────
def _adif_field(name: str, value) -> str:
    s = str(value)
    if not s:
        return ''
    return f'<{name}:{len(s)}>{s} '


def export_adif(entries: list, filepath: str,
                program_id: str = 'HAVEN-FSK',
                version:    str = '0.1.0-alpha') -> int:
    """Write confirmed log entries to an ADIF file. Returns count written."""
    confirmed = [e for e in entries if e.confirmed]
    now = datetime.now(timezone.utc).strftime('%Y%m%d %H%M%S')

    lines = [
        f'<PROGRAMID:{len(program_id)}>{program_id}',
        f'<PROGRAMVERSION:{len(version)}>{version}',
        '<ADIF_VER:5>3.1.6',
        f'<CREATED_TIMESTAMP:15>{now}',
        '<EOH>',
        '',
    ]

    for e in confirmed:
        rec = ''
        rec += _adif_field('STATION_CALLSIGN', e.station_callsign)
        rec += _adif_field('CALL',             e.call)
        rec += _adif_field('QSO_DATE',         e.qso_date)
        rec += _adif_field('TIME_ON',          e.time_on)
        rec += _adif_field('BAND',             e.band)
        rec += _adif_field('FREQ',             f'{e.freq:.4f}')
        rec += _adif_field('MODE',             e.mode)
        rec += _adif_field('SUBMODE',          e.submode)
        rec += _adif_field('RST_SENT',         e.rst_sent)
        rec += _adif_field('RST_RCVD',         e.rst_rcvd)
        if e.my_sig:
            rec += _adif_field('MY_SIG',       e.my_sig)
            rec += _adif_field('MY_SIG_INFO',  e.my_sig_info)
        if e.my_state:
            rec += _adif_field('MY_STATE',     e.my_state)
        if e.sig:
            rec += _adif_field('SIG',          e.sig)
            rec += _adif_field('SIG_INFO',     e.sig_info)
        if e.my_gridsquare:
            rec += _adif_field('MY_GRIDSQUARE', e.my_gridsquare)
        if e.gridsquare:
            rec += _adif_field('GRIDSQUARE',   e.gridsquare)
        if e.name:
            rec += _adif_field('NAME',         e.name)
        if e.comment:
            rec += _adif_field('COMMENT',      e.comment)
        if e.tx_pwr:
            rec += _adif_field('TX_PWR',       e.tx_pwr)
        rec += '<EOR>'
        lines.append(rec)

    with open(filepath, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))
    return len(confirmed)


def export_adif_pota_multi(entries: list, export_dir: str,
                           callsign: str, date_str: str,
                           program_id: str = 'HAVEN-FSK',
                           version:    str = '0.1.0-alpha') -> list:
    """
    Export a POTA activation to one ADIF file per YOUR park.

    Filename format: {callsign}@{park}-{date}.adi  e.g. WD9N@US-1234-20260619.adi
    my_sig_info may be comma-separated for multi-park activations.
    sig_info    may be comma-separated for P2P with multi-park activators.

    A P2P QSO where the other station is at parks K-0001 and K-0002 produces
    two records in each output file — one per their park, identical otherwise.

    Returns list of (filepath, record_count) for each file written.
    """
    confirmed = [e for e in entries if e.confirmed and e.my_sig == 'POTA']

    # Collect YOUR unique parks in order of first appearance
    my_parks, seen = [], set()
    for e in confirmed:
        for p in (e.my_sig_info or '').split(','):
            p = p.strip().upper()
            if p and p not in seen:
                my_parks.append(p)
                seen.add(p)

    if not my_parks:
        return []

    results = []
    for my_park in my_parks:
        filename = f"{callsign}@{my_park}-{date_str}.adi"
        filepath = os.path.join(export_dir, filename)

        park_entries = []
        for e in confirmed:
            their_parks = [p.strip() for p in (e.sig_info or '').split(',')
                           if p.strip()]
            if not their_parks:
                # Non-P2P: one record, no SIG / SIG_INFO
                entry = copy.copy(e)
                entry.my_sig_info = my_park
                entry.sig         = ''
                entry.sig_info    = ''
                park_entries.append(entry)
            else:
                # P2P: one record per their park
                for their_park in their_parks:
                    entry = copy.copy(e)
                    entry.my_sig_info = my_park
                    entry.sig         = 'POTA'
                    entry.sig_info    = their_park
                    park_entries.append(entry)

        export_adif(park_entries, filepath, program_id, version)
        results.append((filepath, len(park_entries)))

    return results


# ── Unified ADIF record builder ───────────────────────────────────────────────
def build_adif_record(entry: 'LogEntry', session: Session,
                      my_park: str = None) -> str:
    """
    Build one ADIF record string.
    my_park overrides session park when generating per-park files.
    """
    def f(name, value):
        v = str(value).strip() if value else ''
        return f'<{name}:{len(v)}>{v} ' if v else ''

    rec  = f('STATION_CALLSIGN', session.station_callsign)
    rec += f('CALL',             entry.call)
    rec += f('QSO_DATE',         entry.qso_date)
    rec += f('TIME_ON',          entry.time_on)
    rec += f('BAND',             entry.band)
    rec += f('FREQ',             f'{entry.freq:.4f}' if entry.freq else '')
    rec += f('MODE',             'MFSK')
    rec += f('SUBMODE',          'HAVEN-FSK')
    rec += f('RST_SENT',         entry.rst_sent or '599')
    rec += f('RST_RCVD',         entry.rst_rcvd or '599')

    park = my_park or (session.my_parks[0] if session.my_parks else '')
    if park:
        rec += f('MY_SIG',      'POTA')
        rec += f('MY_SIG_INFO', park)
    if session.my_state:
        rec += f('MY_STATE',    session.my_state)
    if session.my_summit:
        rec += f('MY_SOTA_REF', session.my_summit)
    if entry.is_p2p:
        rec += f('SIG',         'POTA')
        rec += f('SIG_INFO',    entry.sig_info)
    if entry.is_s2s:
        rec += f('SOTA_REF',    entry.sota_ref)
    if session.my_gridsquare:
        rec += f('MY_GRIDSQUARE', session.my_gridsquare)
    if entry.gridsquare:
        rec += f('GRIDSQUARE',  entry.gridsquare)
    if entry.name:
        rec += f('NAME',        entry.name)
    if entry.comment:
        rec += f('COMMENT',     entry.comment)
    if session.tx_power:
        rec += f('TX_PWR',      session.tx_power)

    rec += '<EOR>'
    return rec


def _adif_header(program_id: str = 'HAVEN-FSK',
                 version: str = '0.1.0-alpha') -> list:
    now = datetime.now(timezone.utc).strftime('%Y%m%d %H%M%S')
    return [
        f'<PROGRAMID:{len(program_id)}>{program_id}',
        f'<PROGRAMVERSION:{len(version)}>{version}',
        '<ADIF_VER:5>3.1.6',
        f'<CREATED_TIMESTAMP:15>{now}',
        '<EOH>',
        '',
    ]


def write_adif_file(records: list, session: Session, filepath: str,
                    program_id: str = 'HAVEN-FSK',
                    version: str = '0.1.0-alpha'):
    """Write a list of pre-built ADIF record strings to a file."""
    lines = _adif_header(program_id, version) + records
    with open(filepath, 'w', encoding='utf-8') as fh:
        fh.write('\n'.join(lines))


def export_cabrillo(session: Session, filepath: str):
    """Export Field Day log in Cabrillo format."""
    lines = [
        'START-OF-LOG: 3.0',
        f'CALLSIGN: {session.station_callsign}',
        'CONTEST: ARRL-FD',
        'CATEGORY-OPERATOR: SINGLE-OP',
        'CATEGORY-BAND: ALL',
        f'CATEGORY-POWER: {session.fd_power or "LOW"}',
        'CATEGORY-TRANSMITTER: ONE',
        'CATEGORY-STATION: FIXED',
        f'CATEGORY-OVERLAY: DIGITAL',
        '',
    ]
    my_exch = f'{session.fd_class} {session.fd_section}'.strip()
    for e in session.entries:
        if not e.confirmed or e.activity != 'Field Day':
            continue
        freq_khz = int(e.freq * 1000) if e.freq else 0
        date_fmt = (f'{e.qso_date[:4]}-{e.qso_date[4:6]}-{e.qso_date[6:]}'
                    if len(e.qso_date) == 8 else e.qso_date)
        their_exch = e.comment.strip() if e.comment else '1A DX'
        lines.append(
            f'QSO: {freq_khz:>6} DI {date_fmt} {e.time_on} '
            f'{session.station_callsign:<13} {my_exch:<6} '
            f'{e.call:<13} {their_exch:<6} 0'
        )
    lines.append('END-OF-LOG:')
    with open(filepath, 'w', encoding='utf-8') as fh:
        fh.write('\n'.join(lines))


def export_all(session: Session, activity_mgr: ActivityManager,
               export_folder: str) -> dict:
    """
    Generate all required export files based on active activities.
    Returns dict of {description: filepath} for display to operator.
    """
    os.makedirs(export_folder, exist_ok=True)
    date_str = datetime.now(timezone.utc).strftime('%Y%m%d')
    cs       = session.station_callsign
    files    = {}

    confirmed = [e for e in session.entries if e.confirmed]

    # ── POTA export ───────────────────────────────────────────────────────────
    if activity_mgr.is_pota_activating:
        for park in session.my_parks:
            records = []
            for e in confirmed:
                if e.is_p2p and len(e.their_parks) > 1:
                    for their_park in e.their_parks:
                        ec = copy.copy(e)
                        ec.sig_info = their_park
                        records.append(build_adif_record(ec, session,
                                                         my_park=park))
                else:
                    records.append(build_adif_record(e, session,
                                                     my_park=park))
            fname = f'{cs}@{park}-{date_str}'
            if session.my_state:
                fname += f'-{session.my_state}'
            fname += '.adi'
            fpath = os.path.join(export_folder, fname)
            write_adif_file(records, session, fpath)
            files[f'POTA {park}'] = fpath

    # ── SOTA export ───────────────────────────────────────────────────────────
    if activity_mgr.is_sota_activating:
        records = [build_adif_record(e, session) for e in confirmed]
        summit_safe = session.my_summit.replace('/', '-')
        fname = f'{cs}-SOTA-{summit_safe}-{date_str}.adi'
        fpath = os.path.join(export_folder, fname)
        write_adif_file(records, session, fpath)
        files['SOTA'] = fpath

    # ── Field Day export ──────────────────────────────────────────────────────
    if activity_mgr.is_field_day:
        fd_entries = [e for e in confirmed if e.activity == 'Field Day']
        if fd_entries:
            s2 = copy.copy(session)
            s2.entries = fd_entries
            records = [build_adif_record(e, s2) for e in fd_entries]
            fname_a = f'{cs}_FieldDay_{date_str}.adi'
            fpath_a = os.path.join(export_folder, fname_a)
            write_adif_file(records, s2, fpath_a)
            files['Field Day ADIF'] = fpath_a

            fname_c = f'{cs}.log'
            fpath_c = os.path.join(export_folder, fname_c)
            export_cabrillo(s2, fpath_c)
            files['Field Day Cabrillo'] = fpath_c

    # ── General / hunting fallback ────────────────────────────────────────────
    if not (activity_mgr.is_pota_activating or
            activity_mgr.is_sota_activating or
            activity_mgr.is_field_day):
        records = [build_adif_record(e, session) for e in confirmed]
        fname = f'{cs}_LOG_{date_str}.adi'
        fpath = os.path.join(export_folder, fname)
        write_adif_file(records, session, fpath)
        files['General Log'] = fpath

    return files


# ── UDP external logger broadcast ─────────────────────────────────────────────
def broadcast_qso_udp(entry: 'LogEntry', session: Session,
                      host: str = '127.0.0.1', port: int = 2333):
    """
    Broadcast QSO to external loggers via WSJT-X UDP packet.
    Compatible with N1MM, Log4OM, Ham Radio Deluxe.
    Non-blocking — failure silently ignored.
    """
    try:
        def f(name, value):
            v = str(value).strip() if value else ''
            return f'<{name}:{len(v)}>{v} ' if v else ''

        adif  = f('CALL',             entry.call)
        adif += f('QSO_DATE',         entry.qso_date)
        adif += f('TIME_ON',          entry.time_on)
        adif += f('BAND',             entry.band)
        adif += f('FREQ',             f'{entry.freq:.4f}' if entry.freq else '')
        adif += f('MODE',             'MFSK')
        adif += f('SUBMODE',          'HAVEN-FSK')
        adif += f('RST_SENT',         entry.rst_sent or '599')
        adif += f('RST_RCVD',         entry.rst_rcvd or '599')
        adif += f('STATION_CALLSIGN', session.station_callsign)
        adif += f('MY_GRIDSQUARE',    session.my_gridsquare)
        adif += f('TX_PWR',           session.tx_power)
        adif += '<EOR>'

        magic    = 0xadbccbda
        schema   = 2
        pkt_type = 12
        app_id   = b'HAVEN-FSK'
        adif_b   = adif.encode('utf-8')

        packet  = struct.pack('>III', magic, schema, pkt_type)
        packet += struct.pack('>I', len(app_id)) + app_id
        packet += struct.pack('>I', len(adif_b)) + adif_b

        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.sendto(packet, (host, port))
        sock.close()
    except Exception:
        pass


def broadcast_qso_simple_adif(entry: 'LogEntry', session: Session,
                               host: str = '127.0.0.1', port: int = 2333):
    """Plain ADIF text over UDP — preferred by Log4OM."""
    try:
        def f(name, value):
            v = str(value).strip() if value else ''
            return f'<{name}:{len(v)}>{v} ' if v else ''

        adif  = f('CALL',             entry.call)
        adif += f('QSO_DATE',         entry.qso_date)
        adif += f('TIME_ON',          entry.time_on)
        adif += f('BAND',             entry.band)
        adif += f('FREQ',             f'{entry.freq:.4f}' if entry.freq else '')
        adif += f('MODE',             'MFSK')
        adif += f('SUBMODE',          'HAVEN-FSK')
        adif += f('RST_SENT',         entry.rst_sent or '599')
        adif += f('RST_RCVD',         entry.rst_rcvd or '599')
        adif += f('STATION_CALLSIGN', session.station_callsign)
        adif += '<EOR>'

        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.sendto(adif.encode('utf-8'), (host, port))
        sock.close()
    except Exception:
        pass


# ── LogManager ────────────────────────────────────────────────────────────────
class LogManager:
    POTA_MIN = 10   # minimum QSOs for a valid POTA activation

    def __init__(self, log_dir: str):
        self.log_dir       = log_dir
        self.entries: list = []
        self._session_file = ''
        # UDP broadcast config — set by App after loading config
        self.udp_enabled   = False
        self.udp_host      = '127.0.0.1'
        self.udp_port      = 2333
        self.udp_format    = 'wsjtx'   # 'wsjtx' or 'adif'
        self._udp_session  = None      # Session object set by App

    # ── Session I/O ──────────────────────────────────────────────────────────
    def new_session(self) -> str:
        date = datetime.now(timezone.utc).strftime('%Y%m%d')
        self._session_file = os.path.join(
            self.log_dir, f'haven_fsk_session_{date}.json')
        return self._session_file

    def load_session(self, filepath: str) -> int:
        try:
            with open(filepath, 'r', encoding='utf-8') as f:
                data = json.load(f)
            self.entries = [LogEntry(**e) for e in data]
            self._session_file = filepath
            return len(self.entries)
        except Exception:
            return 0

    def save_session(self):
        if not self._session_file:
            return
        try:
            with open(self._session_file, 'w', encoding='utf-8') as f:
                json.dump([asdict(e) for e in self.entries], f, indent=2)
        except Exception:
            pass

    @staticmethod
    def find_today_session(log_dir: str) -> str:
        """Return path to today's session file, or '' if none exists."""
        date = datetime.now(timezone.utc).strftime('%Y%m%d')
        fname = f'haven_fsk_session_{date}.json'
        # Prefer Logs subfolder, fall back to root (legacy location)
        for folder in (log_dir, os.path.dirname(log_dir)):
            path = os.path.join(folder, fname)
            if os.path.exists(path):
                return path
            # Also check old naming convention
            old = os.path.join(folder, f'haven_fsk_log_{date}.json')
            if os.path.exists(old):
                return old
        return ''

    # ── Entry management ─────────────────────────────────────────────────────
    def add_entry(self, entry: LogEntry) -> LogEntry:
        entry.confirmed = True
        if not entry.timestamp:
            entry.timestamp = datetime.now(timezone.utc).timestamp()
        self.entries.append(entry)
        self.save_session()
        if self.udp_enabled and self._udp_session:
            if self.udp_format == 'adif':
                broadcast_qso_simple_adif(entry, self._udp_session,
                                          self.udp_host, self.udp_port)
            else:
                broadcast_qso_udp(entry, self._udp_session,
                                  self.udp_host, self.udp_port)
        return entry

    def delete_last(self) -> bool:
        if not self.entries:
            return False
        self.entries.pop()
        self.save_session()
        return True

    def delete_entry(self, index: int) -> bool:
        """Delete a specific entry by its index in the entries list."""
        if index < 0 or index >= len(self.entries):
            return False
        self.entries.pop(index)
        self.save_session()
        return True

    def update_entry(self, index: int, **kwargs) -> bool:
        """Update fields on a specific entry by index."""
        if index < 0 or index >= len(self.entries):
            return False
        for k, v in kwargs.items():
            if hasattr(self.entries[index], k):
                setattr(self.entries[index], k, v)
        self.save_session()
        return True

    def edit_last(self, **kwargs) -> bool:
        if not self.entries:
            return False
        for k, v in kwargs.items():
            if hasattr(self.entries[-1], k):
                setattr(self.entries[-1], k, v)
        self.save_session()
        return True

    def count(self) -> int:
        return len([e for e in self.entries if e.confirmed])

    def is_dupe(self, call: str, band: str) -> bool:
        """True if this call has already been worked on this band this session."""
        call = call.strip().upper()
        band = band.strip().upper()
        return any(e.call.upper() == call and e.band.upper() == band
                   for e in self.entries if e.confirmed)

    def export_adif(self, filepath: str) -> int:
        return export_adif(self.entries, filepath)

    # ── Detection helpers ─────────────────────────────────────────────────────
    @staticmethod
    def detect_callsigns(text: str) -> list:
        return CALLSIGN_RE.findall(text.upper())

    @staticmethod
    def detect_parks(text: str) -> list:
        return PARK_RE.findall(text.upper())

    @staticmethod
    def freq_to_band(freq_mhz: float) -> str:
        return freq_to_band(freq_mhz)

    @staticmethod
    def utc_now() -> tuple:
        """Returns (date YYYYMMDD, time HHMM) in UTC."""
        now = datetime.now(timezone.utc)
        return now.strftime('%Y%m%d'), now.strftime('%H%M')
