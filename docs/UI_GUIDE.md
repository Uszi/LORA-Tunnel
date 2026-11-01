# LORA Tunnel touchscreen and button guide

This describes the **0.2.2-beta.1** interface. Build, flash and scoped physical
results are recorded in `VALIDATION.md`. The user confirmed that the distinct
Wardriver/Recon views and clean radio-only logo look right on 0.2.0, following
the earlier 0.1.2 flicker fix. Complete touch coverage, physical buttons and
category-specific detection accuracy still need their own acceptance checks.

## Home and mode selection

The green radio-logo splash leads to **Home**, with **Wardriver**, **Recon** and
**Setup**. Wardriver is the primary capture mode. Selecting a mode opens its
controls; it does not automatically start a recording.

Changing to the other mode stops the old session and finishes its queued SD
writes. Start the new mode explicitly after saving completes. Returning Home,
opening Setup, or using Back alone does not stop a running capture. Home shows
which mode is running so it can be reopened.

The screen is prepared in a fixed 153,600-byte PSRAM RGB565 buffer and transferred
once per redraw. Status refreshes at up to 2 Hz; input requests an immediate
redraw. The keyboard and evidence detail are otherwise static. If that
allocation fails, `UI BUFFER LOW: DIRECT DISPLAY` identifies a direct-drawing
fallback. Its full clears occur at page transitions, with local updates and
focus-outline erasure between them.

## Wardriver

- **WiFi**, **BLE** and **LoRa** select the recent-observation view. Their numbers
  count admitted observations, including repeat sightings; they are not unique
  physical-device totals. Complete 32-bit counts fit on the tabs' second line.
- Four rows show recent names/identifiers, RSSI and retained sighting counts.
  These are summaries from a bounded cache, separate from timestamped logs.
- **START DRIVE** begins recording when initialization and storage checks pass.
  **STOP + SAVE** stops capture and finishes queued writes.
- **HOME** returns to mode selection; **SETUP** opens credentials and SD retry.
- The LoRa tab displays the fixed receive profile. The confirmed United States
  default is US LongFast, 906.875 MHz. LoRa metadata/raw bytes and candidate
  headers do not establish decoded Meshtastic identities or messages.

Summary eviction can reset a displayed sighting count but does not erase stored
observations. A randomized BLE address is an observed address, not a permanent
identity. A raw LoRa fingerprint and a candidate header prefixed `?` are not
authenticated node identities.

## Recon

Recon runs passive Wi-Fi/BLE observation with category classification. LoRa
reception is stopped in this mode. Ordinary unclassified scans do not fill the
Recon result list.

- **FILTER** cycles All, ALPR, Flock, Bodycam, iBeacon, Ring, Cameras, Drones,
  Find My, Trackers, Glasses, Acoustic, Serial, Radio Tools and Custom.
- The result area holds the latest **eight eligible matched summaries**, shown
  two per page. **< / >** move through this bounded recent window. A filter only
  searches that window; it is not a search of the full SD session.
- Each card shows its summary, category, evidence grade and available evidence.
  Tap a card to open a stable snapshot. **Back to Matches** returns to live
  results. No demonstration devices are inserted into the actual interface.
- **MATCHES** counts enabled-category matched observations, including repeat
  sightings. It can exceed the number visible under the current filter or
  display threshold.
- **MUTE / SOUND** changes the current buzzer setting in RAM. This does not
  rewrite the SD configuration; reboot reloads `recon.buzzer`.
- **START RECON / STOP + SAVE**, Home and Setup work like the Wardriver controls.

The grades **weak indicator**, **medium evidence** and **strong signature**
describe the observed evidence. They are not probability scores, proof of
recording, or proof of malicious use. A vendor prefix can match several product
families. An Apple company field alone does not establish an AirTag. Remote ID
positions are self-reported aircraft/operator data, separate from receiver GPS;
they do not establish the operator's current location or authenticate a sender.
See `RECON.md` for category-specific interpretation.

SD `recon.categories` controls which classifications are enabled.
`recon.display_min` defaults to `low` and affects displayed summaries.
Enabled-category matches are logged even below that display threshold.
`recon.alert_min` defaults to `medium`. Alerts also require the buzzer to be
enabled and the winning rule to allow an alert. They are limited to once per
30 seconds for a retained identity and no more than once per second globally.
Cache eviction bounds the retained identity cooldown history.

## Optional physical controls

Set `ui.buttons_enabled` in the SD configuration; it defaults to `true`.
Touch remains available when physical navigation is disabled.

- **IO:** move the visible focus to the next control, once per released press.
- **USER short press:** activate the focused control.
- **USER held about 700 ms:** go Back once. Releasing it does not also select.
- The first hardware action displays a white focus outline; a touch returns to
  ordinary touch control. Back takes priority over simultaneous button events.
- Keyboard Back cancels and clears the temporary entry. Setup returns to its
  opener; evidence returns to Recon; a capture page returns Home.

Both inputs use 30 ms debounce, without automatic repeat. Buttons held at
initialization are ignored until released. USER is active-low GPIO0; IO/KEY is
intended active-high GPIO46. The published mainboard/expansion resistor values
make the actual IO input level a physical verification item. Do not treat
passing software debounce tests as proof that the fitted switch is reliable.

## Status

**SD OK / SD !** reports storage availability. **FIX / NO FIX** reports a fresh,
valid receiver position; the GNSS detail line provides available acquisition
diagnostics. Receiving NMEA bytes or seeing satellites does not establish a fix.
Unavailable UTC is shown explicitly. Battery is an ADC-derived voltage, not an
unsupported percentage or charging-state estimate.

**DROP / ERR** expose queue/log loss and recorded storage/radio/malformed errors.
These counters accumulate since boot; session observation totals reset on Start.
A stopped session must finish saving before card removal or power-off. Home and
Back are navigation controls, not substitutes for Stop.

## Setup and keyboard

Five fields are available: WiGLE API name, WiGLE API token, WDGWars token,
Wi-Fi SSID and Wi-Fi password. Tapping a field opens a blank replacement entry;
stored secrets are never revealed.

The keyboard supports lower/upper case, digits, punctuation, space and deletion,
with at most 127 ASCII characters. Tokens/passwords are masked. **SAVE TO SD**
queues a save; success appears only after the write is checked. Saving requires
a stopped session and mounted SD. **CANCEL** or long USER clears the temporary
entry without saving. Saving an empty value clears that field.

Credentials are plaintext on the removable card as requested. Do not publish
the real configuration. Storing keys does **not** associate with Wi-Fi or
perform an upload in this milestone. Offline capture/export remains independent
of the upload services.

**RETRY SD** attempts to mount a card while capture is stopped. Reboot to load
configuration or custom rules edited externally. Built-in detectors work with
an empty `detection_rules.json`; optional runtime additions go in that JSON file.
The supplied vendor CSV is an evidence reference, not an automatically imported
runtime database.

## Bench checks

The USB console accepts `tunnel status`, `tunnel start`, and `tunnel stop` at
115200 baud. The bounded helper requires an explicitly confirmed port:

```powershell
python tools/bench_serial.py --port PORT --command status --seconds 10
```

Direct console commands `tunnel drive` and `tunnel recon` select modes without
starting the next session. `tunnel gps` reports bounded sentence/checksum/fix
diagnostics without coordinates. These additional commands are not options in
the helper's status/start/stop command selector.

In 0.2.1, `tunnel gps` also emits `LT SKY` reports for complete GSV sentence
groups by talker, including age, visible-satellite count, C/N0 field counts and
maximum C/N0. Incomplete or out-of-order groups do not replace the preceding
complete group; check its age. Missing C/N0 differs from a reported zero, and
talker/signal groups must not be summed as unique satellites. `ANTENNA OK` and
`ANTENNA OPEN` are parsed receiver status messages, not an independent RF-health
measurement or proof of a position fix. The installed 0.2.1 bench check received
complete groups and `ANTENNA OK`; it still reported no fix.

Replace `PORT` with the confirmed device port and use a Python environment with `pyserial` installed.

The helper clears old input, sends Start/Stop once, then polls status. Success
requires a complete, recent acknowledgment and the requested state; Stop waits
for `running=0 saving=0`. Missing/stale status, an unmet state, serial failures,
or observed reboot/crash diagnostics fail the command. Its output excludes
coordinates, observed names and credentials. Status success does not certify
touch alignment, radio coverage or SD file contents.

Check Home/mode transitions, all filters/pages, empty Recon results, evidence
snapshots, mute and restart persistence, every keyboard symbol, cancellation,
focus wrapping and disabled buttons. Test input while SD writes and scans are
busy. The confirmed 0.2.0 appearance does not complete those input checks or
establish drive-duration reliability. Physical button acceptance remains
pending, separate from the host state-machine and parser checks.
