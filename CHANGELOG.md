# HAVEN-FSK Changelog

---

## v0.1.1-alpha — June 2026 *(current)*

### New Features

**Station bar**
- Callsign, Park, Summit, and Field Day class/section are now inline-editable
  fields directly in the toolbar — no dialog to open
- Park and Summit fields validate format on focus-out and turn red on invalid input
- Multi-park entry: comma-separated values in the Park field (e.g. `US-1234, US-5678`)

**Activity-aware logging**
- Activity auto-detected from station bar fields:
  Park filled → POTA Activation; Summit filled → SOTA Activation;
  FD class filled → Field Day; both Park + Summit → POTA/SOTA combo
- Hunting modes (POTA hunting, SOTA chasing) when activity selected but
  no park/summit entered
- General Chat when all activation fields are blank

**Integrated QSO logger**
- Inline contact entry in the log panel: Call, Band, RST sent/rcvd,
  P2P Park(s), S2S Summit, Notes, Log QSO button
- Auto-fills Call field from callsigns detected in received messages
- Band auto-populated from TCI frequency
- Right-click any recent QSO to open full edit dialog;
  Delete requires explicit confirmation
- Session saved to `Logs/` subfolder; offered to resume on next launch

**ADIF / Cabrillo export**
- POTA activation: one ADIF file per park, named `CALL@PARK-DATE-STATE.adi`
- Multi-park: P2P contacts with multi-park activators duplicated per their park
- SOTA activation: ADIF with `MY_SOTA_REF`
- POTA + SOTA combo: both files generated simultaneously
- Field Day: ADIF + Cabrillo (`.log`) generated together
- Field Day + POTA simultaneously supported

**Macro system**
- 17 variable tags: `<CALL>`, `<PARK>`, `<PARKS>`, `<SUMMIT>`, `<GRID>`,
  `<STATE>`, `<FDCLASS>`, `<FDSECTION>`, `<FDEXCHANGE>`, `<POWER>`,
  `<THEIRCALL>`, `<THEIRPARK>`, `<THEIRSUMMIT>`, `<BAND>`, `<FREQ>`,
  `<DATE>`, `<TIME>`
- Macro editor: tag-insertion buttons grouped by category, live preview
- `+ Add` button for new macros; Delete with confirmation in editor

**UDP external logger broadcast**
- Settings → External Logger: enable UDP broadcast to N1MM Logger+,
  Log4OM, Ham Radio Deluxe, or any WSJT-X compatible logger
- Format selectable: WSJT-X binary packet or plain ADIF text

**Frequency display**
- Seven-digit display: `MM.KKK.HHH MHz`
- Mousewheel on any digit tunes that digit's step (1 Hz to 10 MHz)
- Highlights digit under cursor

**UI layout**
- Waterfall immediately below station bar
- Chat log (Received) below waterfall
- Recent log panel with inline entry below chat
- Macro buttons between log panel and message input

### Bug Fixes
- Export dialog no longer freezes on Windows (removed `grab_set()` conflict
  with native file dialogs)
- `Launch HAVEN-FSK.bat` uses `%~dp0`-relative paths — works on any machine
  regardless of install location
- All dialog windows use `transient()` instead of `grab_set()` — eliminates
  multi-click latency on dialog buttons and confirmation popups

### Performance
- Waterfall rate-limited to 10 fps (was ~23 fps, flooding the event queue)
- Single `_master_tick` loop at 100ms replaces separate `_tick` and
  `_sync_combo_to_engine` loops
- Config saves only on focus-out, Return, and explicit user action —
  never on every keystroke
- Treeview incremental update: inserts/removes changed rows only,
  no full delete-and-rebuild on each log entry

### Visual
- TX LED is red during transmission (was amber)
- DCD LED: green = clear, red = signal/busy, red = transmitting
- TCI LED: green = connected to Thetis

---

## v0.1.0-alpha — June 2026

Initial public release to satisfy FCC Part 97 §97.309 disclosure requirement.
Software published prior to any on-air testing.

### Working at release
- 16-tone MFSK modulation and demodulation
- LDPC(192,96) rate-1/2 forward error correction
- CRC-16 frame integrity verification
- 512ms preamble detection and frame sync
- TCI WebSocket PTT control (Thetis/HPSDR)
- VAC audio engine with separate RX and TX streams
- Scrolling waterfall with adjustable bandwidth slider
- DCD carrier detect with listen-before-talk collision avoidance
- Level meters with RX/TX gain faders
- Callsign identification (FCC Part 97 compliant auto-prefix)
- Config file persistence

### Known Issues at v0.1.0
- VAC TX audio dropout under some conditions
- VAC loopback echo after PTT release
- DCD triggers on any passband energy, not HAVEN-FSK-specific tones
- No Hamlib/rigctld interface (TCI only)
- No waterfall click-to-tune / AFC

---

## Planned Features

- **Hamlib/rigctld** — PTT and CAT for TS-590SG, IC-705, FT-891, G90,
  and most non-SDR radios (next priority)
- **Signal-specific DCD** — discriminate HAVEN-FSK from FT8, RTTY, voice
  by checking tone energy at the 16 specific tone frequencies
- **Waterfall click-to-tune** — click a signal to set receive frequency;
  AFC fine-corrects within ±15 Hz
- **POTA spotting** — optional POST to pota.app after activation entry
- **SNR display** — optional dB reading on received messages
- **Multi-decoder** — simultaneous decode on multiple waterfall signals
- **VAC loopback holdoff** — suppress own TX echo after PTT release
