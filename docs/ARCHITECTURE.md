# LORA Tunnel architecture and milestones

This describes **0.4.0-beta.1 source**, including the Cardputer ADV + U214
target and shared idle-only uploader. The published Heltec 0.3.0-beta.1 firmware
remains unchanged. Historical Heltec build/device evidence is versioned in
[VALIDATION.md](VALIDATION.md); Cardputer physical and live-service acceptance
is tracked separately in [CARDPUTER_RELEASE.md](CARDPUTER_RELEASE.md).
The version is centralized in `include/version.h`.

The application uses pinned PlatformIO/pioarduino, Arduino-ESP32 3.3.12,
LovyanGFX 1.2.24 on Heltec, M5Cardputer/M5Unified/M5GFX on Cardputer, NimBLE 2.5.2, RadioLib 7.8.1, TinyGPSPlus 1.1.0 and
ArduinoJson 6.21.5. The local `lib/recon` adapter imports the owner's
MIT-licensed Dragon Detector subset, retaining inherited notices; adjacent
`lib/opendroneid` contains the Apache-2.0 decoder pinned to
`849c03adbe0cdd68997a462fafab397edcfe53f3`. Exact source hashes and attribution
are retained in those libraries. SquachWatch supplies a feature/signature-fact
reference, not copied GPL application code.

Capture operates independently of a laptop, phone or Kismet host. Desktop
exporters consume SD logs after capture. LoRaPipe informs the raw-packet and
metadata separation; LoRaPipe/Kismet application code is not embedded.

## Modes and data flow

```mermaid
flowchart LR
  GNSS[GNSS UART / validity and age] --> Fix[Receiver fix snapshot]
  Drive[Wardriver / passive AP and BLE schedule] --> Q[Bounded observation queue]
  Recon[Recon / raw Wi-Fi and BLE schedule] --> Q
  LoRa[Wardriver only / fixed SX1262 receive profile] --> Q
  Fix --> Q
  Q --> Main[Main loop / classify and summarize]
  Main --> Cache[Board-sized bounded display summaries]
  Cache --> UI[Home / Wardriver / matched Recon / Settings]
  Main --> W[Board-sized writer queue]
  W --> Buffer[8 KiB SD write buffer]
  Buffer --> SD[Versioned JSONL sessions]
  SD --> Export[Offline WiGLE CSV / mesh candidate JSON]
  SD --> Upload[Stopped and saved only / per-service TLS uploader]
```

Wardriver observes 2.4 GHz APs, BLE advertisements and one LoRa profile.
Recon observes passive Wi-Fi/BLE evidence, filters enabled categories and stops
LoRa reception. A mode change closes the previous session; the user explicitly
starts the new mode. Home/Back navigation alone leaves capture running.
Session generation and mode are attached at admission so delayed callbacks
cannot silently become observations of a later session.

Wardriver preserves ordinary observations. Recon logs enabled-category matches
regardless of its display threshold; `recon.display_min` controls summaries/UI,
not that record-retention decision. Wi-Fi/BLE counters count admitted ingress
observations. `recon_total` counts enabled-category matches, including repeats.
The UI sees only eight recent eligible matches from its bounded summary cache.
Its category selector searches those eight, not historical SD records.

Confidence grades describe evidence. Vendor/name matches remain candidates;
iBeacon describes a packet format, Find My patterns do not uniquely identify
AirTags, and unauthenticated Remote ID broadcasts do not prove physical identity
or flight. Missing per-observation decoded fields remain missing. Raw evidence
and its transport are retained where available. See `RECON.md` for parser
boundaries and per-category qualifications.

## Hardware ownership and scheduling

Pins and allocation limits remain in `board_config.h`. On Heltec, TFT and SD share SPI3 under one mutex; SX1262 uses SPI2. The UI skips display work when the writer owns the bus. Touch polling
still occurs inside the UI path and can be delayed by SD contention. Physical
button polling and `ui::navigation()` require no SPI; actions are queued for the
main loop rather than performing SD/radio work inside UI callbacks. On Cardputer, M5GFX owns the separate display SPI bus while SD and U214 SX1262 share the peripheral SPI bus and mutex. ADV input uses the pinned M5Cardputer TCA8418 implementation; GNSS and cap power/RF-switch handling are board-specific.

The SX1262 interrupt only sets a flag. Packet read/rearm takes priority over
ordinary result processing. Fresh installations leave LoRa UNSET and disabled.
The first-run/Settings picker selects a hardware-compatible high-band Meshtastic LongFast
profiles from `lora_profiles.h`, with BW250, SF11, CR4/5, preamble 16, sync 0x2B
and LNA bypass. US is 906.875 MHz; EU_868 is 869.525 MHz. Lower-band hardware
profiles are listed but unavailable. A profile is not enabled until deliberately
selected, and changing it requires capture stopped and the logger idle.
See [REGIONS.md](REGIONS.md) for pinned sources and occupied-bandwidth checks.
It receives one frequency/modem configuration at a time. Missed RF or controller
packets cannot all be measured by software queue counters.

Wardriver uses passive SDK AP scans over US channels 1–11, default 120 ms per
channel, retaining at most 64 AP results per scan. Excess results count as
drops. Recon receives management and data frames with valid receive status,
removes the four-byte FCS, requires at least the 24-byte header, and uses
transmitter address 2. It retains up to 1,024 bytes with original length and
truncation state. This permits supported beacon/vendor and NAN parsing; an AP
summary alone cannot provide those bytes. Recon visits channels 1–11 in order,
at least 200 ms each: nominally a 2.2-second sweep, extended by main-loop delays
without skipping late channels. The configured BLE window follows, separated
by a 100 ms guard. Wi-Fi is not retuned during an SDK scan/connection attempt.

BLE remains passive, default 2.5-second windows with 100 ms interval / 80 ms window,
duplicate callbacks and no retained NimBLE result list. Passive scanning does
not request scan responses, so some names/services remain unavailable.
Bluetooth Classic, 5 GHz Wi-Fi and simultaneous coverage of every BLE PHY are
not claimed. The scanner uses the legacy path; extended/coded reception is not
enabled. Scan dwell, controller loss, UI latency and combined coverage must
be measured with controlled sources.

## Bounded processing, logs and location

Observation records contain no dynamic strings or pointers. Raw storage is
bounded to 1,024 bytes per record; the Recon API accepts at most 1,024 Wi-Fi bytes
or 512 BLE bytes, and LoRa retains at most 255. BLE retains its ADV/SCAN_RSP
boundary when supplied by the scanner. Truncation/unsupported framing must not
produce a claim that a complete packet was decoded. Heltec uses 48 ingress records with eight reserved management slots and 32 writer records in PSRAM. Cardputer uses 12 ingress records with two reserved management slots and eight writer records in internal RAM. Both allocate fixed callback scratch records at initialization. Deliberate capacity drops increment ingress counters; queue control/task state stay internal. No large allocation is permitted in radio callbacks.

The main loop maintains 64 least-recently-seen summaries on Heltec and 32 on Cardputer. Eviction removes only
the display summary, not observations already logged. Public and randomized
BLE address types remain distinct. A LoRa candidate sender is prefixed `?`;
unknown packets use a packet fingerprint, not an invented node identity.
Changing signal/location and later sightings remain observations even when the
display suppresses duplicate rows.

The logger uses an 8 KiB write buffer and fixed 8 KiB JSON document, flushing at
least every 2 seconds or when full and closing on Stop. A failed write marks
storage unavailable and conservatively counts the buffered records as dropped.
It cannot prove what a partially successful card write committed. Interrupted
sessions keep their existing file; new sessions receive new random filenames.
Queue saturation, malformed lengths, radio/storage errors, UI skips and maximum
flush duration are counted. SD-driver stalls/watchdog behavior still require
physical testing.

GNSS positions always describe the receiver. Invalid/stale fixes become null;
zero coordinates are not fabricated. UTC is accepted only from a GNSS sentence
no more than 1.5 seconds old, with its age/basis retained. Monotonic time always
exists. AP-scan timestamps describe result delivery, not each beacon's precise
arrival. Raw-frame timestamps have their own observation basis. Remote ID
aircraft and takeoff/fixed/live operator coordinates remain separate from
receiver GPS; future decoded mesh-node positions must also stay separate.
Relayed mesh packets do not establish that the original sender is nearby.

The 0.2.1 diagnostic parser retains complete GSV groups for five supported
talkers (GP, GB, BD, GL and GN). It checks sequence consistency and keeps the
previous completed group, with its age, while a new group is incomplete. Each
report carries visible-satellite count, signal ID, maximum C/N0, present C/N0
count and positive C/N0 count; missing and zero values remain distinct. Talker
or signal reports are not a deduplicated satellite count. Exact `ANTENNA OK`
and `ANTENNA OPEN` TXT status messages are counted separately from unknown
text. These diagnostics retain no coordinates and do not alter fix validity.
The historical 0.2.1 bench received complete groups and `ANTENNA OK` without a
fix. The later 0.2.3-dev physical check verified GGA quality 1, RMC A, GSA type 3
and a 341 ms-old location. Neither diagnostic history nor this successful sample
establishes long-duration GPS reliability or SD-file geotag correctness.

## UI, controls and SD configuration

The Heltec presentation path uses one 153,600-byte PSRAM framebuffer,
attached to the LovyanGFX sprite without a second full-frame copy. Drawing is
offscreen, followed by one synchronous transfer. Status refreshes at 2 Hz unless
input marks the screen dirty; keyboard/evidence pages are otherwise static.
The user confirmed 0.1.2 fixed the reported flicker and subsequently confirmed
that the distinct 0.2.0 Wardriver/Recon views and clean radio logo look right.
Full input coverage, button operation and drive-duration behavior remain
separate physical checks.

If framebuffer allocation fails, a visible direct-display fallback uses local
updates and clears on page transitions. Physical controls have 30 ms debounce,
a 700 ms USER long-press, no automatic repeat, boot-held suppression and wrap-safe
timing. IO cycles focus, USER selects and long USER goes Back. Touch remains
available when `ui.buttons_enabled=false`. GPIO46 electrical levels remain a
fitted-board check despite passing the portable button tests.

SD configuration/rules are bounded to 16 KiB each, with at most 64 valid custom
rules. Built-in detection works with the shipped empty rule list. Runtime
rules support exact names, SSID prefixes, OUIs, BLE service IDs and manufacturer
IDs; the 201-row vendor CSV is an editable reference, not a runtime importer.
Random/local addresses never supply vendor-prefix evidence.

Defaults: `ui.buttons_enabled=true`, `recon.buzzer=true`,
`recon.display_min=low`, `recon.alert_min=medium`; omitted categories enable all
and an empty array disables all. Alerts require enabled-category evidence at
the alert threshold and an allowed winning rule, with a 30-second retained
identity cooldown and one-second global cooldown. UI mute changes RAM; reboot
reloads the SD setting. Credential storage/keyboard replacement uses checked
temporary-file promotion and preserves a valid backup. Existing damaged radio
configuration with no valid backup disables LoRa. Missing configuration uses a
saved NVS region when present; otherwise it stays UNSET. An explicit SD file
wins over NVS, including explicit UNSET. Region saves preserve credentials and
unrelated valid fields, check the SD write first when mounted, and store only
the selected code in NVS. Successful Skip saves UNSET plus the SD
`selection_complete` marker, distinguishing an intentional LoRa-off choice
from unfinished setup. A mounted-card failure leaves RAM/NVS unchanged;
without a card, NVS readback must succeed. Legacy US settings retain their
frequency/modem values until another region is selected. The original VTS logo
is decoded into a small cached sprite once, then drawn in the header without
repeated PNG decoding. See [REGIONS.md](REGIONS.md) and [UI_GUIDE.md](UI_GUIDE.md).

Cardputer renders a compact 240 × 135 landscape interface in an 8-bit internal-RAM framebuffer. Physical keyboard events drive the same application actions, with shortcuts and native text entry. Its `ui_p95_ms`/`ui_max_ms` counters measure keyboard dispatch to presentation; they exclude TCA FIFO time and main-loop work before polling, so physical input latency still requires measurement.

## Stages and acceptance gates

| Stage | Source capability | Remaining acceptance/work |
| --- | --- | --- |
|1 Hardware | Verified configuration, display/touch/GNSS/SD and voltage/buzzer adapters | Actual PCB/FEM, independent battery boot, input corners/buttons, repeatable GPS/geotags, SD readback/resilience |
|2 Wardriver | Passive Wi-Fi/BLE schedule, fixed queues, JSONL and separate summaries | Controlled-source coverage, timing, dense environments, long drives |
|3 LoRa | Region-selected receive-only profile, raw metadata and candidate headers | First-run/persistence checks, known transmitter, LNA comparison, protocol confirmation and payload decode |
|4 Recon/UI | Distinct pages, bounded signatures/Remote ID, SD rules, evidence grades, optional buttons and alerts | Real category/transport comparisons, false positives, buttons, full touch coverage and UI/buzzer latency |
|5 Export/upload | Desktop exports plus 0.4 completed-session CSV staging, per-service TLS uploader and persistent receipts | Physical idle/capture handoff, real WiGLE/Wardrift/WDGWars acceptance, outage/reboot behavior |

USB status/start/stop is an optional bounded bench interface; startup does not
wait for it. Diagnostics avoid observed names, coordinates and credentials.
The serial helper verifies recent command state and fails on missing status or
restart/crash diagnostics, but cannot certify radio captures or SD content.

The 0.4 uploader uses only configured home Wi-Fi, completed Wardriver sessions, validated TLS, independent destination/chunk checkpoints and bounded retries. Each service is explicitly enabled or manually requested. It runs only after capture and saving stop; a new Start cancels it and waits for teardown before scanner ownership resumes. Auth failures and uncertain receipts pause automatic resubmission. Native logs survive successful and failed uploads. [UPLOADS.md](UPLOADS.md) describes receipt contracts and software-test versus live-service evidence.
After reliable fixed-profile reception, slow profile hopping can account for
airtime and traffic density. MeshCore and optional known-key Meshtastic payload
decoding remain later work; exported fields must never be fabricated.

## Settings shutdown and wake

Shutdown is a main-loop state transition requested only after the Settings
Power confirmation. It stops capture, snapshots storage/drop counters and
waits for the logger to close. A new storage error/drop for a session being
closed, unavailable storage for that session, or a 15-second save timeout
cancels shutdown while keeping the device awake. An already idle device can
sleep without an SD card; absence of a card is not itself a pending file.

After close, a separate 15-second `esp_timer` recovery guard restarts idle if
an upstream peripheral teardown blocks. Radios are stopped before the shared
bus is taken. Wake-source preparation must succeed with USER released; then
SD is unmounted, the GNSS UART ends and the hardware layer sleeps the display,
floats bus signals and holds switched peripheral power off before deep sleep.
The L76K backup rail remains supplied. Ext0 on active-low USER/GPIO0 wakes the
ESP32 into a fresh boot; a previous capture is not automatically resumed.
An error after radio teardown can require reboot before capture is available.

The physical POWER switch directly controls the expansion board battery latch,
without an MCU input; it cannot be repurposed as a guaranteed save-before-off
button. USB can supply the board independently. Software deep sleep is not a
battery disconnect or a measured zero-current state. Electrical/runtime
acceptance is listed in [TEST_CHECKLIST.md](TEST_CHECKLIST.md).

## Bounded diagnostic quiet state

The optional `tunnel quiet` USB command requires capture stopped and the logger idle. It stops radio work and sleeps the display/backlight while the loop continues GNSS polling and status commands. It does not change GNSS power, baud, constellation or output configuration. An independent `esp_timer` callback restarts the ESP32 after 90 seconds (`tunnel quiet-long` selects 300 seconds), including if radio shutdown stalls; `tunnel resume` requests an earlier restart. It is a bounded diagnosis path, not an operating mode or GPS fix. The installed beta's narrow comparison and automatic restart are recorded in [VALIDATION.md](VALIDATION.md).

Original application code is MIT licensed. Imported code and framework components retain their own license and source/relinking requirements; see [THIRD_PARTY.md](THIRD_PARTY.md) and [RELINKING.md](RELINKING.md).
