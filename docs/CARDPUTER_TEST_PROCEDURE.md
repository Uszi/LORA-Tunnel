# Cardputer ADV candidate device test

This procedure is for **Cardputer ADV + Cap LoRa-1262 U214**, version
**0.4.0-beta.1**. It is a procedure, not a record of completed testing.
Use [the controls guide](CARDPUTER_ADV.md), [upload guide](UPLOADS.md), and
[release acceptance record](CARDPUTER_RELEASE.md) together.

## Preserve the device first

1. Identify the attached Cardputer ADV, its USB port, flash size and existing
   firmware. Record whether Launcher is already installed and its version.
   Close other serial monitors. Do not select a different attached ESP32-S3.
2. Copy the existing microSD contents to a private backup. With the identified
   device in download mode, use esptool to read all **8,388,608 bytes** of flash
   before any write. For esptool 5, the command is:
   `python -m esptool --chip esp32s3 --port PORT read-flash 0 0x800000 cardputer-before.bin`.
   Replace PORT with the identified port. Verify length and SHA-256, then read
   again to a second private file and compare. Dumps can contain credentials;
   never put them in the public repository or release package.
3. Record candidate factory/app hashes from `PACKAGE.json`. The factory image
   replaces Launcher; the application image must only be installed through
   Launcher's SD application installer. Preserve a verified recovery route.
4. Use a FAT32 test microSD and the supplied empty-credential SD setup. Fit U214
   and its antenna with power off. Keep service upload switches off initially.

## Short standalone acceptance

1. Install the factory candidate through the staged loopback web installer.
   Explicitly select Cardputer ADV + U214 and confirm that board. Record the
   browser/version and exact downloaded hash. Test a cold boot without USB.
2. Check landscape orientation, colors, green branding, VTS mark, readable
   pages and both menu pages. Exercise Home, all Wardriver tabs, Recon list,
   evidence detail, Settings, region, credentials, Uploads and Power.
3. Type a disposable SSID/password and API values using Shift/Fn, punctuation,
   spaces, Backspace, Enter and Escape. Switch between equal-sized key chords
   and test quick press/release and held navigation. Verify masks, cancellation,
   validation errors, saved values after reboot and no credential serial output.
4. Confirm first-run UNSET/off, explicit Skip LoRa and region persistence after
   restart, both with and without the SD config. Try an imported unsupported
   region/frequency: it must retain the document and explain why LoRa is off.
   An imported `buttons_enabled: false` must leave keyboard navigation usable.
5. Acquire a fresh outdoor GNSS fix and UTC. Capture known Wi-Fi/BLE and a known
   compatible LoRa source in Wardriver. Match its selected frequency/modem and
   record actual receive evidence; a ready indicator alone is insufficient.
6. Stop/save, read the SD independently, validate complete matching start/end
   records and inspect actual saved geotags privately. Confirm stale or missing
   fixes are not exported as valid positions. Check Recon evidence and optional
   audible alerts, mode changes, repeated Start/Stop and repeated sessions.
7. Test battery operation, Power Cancel, confirmed save/sleep and G0 wake.
   Software sleep does not disconnect the battery or guarantee GNSS power-off.
   Wait for or cancel uploads before removing the card or power-cycling.

## Offline SD readback

After Stop has saved completely and uploads are finished or cancelled, power
down and use the PC's microSD reader to copy the selected native
`/sessions/LT-*.jsonl` files into a private directory. Retain the original files
and validate the copies offline, for example:

```sh
python tools/check_session_readback.py private/readback/session.jsonl --expected-target cardputer-adv-lora1262 --expected-version 0.4.0-beta.1
```

Replace the example path with the copied session; multiple file paths are
accepted. The tool checks the production schema, one matching start/end pair,
contiguous sequence numbers, session/mode/board/version consistency, retained
raw lengths, and GNSS freshness rules. It fails on malformed or partial records.
For a session deliberately containing the corresponding test evidence, add
`--min-lora-records 1` and/or `--min-fresh-gps-records 1`. Each minimum applies to
**every supplied session**, so validate different test scenarios separately.
A structurally valid session alone does not prove LoRa reception, RF losslessness,
GNSS accuracy or origin; inspect the known-source evidence privately as well.

The JSON report includes aggregate counts, line numbers, fixed error codes and
schema locations, without input paths, record values, captured identifiers or
coordinates. Direct capture files and original diagnostic logs must remain
private: never commit or publish them with firmware/source/relink packages.
Publish only a reviewed, sanitized acceptance summary. The validator makes no
device or network connection and does not upload the copied data.

## Storage and absent-hardware checks

Use disposable test data/cards for fault cases. Test boot without SD, Retry SD,
a full test card, write/open failures, and a slow card. Confirm errors and drops
are visible, original files survive, and save/sleep failure keeps the device
awake rather than claiming a clean shutdown. Confirm UI responsiveness while
SD writes and LoRa receive compete for SPI access. Power down before removing
U214; reboot without it and verify a useful cap status while Wi-Fi/BLE remain
usable. Restore U214 and repeat reception.

## Thirty-minute capture and timing

Start Wardriver manually, with known receive traffic and healthy SD storage.
Interact with the UI at least 30 times throughout a run of at least 30 minutes.
Collect allowlisted metrics using:

```sh
python tools/bench_cardputer.py --port PORT --seconds 1800 --output private/cardputer-30min.json
```

Require no crashes, watchdog resets or allocation failures. Inspect heap trends
for a growing leak; target minimum free internal heap of 32 KiB, largest free
block of 16 KiB and monitored task-stack margins of 1 KiB. The report preserves
first/last heap-window averages but does not classify allocator fluctuation as
proof of a leak or its absence. Record drops, error deltas and actual SD readback.
Software counters cannot establish lossless RF reception.

Measure physical keypress-to-draw separately, including keyboard FIFO and main
loop delay. Target p95 at most 100 ms and maximum 250 ms on healthy storage.
Firmware `ui_p95_ms`/`ui_max_ms` only measure dispatch-to-draw. A host render or
passing dispatch metric alone cannot pass the complete physical latency check.

## Live uploads and Launcher

1. Enter the home SSID/password and service API credentials on the keyboard.
   Keep them and location-bearing captures private. Start with a small completed
   session appropriate for the configured account; synthetic-marked sessions
   are deliberately excluded from network upload.
2. Test each enabled service manually, then enable its automatic switch. Verify
   actual WiGLE and Wardrift receipts and WDGWars job completion. A stored key,
   HTTP request, or pending 202 is not successful acceptance.
3. Check startup and stop/save upload scheduling, no network upload during
   capture, unavailable home Wi-Fi, credential failure, cancellation and Start
   during an upload. Reboot after a pending/uncertain request; accepted chunks
   must stay complete and ambiguous POSTs must require manual retry. Check peak
   TLS heap and cancellation responsiveness on the device.
4. Restore/install the official Launcher as appropriate for the backed-up
   device. Record the exact version. Install the checksummed application-only
   BIN from its SD menu, preserving its settings and other applications.
5. Test LORA Tunnel, stop/save and finish/cancel uploads, power-cycle, press Enter
   during Launcher startup, and return to its menu. Check another installed app,
   relaunch LORA Tunnel, verify settings, and verify Launcher auto-start.

Record results against the exact hashes in the release checklist. Any binary
change invalidates prior binary-specific acceptance. GitHub/Gitea publication,
production web installation and M5Burner submission follow completed acceptance.

## Optional offline upload-memory diagnostic

On a Cardputer with capture stopped and saved, a USB serial terminal at 115200
baud can send `tunnel upload-check`, then `tunnel status`. This uses the real
idle writer, BLE release/restore and upload-worker allocation path, but does
not access files, connect Wi-Fi, make HTTP requests, copy credentials or write
settings. It consumes pending automatic/retry triggers for that idle period;
the stored upload preferences remain unchanged. `tunnel upload-cancel` cancels
it through the normal cancellation path. It is refused on Heltec and while
capture, save or another maintenance operation prevents safe admission.

Wait for `upload=0` before interpreting `upload_check`: 0 means unrun, 1 running,
2 passed and 3 failed/canceled. `scanner_stage=ready` is required afterward.
`upload_runs` counts completed workers, so cancellation before worker creation
does not increment it. Resource fields are sampled lifetime minima, not peak
allocation traces: `upload_min_free` / `upload_largest` cover sampled worker
phases; `upload_ready_free` / `upload_ready_block` sample after parser release
and before HTTP client allocation (also reached by this offline diagnostic).
`upload_stack` is a worker stack low-water mark. Zero means no sample yet.

This diagnostic can verify cleanup and cancellation without capture or a
service account. It does not replace a real TLS/upload receipt, loaded capture,
physical UI latency or Launcher installation check.
