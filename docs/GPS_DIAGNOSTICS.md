# GNSS diagnostics

The connected device's 0.2.1 idle snapshots on 2026-11-01 received fresh complete GSV groups: GP changed from zero to one visible satellite with maximum C/N0 19 dB-Hz; GL changed from one to two with maximum C/N0 29 dB-Hz; BD reported zero. The receiver also produced exact `ANTENNA OK` reports. It still reported no position fix and zero satellites used. These observations establish some tracked signals and a working UART; they do not establish an RF hardware fault or a successful outdoor fix. The user reports that this same hardware previously obtained a fix with stock firmware, and that its connected patch antenna faces open sky. The user subsequently confirmed that the stock fix occurred at the same location with USB power, so a USB-versus-battery explanation is not established.

Earlier 0.1.3 diagnostics received valid GGA/RMC but no GSA/GSV. In 0.2.0, the output request below successfully enabled GSA/GSV/TXT reception with zero observed checksum failures. Version 0.2.1 adds complete-group telemetry so the latest sentence from one constellation no longer stands in for all satellite visibility.

Startup sends one PCAS03 output request after the UART has had a settling interval:

```text
$PCAS03,1,0,1,2,1,0,0,1,0,0,,,0,0*00<CR><LF>
```

Quectel's L76K protocol V1.1 section 2.3.3 defines these fields as output divisors. This request enables GGA, RMC, GSA and ANT every positioning cycle and GSV every second cycle. It disables GLL, VTG and ZDA and preserves the specified reserved fields. The helper computes the NMEA XOR checksum. It sends no baud, positioning-rate, constellation or reset command. ANT information is carried in TXT; section 2.2.7 documents the exact warning `ANTENNA OPEN`. [Quectel protocol specification, pages 20 and 23–24](https://files.waveshare.com/upload/d/dd/Quectel_L76K_GNSS_Protocol_Specification_V1.1.pdf)

The configuration write is attempted once with a bounded command and sufficient UART transmit space. `output_config_attempted` records the attempt; `output_config_sent` records a complete write. Neither alone means the receiver acknowledged or applied it. Subsequent GSA/GSV/TXT reception demonstrated the output change on the connected unit. This startup path sends no reset or baud change.

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

For a useful comparison, keep location and antenna orientation the same and compare fresh completed groups while stopped and while capturing. This can reveal a correlation with scan activity without assuming its cause. A missing fix alone does not justify a cold/factory reset. Native tests cover checksums, command bounds, exact antenna text, GSV grouping, blank versus zero C/N0, malformed fields and timeout/wrap behavior. Firmware compilation, diagnostic reception and a physical position fix remain distinct validation steps.

A later 15-second read-only helper run at approximately 257 seconds uptime passed with fresh status/GPS/SKY replies. GP then reported one satellite at a maximum 25 dB-Hz, BD one at 30 dB-Hz, and GL two at a maximum 29 dB-Hz. The receiver still reported no fix. Signal observations change over time; these values describe that recorded window, not a fixed hardware capability. The public result summary is in [VALIDATION.md](VALIDATION.md); raw bench transcripts remain private.

## Bounded quiet comparison in 0.2.2-beta.1

`tunnel quiet` is an optional USB diagnostic, not a capture mode or GNSS reset. It is accepted only after capture is stopped and queued writes are closed. It stops Wi-Fi/BLE and LoRa reception, sleeps the display/backlight, and leaves GNSS power, baud and output configuration unchanged. `tunnel status` and `tunnel gps` remain available. `tunnel resume` reboots immediately; an independent 90-second timer also reboots automatically. The display is intentionally dark during this interval. Start, mode and other commands are refused while the test is active. Stop and save before using it.

The installed beta reported both radio and display shutdown. GNSS continued producing checksum-valid sentences but no fix throughout the recorded quiet window. At about 88.8 seconds uptime, 657 checksums had passed and none failed; the final sampled GP/BD counts were zero and GL showed two visible with maximum C/N0 26 dB-Hz. Those are one snapshot, not an aggregate of the window. The independent timer restart was observed, and normal startup again reported ready, SD, touch and LoRa with zero errors/drops. A subsequent normal sample showed GP three with maximum 25 dB-Hz, BD zero and GL two with maximum 25 dB-Hz, still without a fix.

This narrow comparison did not resolve GPS and does not prove or exclude a particular interference mechanism or hardware fault. No warm/cold/factory restart, baud or constellation command was sent. Continued same-location comparison against the previously working stock behavior remains an acceptance task.
