# Practical acceptance checklist

The current source/build is 0.2.2-beta.1. Results are recorded individually below
and by version in `VALIDATION.md`: scoped recovery acquisition, verified
installation, USB startup, short mode/capture/Stop checks and limited user
appearance confirmation have evidence. Full-chip backup comparison failed;
scoped restoration remains untested. The user confirmed the 0.1.2 flicker fix
and the distinct 0.2.0 Wardriver/Recon views and clean radio logo. Physical
buttons, complete touch coverage, GNSS fix, known-source LoRa/Remote ID
comparisons, SD readback/recovery, battery operation and service uploads remain
pending. A successful build or host parser test does not establish physical
acceptance. Mark each result with firmware hash, board/expansion revision,
date, setup, expected result, measured result, and a pass/fail/pending status.
Use a copy of this checklist for each hardware revision.

## Baseline and recovery readiness

- [ ] Record actual mainboard and expansion silkscreens, GNSS/module marking,
  radio band and antenna; compare with `docs/HARDWARE.md`.
- [ ] Record stock version, boot screen, existing settings and functionality.
- [x] Identify the bootloader port: confirmed on 2026-11-01 by device identity
  and ESP32-S3 ROM response; recheck port identity before each resumed operation.
- [ ] Read the complete 16 MB flash twice, compare lengths and SHA256 hashes,
  and store private copies plus metadata using `docs/RECOVERY.md`.
  Two complete reads exist but differ; this recovery-readiness check has not passed.
- [x] Verify the scoped recovery alternative for this initial image: three fresh
  prefix reads through `0x372000` match, and two fresh coredump reads at `0xFF0000`
  match both old full-dump tails. Hashes and limitations are in `VALIDATION.md`.
  This verifies acquisition of the regions this prototype may change; restoration
  and the full-chip discrepancy remain untested/unresolved respectively.
- [ ] Copy the SD card separately. Retain a verified recovery path before any
  custom-firmware write; never confuse an app binary with a full flash backup.
- [ ] Record the exact firmware ELF/bin hashes and dependency versions used.

## Milestone 1: board and interface bring-up

- [x] Flash the intended V4-R8 TFT build only to the confirmed port; retain the
  flashing transcript and verification result. Record this as flashing evidence.
  0.1.1-dev factory SHA256 `fa1fe00b79b0a1884ae733e4dc63c55c59d94d763ce3097ea17885c5a5885810`;
  automatic and separate digest verification passed on the confirmed unit, 2026-11-01.
- [x] Confirm USB application startup, SD mount, responding touch controller,
  LoRa initialization and a short USB Start/Stop sequence. Approximately 54 seconds
  produced 113 Wi-Fi observations and 1,991 BLE advertisements, zero LoRa packets,
  zero reported drops/errors, and a completed logger flush/close. These narrow
  checks do not complete the broader peripheral/capture tests below.
- [ ] Check reported 16 MB flash and 8 MB PSRAM; run an actual PSRAM allocation
  and read/write check before treating detected memory as usable.
- [ ] Boot from USB, then cold boot from the battery with USB disconnected.
  Repeat power cycles and record failures; a serial-attached boot is insufficient.
- [ ] Verify green logo visibility, color order, portrait orientation and all
  text/button boundaries on the physical 240 x 320 display.
- [x] User confirmed the reported 0.1.1 flicker/pulsing was fixed by 0.1.2.
  This is a focused user observation, not acceptance of every display behavior.
- [x] User confirmed that the clean radio-only logo and distinct Wardriver/Recon
  views look right on the installed 0.2.0 build. This covers appearance only.
- [ ] Verify Home cards, full counter widths, Recon evidence/detail pages,
  category filters and empty-result messages through complete touch operation.
- [ ] Tap the four corners, edges and center; check coordinate alignment,
  release detection, long holds, and repeated taps without ghost activation.
- [ ] Exercise Start/Stop, Wi-Fi/BLE/LoRa, Recon and Settings navigation. Confirm
  missing touch/storage status is visible and does not falsely report readiness.
- [ ] Verify USER active-low GPIO0 and IO/KEY active-high GPIO46 on the fitted
  board. Measure IO idle/pressed voltage: the published R17/R47 values require
  checking rather than assuming a reliable digital-high level.
- [ ] Test IO focus cycling and USER Select/long Back on every page, including
  the keyboard. Test bounce, prolonged holds, release after Back, simultaneous
  buttons and boot-held inputs. One press must produce one action, with no
  Select following long Back and no automatic repeat.
- [ ] Set `ui.buttons_enabled=false`, reboot, and confirm touch still works
  while button events are ignored. Restore the setting and verify navigation.
- [ ] Confirm NMEA bytes at 9600 baud on ESP32 RX39/TX38. Outdoors, record time
  to first fix, actual UTC, satellites, HDOP, and receiver coordinates privately.
- [x] Installed 0.2.1 reported valid GNSS UART/checksum input, completed GP/BD/GL
  GSV groups and exact `ANTENNA OK` status. GP/GL groups contained positive C/N0
  reports, while receiver fix remained unavailable. Exact readings and timing
  are in `VALIDATION.md`; this does not pass the outdoor-fix check above.
- [x] Installed 0.2.2-beta.1 quiet comparison reported radio/display shutdown,
  continued valid GNSS input and no fix, then independently restarted after
  90 seconds. Normal startup returned ready with zero errors/drops. This narrow
  test does not identify a cause or complete GPS acceptance.
- [ ] Compare per-talker complete GSV groups and C/N0 against a receiver trace,
  including missing versus zero C/N0, incomplete/out-of-order groups, group age
  and multiple signal IDs. Do not sum talker/signal reports as unique satellites
  or treat an antenna status message as an independent RF measurement.
- [ ] Lose GNSS reception deliberately: coordinates must become null when
  invalid/stale; old fixes must retain age and never appear current. Reacquire
  and verify recovery without a restart. Check midnight UTC/date rollover.
- [ ] Mount a known-good SD card; create/read/export a session; compare the
  observed file contents with UI totals and sequence numbers.
- [ ] Check buzzer on/off and a bounded alert tone while GNSS is acquiring and
  while the display/SD are busy. Record timing artifacts or sustained tones.
- [ ] Compare displayed battery voltage against a meter at several charge
  levels, USB connected/disconnected and with no battery if supported. Do not
  claim calibrated state of charge from voltage alone.

## Milestone 2: Wi-Fi and BLE observation

- [ ] Use known 2.4 GHz APs on supported channels. Match BSSID, SSID, channel,
  frequency, security, RSSI and observation UTC against their configuration.
- [ ] Test a hidden SSID plus names containing commas, quotes and non-ASCII
  text. Check native JSONL and CSV escaping without altering the raw log.
- [ ] Use known BLE advertisements with a public address, a random address,
  names, 16/128-bit services and manufacturer data. Verify byte lengths and
  manufacturer ID endianness using a reference capture.
- [ ] Confirm random BLE addresses are visibly temporary; no OUI vendor claim
  comes from a random/local address. Address rotation must not be presented as
  certain sightings of one permanent physical device.
- [ ] Run Wi-Fi-only, BLE-only, and the scheduled combined mode for equal
  durations against the same controlled sources. Record sightings/minute,
  longest observation gap, UI latency, queue drops and scan errors for each.
- [ ] Record the time basis: Wi-Fi result-delivery time is not a precise beacon
  receive timestamp. Confirm GNSS age is checked at observation delivery.
- [ ] Ensure the mode performs observation only: no automatic associations,
  probe flood, BLE connections, or LoRa transmissions as capture side effects.

## Milestone 3: fixed-profile LoRa

- [x] The 12-second 0.2.0 Wardriver bench reported three received LoRa packets,
  with zero reported errors/drops. This is a narrow receive-count observation;
  known-source packet comparison and Meshtastic payload decoding remain open.
- [ ] Confirm country, 915 MHz hardware band, antenna and regional channel
  before enabling LoRa. Keep disabled when the regional/profile choice is unset.
- [ ] Record the known test transmitter's Meshtastic frequency, SF, bandwidth,
  coding rate, sync word and preamble. Match every setting on the receiver.
- [ ] Send a counted set of ordinary packets from an owned/authorized test node
  at several signal levels. Compare sent/received counts, sequence/packet IDs,
  raw lengths/bytes, RSSI, SNR and receiver timestamps against a reference.
- [ ] Verify the selected RF front-end revision and compare LNA versus bypass
  under weak and strong signals. Do not describe power-off as receive bypass.
- [ ] Check unknown and malformed frames do not crash, stall, or produce
  fabricated node names, positions, messages, or a false decoded classification.
- [ ] Header candidates must remain candidates when payload decoding is absent.
  Confirm encrypted/unknown bytes are preserved for later analysis.
- [ ] Receive duplicate/relayed packets. Check original sender versus relay
  metadata; do not infer the original node is nearby from a received relay.
- [ ] Measure maximum packet airtime for the selected test lengths. Fixed
  profile reception must pass before adding hopping; later dwell times must
  allow full packet completion and document time spent away from each preset.
- [ ] Confirm the receiver never claims simultaneous coverage of multiple
  frequencies/presets or discovery of every LoRa device.

## Milestone 4: scheduling, recon and resilience

- [x] The 0.2.0 bench ran Recon for 12 seconds: 119 raw Wi-Fi frames, 329 BLE
  observations, four matched observations and zero LoRa packets. Switching to
  Wardriver stopped/flushed the old session; a 12-second drive-mode run reported
  25 Wi-Fi AP observations, 460 BLE observations and three LoRa packets. Reported
  errors/drops stayed zero and final Stop closed the log. Counts include repeat
  sightings; match identity, SD contents and stressed transitions remain open.
- [ ] Start Wardriver, return Home, then choose Recon while traffic is arriving.
  Verify the old session closes with its original mode and no later callback
  enters the new session. The new mode must wait for an explicit Start. Repeat
  in the opposite direction while SD flush is delayed. Home/Back alone must
  leave capture running.
- [ ] Confirm Wardriver receives its configured LoRa profile and Recon leaves
  LoRa reception stopped. Compare its channel/phase status with known sources.
- [ ] Compare Recon raw management/data frames against a reference: transmitter
  address, frame type, FCS removal, SSID validation, length, truncated state,
  channel and RSSI. Check the 1–11 sweep and minimum 200 ms dwell while UI/SD
  work is busy; measure delays rather than assuming the nominal sweep rate.
- [ ] Exercise BLE legacy advertisements, service/manufacturer fields and
  boundary/truncation cases with controlled sources. Do not report extended or
  coded-PHY coverage: those scanner modes are not enabled.
- [ ] Compare known ALPR-vendor, Axon/body-camera, iBeacon, Ring/camera, Remote
  ID and Find My/accessory fixtures or owned devices with their reference
  captures. Record false positives and missed observations separately. A
  vendor/payload match must not claim recording state, intent or authentication.
- [ ] Confirm generic Apple company data is not a confirmed AirTag and that
  generic serial modules are not labeled confirmed skimmers. Random/local
  addresses must never obtain vendor evidence from their apparent prefix.
- [ ] Test valid BLE and Wi-Fi Remote ID, malformed packs, missing fields and
  stale summaries. Keep aircraft/operator/takeoff coordinates separate from
  receiver GPS and display fields only when actually decoded. No physical
  capture is established by the synthetic parser tests.
- [ ] Fill Recon's eight-row window, cycle all category filters including
  Custom, page both ways and open evidence detail. Ordinary scan results must
  not fill that list; detail must remain a stable selected snapshot.
- [ ] Raise `display_min` above a known match: it disappears from display but
  remains in the enabled-category native Recon log. Disable its category and
  verify Recon filtering, while ordinary Wardriver observation remains usable.
- [ ] Test `alert_min`, winning-rule `alert:false`, the 30-second retained
  identity cooldown and one-second global cooldown. Verify UI mute takes effect
  without writing configuration and reboot reloads `recon.buzzer`.
- [ ] Keep the shipped rule list empty and confirm built-in detection works.
  Add controlled name/SSID-prefix/OUI/service/manufacturer rules; test invalid
  types, malformed advertisements, unknown categories, duplicates and oversized
  files. Editing the CSV alone must not silently change runtime behavior.
- [ ] In a controlled dense environment, record free/minimum heap, queue
  occupancy/high water marks where instrumented, SD flush maximum, scan cycles,
  ingress/log/UI drops, radio errors, resets and worst touch response.
- [ ] Run at least one 60-minute combined capture and a realistic longer
  battery session. Use measured results to tune queue sizes, dwell and UI rate.
- [ ] Confirm shared TFT/SD transactions do not corrupt pixels or files while
  LoRa runs on its independent SPI bus. Test at least two SD cards, including a
  slow card. Capture card model and worst flush latency with each result.
- [ ] Stop during a busy scan and flush; wait for the logger to become idle.
  Repeated Start/Stop must create separate sessions without mixing observations.
- [ ] Missing card at boot: visible failure, bounded memory, no false saved
  count, responsive UI. Reinsert/remount only while stopped and logger idle.
- [ ] Fill a spare test card, then capture: visible storage error and accurate
  unsaved/drop counts, no watchdog loop or indefinite queue growth.
- [ ] Remove a spare card during an active write; check recoverable failure.
  After stopping, remount/restart into a new session without appending over an
  existing log. Preserve the damaged session for exporter recovery tests.
- [ ] Interrupt power during capture on a spare card. Earlier complete JSONL
  records must remain parseable; a partial final record is visibly skipped by
  export. Do not claim the buffered tail is durable before a completed flush.
- [ ] Test config.json missing/malformed, valid config.bak fallback, oversized
  JSON, invalid radio numbers, and excessive rule counts. Invalid radio config
  must leave reception disabled rather than silently changing the preset.
- [ ] Save credentials using the touchscreen keyboard with dummy values only.
  Verify masking, cancel/backspace, maximum lengths, reboot persistence, and
  recovery from an interrupted save with a valid previous configuration.
- [ ] Search serial/native logs and export files for dummy secret values; none
  may appear. Config may contain secrets by design and stays private on the SD.
- [ ] Test OUI/name rules with known matching and nonmatching devices. Label
  these as rule/vendor evidence rather than proof of device purpose. Verify
  alert cooldowns, mute, returning sightings and changing RSSI remain usable.
- [ ] A summary entry can be deduplicated while subsequent observations and
  their changed signal/location still enter the session log.
- [ ] Verify PSRAM queue/scratch allocation failures refuse initialization
  cleanly, and saturated raw-data traffic increments ingress drops while the
  reserved management capacity and UI remain useful. Allocation success alone
  is not a memory endurance result.

## Milestone 5: exports and eventual upload

- [ ] Run host tests: `py -3 -m unittest discover -s tests -p test_exports.py -v`.
  Current tests cover schema/format, malformed/truncated input, staleness,
  coordinates, random BLE, logger field contract and mesh identity/location.
- [ ] Export a real stopped session. Inspect UTC, receiver positions, empty
  unknown altitude/accuracy, Wi-Fi channel/frequency and BLE Type. Reconcile all
  skip counts. No-fix observations remain in native JSONL and are not invented.
- [ ] Confirm no raw LoRa rows enter WiGLE CSV. Confirm header-only LoRa creates
  zero WDGWars mesh records, with an explicit skipped/not-decoded count.
- [ ] With later verified payload decoding, export a mesh observation whose
  receiver and advertised node positions differ. Require the advertised node
  position and true identifier; reject unknown hop history instead of asserting
  direct reception. Check both Meshtastic and MeshCore identity namespaces.
- [ ] Keep all synthetic fixtures offline. Service acceptance requires real
  observations and intentionally configured credentials, not a synthetic test.
- [ ] When firmware upload exists, test manual upload first. Record response
  transaction/job ID and accepted/rejected counts; bytes sent or HTTP 200 alone
  are insufficient evidence that records were imported.
- [ ] Enable optional auto-upload deliberately, on configured Wi-Fi while
  capture is stopped. Test loss of connectivity, partial success, invalid
  tokens, rate limits, retry backoff and reboot persistence of upload state.
- [ ] Verify a successful upload does not delete original logs and independent
  WiGLE/WDGWars state prevents unintended duplicate resubmissions.
- [ ] Measure scan interruption explicitly if capture-time uploads are later
  offered. Wi-Fi/BLE share one ESP32 radio; do not promise continuous capture.

## Result record

| Check | Firmware hash / hardware | Setup and duration | Measured evidence | Result |
| --- | --- | --- | --- | --- |
| Example: cold battery boot | Fill in | USB disconnected, repeated cycles | Boot log/photo and failure count | PENDING |
| Example: combined scan | Fill in | Known sources, 60 minutes | Counts, gaps, drops, latency | PENDING |
| Example: export | Fill in | Real closed session | CSV inspection and skip report | PENDING |

Keep sensitive coordinates, identifiers and credentials out of public test
reports. Record enough private evidence to reproduce a finding, and keep
software verification distinct from physical and service acceptance.
