# GNSS diagnostics

**Current status, 2026-11-01: GPS positioning is physically confirmed on installed 0.3.0-beta.1.** A read-only query at 153,181 ms uptime reported `gps_fix=1`, six satellites, GGA quality 1, RMC A and GSA fix type 3. The accepted location age was 182 ms; 1,286 sentence checksums passed and none failed in that parser epoch. No coordinates were printed. [VALIDATION.md](VALIDATION.md) records the build, verified installation and scoped capture result.

The earlier 0.2.3-dev check independently recorded a fresh 3D fix with five satellites, location age 341 ms and 11,500 valid/zero failed checksums after the user reported working GPS. Both observations remain version-specific. They do not establish the cause of the earlier acquisition difficulty, repeated time to first fix, loss/reacquisition behavior or geotagged SD-file contents. Earlier no-fix windows below remain accurate historical observations; they do not describe the current result.

The user previously reported a stock-firmware fix at the same location, with USB power and the original kit patch antenna connected and facing open sky. The observations did not establish a USB-power problem or a hardware fault.

The controls introduced in `0.2.3-dev` and retained for `0.3.0-beta.1` provide explicit USB version identification, warm/cold restart and constellation selection. They are optional maintenance commands, separate from normal capture and automatic startup. The application does not automatically restart GNSS when it has no fix, and exposes no factory-reset command.

## Automatic output configuration

Earlier 0.1.3 diagnostics received valid GGA/RMC but no GSA/GSV. In 0.2.0, the output request below successfully enabled GSA/GSV/TXT reception with zero observed checksum failures. Version 0.2.1 adds complete-group telemetry so the latest sentence from one constellation no longer stands in for all satellite visibility.

Normal startup sends one PCAS03 output request after a 1500 ms UART settling interval:

```text
$PCAS03,1,0,1,2,1,0,0,1,0,0,,,0,0*00<CR><LF>
```

Quectel's L76K protocol V1.1 section 2.3.3 defines these fields as output divisors. This request enables GGA, RMC, GSA and ANT every positioning cycle and GSV every second cycle. It disables GLL, VTG and ZDA and preserves the specified reserved fields. The helper computes the NMEA XOR checksum. This automatic path sends no baud, positioning-rate, constellation or reset command. ANT information is carried in TXT; section 2.2.7 documents the exact warning `ANTENNA OPEN`. [Quectel protocol specification, pages 20 and 23–24](https://files.waveshare.com/upload/d/dd/Quectel_L76K_GNSS_Protocol_Specification_V1.1.pdf)

The configuration write is attempted once per parser epoch with a bounded command and sufficient UART transmit space. `output_config_attempted` records the attempt; `output_config_sent` records a complete write. Neither alone means the receiver acknowledged or applied it. Subsequent GSA/GSV/TXT reception demonstrated the output change on the connected unit. An explicit restart or constellation command begins a new parser epoch and reapplies this request after its settling period.

## Explicit USB controls

Use an already verified USB serial connection, stop capture, and wait until `tunnel status` reports `running=0 saving=0`. Send one selected command, terminated by a newline; these are alternatives for individual tests, not a sequence to run automatically.

| USB command | Submitted receiver request | Purpose |
| --- | --- | --- |
| `tunnel gps-version` | `$PCAS06,0*1B` plus CR/LF | Query software identification without changing the positioning configuration. |
| `tunnel gps-warm` | `$PCAS10,1*1D` plus CR/LF | Request a warm restart. |
| `tunnel gps-cold` | `$PCAS10,2*1E` plus CR/LF | Request a cold restart as a separate diagnostic experiment. |
| `tunnel gps-init` | `$PCAS04,7*1E` plus CR/LF | Select GPS, BeiDou and GLONASS, then reapply diagnostic output. It does not change baud, navigation mode or positioning rate. |

PCAS04 and PCAS10 are documented in Quectel's L76K protocol sections 2.3.4–2.3.5. The PCAS06 software query comes from the upstream Meshtastic L76K probe and is not listed in that Quectel document. [Quectel protocol](https://files.waveshare.com/upload/d/dd/Quectel_L76K_GNSS_Protocol_Specification_V1.1.pdf), [pinned Meshtastic version probe](https://github.com/meshtastic/firmware/blob/54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb/src/gps/GPS.cpp#L1358)

All four controls are refused while capturing, saving or waiting for the logger to close. The receiver-control layer also refuses a new request during a settling hold or within five seconds of the last successful submission. Insufficient UART transmit space or an incomplete write returns `sent=0`. A reported `sent=1 acknowledged=0` means the complete command was submitted to UART; it does not prove a receiver acknowledgment, configuration change, restart or fix. These PCAS requests have no acknowledgment defined in the cited L76K specification.

Warm/cold requests clear the host's previous location, UTC, custom GGA/RMC validity fields and diagnostic caches immediately, discard incoming settling bytes for at least 1500 ms, then clear the caches again when the receive backlog has drained. Constellation selection follows the same process with a minimum 250 ms hold. The receive work remains bounded to 512 bytes per loop. Diagnostic output is requested again on the next loop. Start is refused while that hold is pending, so cached coordinates or time from before the command cannot enter a new session. The five-second command cooldown is separate from this shorter Start hold. Fresh sentences repopulate the counters and validity fields; the resulting counter reset is intentional. A version query clears only the prior software-identification result and does not clear the position cache.

An expected software reply is `$GPTXT,01,01,02,SW=URANUS5,V5.3.0.0*1D` followed by CR/LF: 40 wire bytes, including a comma inside the TXT text. The comma must remain part of the software value; splitting every comma and examining only the last field loses the `SW=` prefix. Only a bounded, allowlisted software-identification value from a checksum-valid sentence should be exposed as `LT GNSS_VERSION`. A blank result is not proof that the receiver did not answer. The initial 0.2.3-dev query produced additional TXT messages but its software field was not captured; the comma-handling issue was identified separately. Firmware-family strings such as `URANUS5,V5.3.0.0` do not uniquely establish the physical module model, and HW/IC replies may contain serial identifiers. [Pinned CASIC hardware examples](https://github.com/espruino/EspruinoDocs/blob/6355b54120a3a8a28c628b3d087160f7149cbd65/info/Bangle.js2%20Technical.md#L126), [upstream parser preserving the TXT tail](https://github.com/jclark/satpulse/blob/4f312a0d5f23eeecdbf59cd0d9cb3331efbfc84b/gps/lib/casmsg/casmsg.go)

For comparison, the manufacturer's currently linked Meshtastic 2.7.26 image identifies upstream revision `54e0d8d`. Its L76K path selects all three constellations, asks only for GGA/RMC and sends a vehicle-mode command (`PCAS11,3`). LORA Tunnel retains GSA/GSV/antenna diagnostics and does not send PCAS11, which is not described in the cited Quectel protocol. Existing GP/BD/GL reports suggest all three constellations were already active on the tested unit; explicit selection is not a proven fix. [Manufacturer image directory](https://resource.heltec.cn/download/WiFi_LoRa_32_Expansion_Kit-V2/firmware/Meshtastic), [pinned stock-revision initialization](https://github.com/meshtastic/firmware/blob/54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb/src/gps/GPS.cpp#L528)

## Read-only status helper

Run the bounded host helper from the project directory with the explicit, currently verified serial port:

```powershell
python tools/bench_gps.py --port PORT --seconds 15
```

Use a Python environment with `pyserial` installed (`python -m pip install pyserial`). Replace `PORT` with the confirmed serial port. The port must be available to this helper; use the actual current port if Windows renumbers it. `--seconds` defaults to 15 and accepts 1 through 60. Every three seconds the helper sends only `tunnel status` and `tunnel gps`. It does not start/stop capture, change configuration, reset the receiver, or flash firmware. DTR/RTS are disabled before opening the port to avoid the conventional automatic reset sequence.

Only allowlisted status/GPS numbers, status enums and GP/GB/BD/GL/GN sky rows are printed. Unknown fields, raw NMEA, coordinates and arbitrary serial text are omitted. Input lines are bounded to 4096 bytes. The helper returns nonzero for serial errors, missing/stale status or GPS replies, receiver sentences older than five seconds, panic/reset diagnostics or oversized serial lines. A `GPS bench PASS` means fresh diagnostic reception; a valid no-fix result can pass. It does not claim that GNSS positioning works. No files or credentials are read from the device.

`tunnel gps` exposes counters, status codes and ages without coordinates, UTC strings or raw NMEA text. Read a fresh snapshot after output has settled:

- Increasing checksum successes establish NMEA reception at the configured baud. Repeated checksum failures or bytes without accepted sentences require a different investigation.
- GGA quality 0, RMC V and GSA type 1 indicate no fix. They should not be replaced with fabricated coordinates.
- The original `satellites_visible` is still only the latest GSV sentence's value. Prefer the separate `LT SKY` rows: `talker`, `sentences`, `complete_groups`, `age_ms`, `visible`, `max_cn0`, `cn0_count`, `positive_cn0_count`, and `signal_id`.
- Each sky row represents its latest complete, sequential GSV group for one talker and signal. Partial, out-of-order or inconsistent groups do not overwrite the previous completed group. Assembly expires after five seconds; completed data retains an increasing age. Counts across rows are not a deduplicated total, and a talker's later signal group may replace its earlier signal group. GB and BD are kept separately rather than double-counted as a sum.
- `cn0_count` counts nonblank C/N0 fields; `positive_cn0_count` counts values above zero. Missing C/N0 remains unknown (`max_cn0:-1` when none are reported). `signal_id:255` means unavailable. The L76K protocol describes C/N0 in dB-Hz and leaves it blank when a satellite is not tracked. [Quectel GSV definition, pages 14–15](https://files.waveshare.com/upload/d/dd/Quectel_L76K_GNSS_Protocol_Specification_V1.1.pdf)
- `antenna_status` is the latest recognized exact TXT report: -1 unknown, 0 OK, 1 OPEN. `antenna_status_age_ms`, `antenna_ok_reports`, `antenna_open_reports`, and `antenna_open_age_ms` show freshness and history. `ANTENNA OK` is documented in Quectel's L76K examples; the receiver's active-antenna detection is based on current, not a test of GNSS signal quality. Neither OK nor an absence of OPEN proves an unobstructed, suitable or healthy RF path. Unrecognized TXT remains undisclosed. [Quectel GNSS examples](https://developer.quectel.com/doc/quecpython/API_reference/en/gnsslib/gnss.html), [L76K hardware design, page 21](https://resource.heltec.cn/download/Heltec%20Capsule%20Sensor%20V3/Quectel_L76K_V1.1-1.pdf)
- `UINT32_MAX` age means the corresponding observation is unavailable. Old location validity is separate from the application's current, age-checked fix.

For a useful comparison, keep location and antenna orientation the same and compare fresh completed groups while stopped and while capturing. This can reveal a correlation with scan activity without assuming its cause. Keep explicit restart experiments separate and do not repeat them in a loop that prevents acquisition. Native tests cover checksums, command bounds, exact antenna text, GSV grouping, blank versus zero C/N0, malformed fields and timeout/wrap behavior. Firmware compilation, diagnostic reception and a physical position fix remain distinct validation steps.

## Recorded 0.2.1 observations, 2026-11-01

The connected device's 0.2.1 idle snapshots received fresh complete GSV groups: GP changed from zero to one visible satellite with maximum C/N0 19 dB-Hz; GL changed from one to two with maximum C/N0 29 dB-Hz; BD reported zero. The receiver also produced exact `ANTENNA OK` reports. It still reported no position fix and zero satellites used. These observations establish some tracked signals and a working UART; they do not establish an RF hardware fault or a successful outdoor fix.

A later 15-second read-only helper run at approximately 257 seconds uptime passed with fresh status/GPS/SKY replies. GP then reported one satellite at a maximum 25 dB-Hz, BD one at 30 dB-Hz, and GL two at a maximum 29 dB-Hz. The receiver still reported no fix. Signal observations change over time; these values describe that recorded window, not a fixed hardware capability. The public result summary is in [VALIDATION.md](VALIDATION.md); raw bench transcripts remain private.

## Bounded quiet comparison

`tunnel quiet` is an optional USB diagnostic, not a capture mode or GNSS reset. It is accepted only after capture is stopped and queued writes are closed. It stops Wi-Fi/BLE and LoRa reception, sleeps the display/backlight, and leaves GNSS power, baud and output configuration unchanged. `tunnel status` and `tunnel gps` remain available. `tunnel resume` reboots the ESP32 immediately; an independent 90-second timer also reboots it automatically. The `0.2.3-dev` command `tunnel quiet-long` selects a 300-second limit instead. This intentional host restart restores normal operation; it is not an automatic no-fix GNSS restart. The display is intentionally dark during the interval. Start, mode and other commands are refused while the test is active. Stop and save first, and finish any GNSS settling hold before starting a quiet comparison.

In the installed `0.2.2-beta.1` test on 2026-11-01, the device reported both radio and display shutdown. GNSS continued producing checksum-valid sentences but no fix throughout the recorded quiet window. At about 88.8 seconds uptime, 657 checksums had passed and none failed; the final sampled GP/BD counts were zero and GL showed two visible with maximum C/N0 26 dB-Hz. Those are one snapshot, not an aggregate of the window. The independent timer restart was observed, and normal startup again reported ready, SD, touch and LoRa with zero errors/drops. A subsequent normal sample showed GP three with maximum 25 dB-Hz, BD zero and GL two with maximum 25 dB-Hz, still without a fix.

That narrow 0.2.2-beta.1 comparison did not resolve GPS and does not prove or exclude a particular interference mechanism or hardware fault. No warm/cold/factory restart, baud or constellation command was sent during that earlier test. Subsequent user confirmation and a fresh, physical fix check are recorded above. Repeatable acquisition and valid geotagged-log checks remain separate acceptance tasks.

## 0.2.3-dev diagnostic checkpoint, 2026-11-01

The installed candidate returned the checksum-valid software value `URANUS5,V5.3.0.0` after the comma-handling repair. Separate warm-start (150 seconds), explicit all-constellation selection (120 seconds), and cold-start (300 seconds) observation windows all retained receiver-reported no-fix. Their final accepted checksum counts were respectively 1,134, 915 and 2,257, with zero failed checksums in each epoch. The cold-start final report still had GGA quality 0, RMC V and GSA type 1. This establishes command-path/diagnostic testing, not a positioning repair. Further isolated-receiver evidence is recorded in [VALIDATION.md](VALIDATION.md). The later user confirmation and physical fix check supersede this historical no-fix status, without retroactively changing these test results.
