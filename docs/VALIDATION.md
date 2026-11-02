# Validation record — 2026-11-01

Latest installed development build: **LORA Tunnel 0.2.3-dev**; published beta remains **0.2.2-beta.1**. Local board `heltec_v4_r8_tft`, Heltec Expansion V2 / V4-R8, 16 MB QIO flash and 8 MB OPI PSRAM. The version-specific results below distinguish source checks, device reports and user confirmation. GNSS communication and signal reports work; no position fix is claimed. User confirmed the kit family, 915 MHz variant, and United States operation on 2026-11-01. The exact PCB silkscreen and fitted RF front-end still require physical verification.

## 0.2.3-dev: documented recovery and isolated receiver comparison

The user reconfirmed persistent outdoor no-fix and identified the original GNSS antenna supplied with the kit. Before changing receiver state, a read-only check at roughly 9,988 seconds uptime showed 74,933 accepted checksums, zero failed checksums, zero sentences with a fix, and fresh GGA quality 0 / RMC V / GSA type 1. A running capture was stopped and its queued writes closed before maintenance.

The development build adds explicit, idle-only USB software-version, warm-start, cold-start and all-constellation commands. It invalidates prior fix/time/parser caches across recovery, re-registers TinyGPS custom fields, drains settling input with bounded work, and reapplies diagnostic output. Start is held during the settling interval. A TXT comma-handling repair preserves `SW=URANUS5,V5.3.0.0`; the physical receiver returned that checksum-valid software identifier. A software-family string does not prove a physical module marking. All **17 GNSS native tests passed**; the final production candidate compiled in 27.25 seconds. No automatic no-fix reset or production factory-reset command was added.

| Separate observation | Duration | Final accepted / failed checksums | Position fix |
| --- | ---: | ---: | --- |
| Documented warm restart | 150 s | 1,134 / 0 | None |
| Explicit GPS + BeiDou + GLONASS selection | 120 s | 915 / 0 | None |
| Documented cold restart | 300 s | 2,257 / 0 | None |
| Temporary GPS-only application | 150 s | 1,122 / 0 | None |
| GPS receiver factory-settings request, GPS-only application | 300 s | 1,782 / 0 | None |
| Stock Meshtastic GNSS command sequence, GPS-only application | 150 s | 303 / 0 | None |

The temporary application initialized no Wi-Fi, BLE, LoRa, display or SD drivers. It held SX1262 reset, disabled its front end, turned VEXT off, left peripheral buses as inputs and ran the CPU at a reported 80 MHz with APB80. TinyGPSPlus and an independent checksum/status parser agreed on the absence of a fix. This is stronger isolation than merely stopping a capture, but does not prove a particular hardware failure or exclude all supply, cable or RF interference.

The receiver-only factory-settings request used documented PCAS10 flag 3, sent once by explicit bench command. ESP32 flash settings, SD files and application credentials were not factory-reset. Subsequent output changed to the documented default GPS/BeiDou combination; warm/cold/constellation requests have no defined PCAS acknowledgment, so counters alone establish UART submission. The factory-window host report-drop count of two was a USB reporting-backlog counter across the disconnected interval, not an NMEA checksum failure.

The last comparison submitted the three GNSS commands from Meshtastic revision `54e0d8d`: PCAS04 mode 7, GGA/RMC-only PCAS03, and PCAS11 mode 3, separated by at least 250 ms. Its stage reached complete with three submissions and fresh GGA/RMC continued, still quality 0 / RMC V. GSA/GSV/TXT are deliberately disabled by that stock output selection, so their absence in this window is not receiver failure. This compared the GNSS initialization sequence in an isolated application; **the original Meshtastic firmware itself was not reflashed or physically retested**. PCAS11 is sourced from upstream implementation, not the inspected L76K protocol specification.

The normal 0.2.3-dev application was restored with an application-only write and separate flash verification. Binary length is **2,454,480 bytes**, SHA256 `8e2deb3a08cb294f26f3737f185b208efd83ccc82d3b6c27c428c5f68795dac0`. The rounded write boundary `0x268000` remains inside the previously verified recovery-prefix boundary. Restored boot reported ready, SD, touch and LoRa with zero errors/drops. All three constellations and diagnostic output were explicitly re-established. A short capture/stop regression recorded **16 Wi-Fi observations and 197 BLE advertisements**, zero LoRa packets, zero application errors/drops, and a closed session (`running=0 saving=0`). It did not verify SD file contents or LoRa reception in that short window.

The final read-only query at about 151 seconds uptime still showed no fix, with 1,004 accepted checksums and one failed checksum. That single failed-checksum count was already present at the earlier post-configuration snapshot and did not increase in the final check; its cause was not established. GGA quality 0 / RMC V / GSA type 1 remained fresh. The normal interface application is restored; new physical screen acceptance has not been claimed. Raw diagnostics and temporary images remain private. No positioning repair is claimed; antenna/connector inspection and current reception conditions remain the next physical checks. Primary hardware/protocol references and command semantics are in [GPS_DIAGNOSTICS.md](GPS_DIAGNOSTICS.md) and [HARDWARE.md](HARDWARE.md).

Raw build/serial transcripts and device-specific backup metadata are retained privately and excluded from the public source release. This document preserves summarized results, versioned public firmware hashes and limitations; synthetic test fixtures remain public. Source/relinking materials are described in [RELINKING.md](RELINKING.md).

The 0.1.1-dev changes add a shared version definition, confirmed US default LoRa settings, explicit US passive Wi-Fi channels 1–11, current-radio-setting preservation when creating a config through credential save, and optional `tunnel status/start/stop` USB commands with a bounded bench helper. Missing configuration uses the confirmed defaults; damaged existing configuration without a valid backup disables LoRa. These source changes are not evidence of physical capture or standalone acceptance.

The first 0.1.1-dev rebuild passed in 224.54 seconds: **2,047,139 / 6,553,600 bytes application flash** and **109,004 / 327,680 bytes static RAM**, with 15 native and 15 Python exporter tests passing. Its factory SHA256 was `c0bc95ce3ffadccfb823efa9054c0786ef58d93d533e5da2ae43170e0e1a9c9e`. **That pre-review binary was superseded and was not installed.** The later fix validates schema/radio settings before primary/backup/temp selection and backup rotation; 22 native tests passed including seven new configuration document cases. The installed build and physical bench results are recorded below.

## Historical 0.1.0-dev build and host checks

The first full firmware compilation and the dedicated-board compilation passed. Final artifact metrics and checksums are recorded with the build outputs. The firmware build uses pioarduino 55.03.312-1, Arduino-ESP32 3.3.12, ESP-IDF binary libraries 5.5.5+sha.b774170ff46, Xtensa compiler 14.2.0+20260121, esptool 5.4.0 and PlatformIO Core 6.2.0. Direct dependencies are pinned in `platformio.ini`; resolved versions were recorded with the private build record. A fresh-machine rebuild and byte-for-byte reproducibility have not been demonstrated.

Final 0.1.0-dev build after its source fixes: **PASS**, 268.49 seconds. PlatformIO size check: **2,045,739 / 6,553,600 bytes application flash (31.2%)** and **108,980 / 327,680 bytes static RAM (33.3%)**. These figures exclude runtime queue/task/driver allocations and do not establish operating free heap. Startup diagnostics report actual flash, PSRAM and free heap on the device. A complete factory binary and application binary were generated; their SHA256 values were recorded in that build's local output manifest. These sizes do not describe the newer 0.1.1-dev source.

Host validation of 0.1.0-dev:

- **15/15 native C++ tests passed**: 5 Meshtastic header candidate tests, 4 radio configuration validation tests and 6 bounded UTF-8/name tests. Native compiler is existing MinGW GCC 5.1.0 with C++14. Firmware uses the ESP32 cross-compiler.
- **15/15 Python exporter tests passed** under Python 3.14. Coverage includes malformed/interrupted/oversized JSONL, UTC normalization, stale/missing GPS, legitimate zero coordinates, channel/frequency consistency, random BLE filtering, raw SSID recovery, CSV escaping, mesh identity and node/receiver position separation, and output overwrite protection.
- Both JSON schemas checked against Draft 2020-12. All six synthetic native records and the shipped example configuration validated; malformed probes rejected and extra fields accepted. This schema check used jsonschema 4.25.1 in a temporary host environment; the exporter itself needs only the Python standard library.
- Original supplied BWM asset copied without modification; SHA256 compared against the user's original. The generated logo was visually inspected as a green/cyan pixel reinterpretation and embedded into the firmware.
- Hardware/source and license review recorded in `HARDWARE.md`, `LORA.md`, `EXPORTS.md` and `THIRD_PARTY.md`.

Build log warning: RadioLib warns that USB CDC debug output may stop on sleep. This firmware does not implement sleep and disables RadioLib debug output; standalone cold-boot and future sleep behavior still need physical checks. Some early Windows console output could not display optional PlatformIO metrics; the actual linker size check and binary generation passed.

Source review fixes included retaining a valid config backup during recovery, validating radio integers before narrowing, counting buffered records on SD failure, preventing a BLE callback crossing into another session, preserving invalid GNSS status until a newer fix, prioritizing LoRa read/rearm, bounding malformed names, and avoiding recursive acquisition of the display/SD mutex.

**At the initial build checkpoint, no physical device test had been performed.** No serial device had been opened, no backup or flash had been performed, no GNSS fix or radio capture had been observed, no touchscreen/buzzer/battery behavior had been measured, no endurance/card-interruption test had been run, and no WiGLE/WDGWars upload had been made. The synthetic fixtures are not captures and must not be uploaded. Hardware acceptance results must be recorded in `TEST_CHECKLIST.md` as checks are actually completed.

## Subsequent connected-unit preflight

Connected-unit preflight on 2026-11-01 used esptool 5.4.0 on the confirmed device port. It identified **ESP32-S3 QFN56 revision 0.2, embedded 8 MB PSRAM (AP, 3.3 V), and 16 MB quad flash**, with secure boot and flash encryption disabled. These are actual read-only device identification results. They do not establish PSRAM runtime stability or display/touch/GNSS/SD/radio acceptance.

The default esptool 5.4 stub failed at flash-size configuration before creating a backup. Clean-reset 4 KiB reads succeeded with ROM-only mode and with the legacy stub selected by `--stub-version 1`; see `RECOVERY.md` for the version-specific fallback. Two complete **16,777,216-byte** legacy-stub reads completed, but their SHA256 hashes differed. They differed at one byte, address `0x3F328B` (bit mask `0x02`). The original application image starts at `0x10000` and ends at `0x371B50` exclusive; its appended SHA256 validated in both full reads. The unstable byte is beyond that image, inside the app0 partition. This establishes intact application image copies, not a fully reproducible flash backup or successful restoration.

Same-connection repeated sector reads also differed through both the legacy stub and the ESP32 ROM reader. After the user reported reconnecting and entering boot mode, an automatic reset successfully contacted the ROM bootloader again on the confirmed port. Five further ROM reads of sector `0x3F3000` still differed at `0x3F328B`, with one additional differing byte at `0x3F332C` in one probe. The cause is not established. Do not claim a bad flash chip, stable full backup, or successful flash from these results.

The user subsequently resumed installation work. **No erase or flash write has occurred at this pre-install checkpoint.** Private dumps and diagnostics are retained outside the source tree; they do not enter public archives.

## Verified recovery regions after resuming

Fresh held-loader reads established a scoped recovery set for the current prototype. Prefix `0x000000..0x372000` (3,612,672 bytes) matched across three fresh acquisitions, SHA256 `173e34b2fc90205f8f180ffdb41335470c7dc6172325907982213324e4479490`. The entire old application remains byte-identical to both full acquisitions and internally hash-valid. The fresh prefix differs from the older full acquisitions only at 83 bytes in NVS sector `0x9000`, after the user's intervening power cycle; the prefix is now preserved at this newer state. This is an observed NVS difference, not evidence of a particular setting or its cause.

The coredump range `0xFF0000..0x1000000` (65,536 bytes) matched in two fresh reads and both earlier full acquisitions, SHA256 `71189f7fb6aed638640078fba3a35fda6c39c8962e74dcc75935aac948da9063`. It is included because the selected SDK can write a flash coredump after a panic. A private recovery manifest records the exact files, ranges and checksums. Restoration itself has not been tested. The full-chip discrepancy remains unresolved and the unstable sector is excluded from the planned write and scoped recovery operations. See `RECOVERY.md` for the reviewed write bounds and runtime-write scope.

The final host checks passed **22 native cases and 25 Python cases** (15 exporters plus 10 fake-serial bench-helper cases). The first final rebuild failed because the shared Arduino framework/SDK directories changed versions during compilation: its start reported 3.3.12/5.5.5, while the failed build's paths subsequently contained 3.0.7/5.1.0. The retained private build transcript records missing headers in that interrupted installation. The project now sets `core_dir = .pio-core` to isolate its pinned packages. The user confirmed that a microSD card is installed.

## Installed 0.1.1-dev build and USB bench result

The isolated rebuild **passed in 489.98 seconds**, including first-time private framework/SDK installation. Final size is **2,047,315 / 6,553,600 bytes application flash** and **109,052 / 327,680 bytes static RAM**. The generated factory image is **2,135,600 bytes**, SHA256 **`fa1fe00b79b0a1884ae733e4dc63c55c59d94d763ce3097ea17885c5a5885810`**. Its bootloader and application internal SHA256 digests were independently checked before installation.

The user authorized proceeding with installation. The confirmed port was rechecked against the same unit's identity. Project-local esptool **5.4.0**, legacy stub **1**, wrote the factory image at address zero with mode/size/frequency preserved. The flasher reported erasure of **`0x00000000..0x00209FFF` inclusive**, entirely inside the verified recovery prefix and outside the unstable sector. Automatic written-data digest verification passed. A separate `verify-flash` also passed before the first hard reset. The private flash and verification transcripts are retained with the recovery set. No full-chip erase or eFuse change occurred.

Application USB status identified **0.1.1-dev** and reported `ready=1`, `sd=1`, `touch=1`, and `lora=1`. The ready gate includes the expected flash/PSRAM size checks and successful display initialization. This is initialization evidence, not an electrical memory stress test, proof of screen appearance, touch alignment, or LoRa packet reception. GNSS character count advanced from 177 at uptime 5.0 seconds to 5,605 at uptime 96.6 seconds; `gps_fix=0` and `sats=0` throughout the recorded bench window. No valid receiver location was claimed.

A short capture was started by the USB command, with first running acknowledgment at uptime **33.695 seconds**. Stop was acknowledged at uptime **87.448 seconds**, approximately **54 seconds** later. Final counters were **113 Wi-Fi observations, 1,991 BLE advertisements, zero LoRa packets, zero reported queue/log drops, and zero reported errors**. These counts include repeated sightings and are not unique physical-device totals. During the sampled capture, free heap settled near **60,968 bytes** and returned to **65,632 bytes** after Stop. This brief result is not an endurance or dense-environment acceptance test.

Stop first reported `running=0 saving=1`, then `running=0 saving=0` with the SD still mounted and no reported write errors. The logger therefore completed its flush/close path. The actual SD JSONL file has **not yet been read back or exported**; do not infer file-content integrity or GPS-valid export eligibility solely from counters. No upload occurred. The corresponding status/capture/Stop transcripts remain private.

The unit was left running the application with capture stopped. Screen appearance/touch navigation await user confirmation; battery-only cold boot, outdoor GNSS fix, a known LoRa transmitter, SD file readback, resilience/endurance and service acceptance remain pending. Automatic uploads, Meshtastic payload decoding, hopping and full recon signatures remain later milestones.

The first bench sequence is stock backup -> board/memory verification -> battery-only boot -> display/touch -> GNSS -> SD -> owned Wi-Fi/BLE comparison -> fixed-profile LoRa reference traffic. The United States regional selection is confirmed; verify the fitted RF front-end before the enabled SD profile is used for reception. Automatic uploads, authenticated mesh payload decoding, hopping and full recon signature coverage are not completed by this build.

## Installed 0.1.2 and 0.1.3 updates

The 0.1.2 renderer moved full-screen drawing into a 153,600-byte PSRAM framebuffer and presented one completed frame to the TFT. Build passed in 27.09 seconds; application flash 2,052,571 bytes and static RAM 109,412 bytes. Application SHA256 was `3ca264267fd09ce81ebe86bcdbb85885a8dad937d9fade20d7b2fec757a4941d`. Application-only write and independent verification passed. A short bench recorded 27 Wi-Fi and 495 BLE observations, zero LoRa packets and zero reported drops/errors. **The user confirmed the screen worked and flickering/pulsing stopped.**

The 0.1.3 diagnostic update built in 268.01 seconds, with application flash 2,054,235 bytes and static RAM 109,572 bytes. Application SHA256 was `014fb525b99cd26e53100f765113c0b990adfafbfb7cd9ee96cab88e28c3a203`. Application-only write and independent verification passed. GNSS diagnostics established checksum-valid 9600-baud GGA/RMC, GGA quality 0 and RMC V, with no GSA/GSV output. This was receiver-reported no-fix, not a conclusion drawn from byte counts alone. The user reports this same unit previously obtained a fix with stock firmware and confirms its GNSS patch antenna is connected and faces open sky.

Before the next update, the user-started 0.1.3 session was stopped and flushed. Final counters were **1,040 Wi-Fi observations, 15,381 BLE advertisements, eight received LoRa packets, zero drops and zero errors**. `saving` went from 1 to 0 with SD still available. Counts do not identify unique devices or prove the LoRa payload protocol. The actual SD file was not read back.

## Installed 0.2.0-dev: separate modes, Recon and radio logo

The integrated source added independent Wardriver and Recon scheduling, bounded raw Wi-Fi reception, strict passive Recon parsers, SD category/rule preferences, PSRAM-backed ingress/writer queues, optional IO/USER navigation, separate Home/Wardriver/Recon views, and the clean radio-only logo. Remote ID fields are self-reported, per-observation data; they are kept separate from receiver GPS. Truncated payloads cannot be passed off as complete protocol packets, and a directed probe's requested SSID is not used as the transmitter's name.

Host checks passed:

- **56 native cases** in the PlatformIO native suite, plus **four GNSS command cases and six BLE AD-boundary cases** compiled directly with MinGW C++14, `-Wall -Wextra -Werror`: **66 native cases total**.
- **29 Python cases**: 19 exporter tests and 10 fake-serial tests.
- Draft 2020-12 meta-validation of all three schemas using isolated jsonschema 4.25.1, six shipped synthetic records, the configuration example, a synthetic raw Recon/Remote ID record, and two negative contract probes.
- Independent source review of mode-switch/Stop ordering, callback generation admission, log/display/alert thresholds, queue allocation, truncated inputs and probe-request attribution. UI host-harness checks are not a substitute for touchscreen acceptance.

The initial full build passed in **242.24 seconds**. After the final clean logo asset was embedded, the final build passed in **24.61 seconds**, with **2,423,687 / 6,553,600 bytes application flash** and **123,588 / 327,680 bytes static RAM**. Application binary length is **2,447,584 bytes**, SHA256 `dd41d000f2b9bc593dac0fe34fe1e09306c60e67030a02ec6b1246d07f38ffd0`. Factory image SHA256 is `540146f43b4b5c94873dd6d41eda6e489917dca51c6c5c5075e4292411c731b6`. Internal application checksum and digest validated. Artifact sizes and hashes are in the versioned build manifest.

The connected device identity matched the confirmed unit. The application-only write at `0x10000` covers erased sectors through `0x265FFF` inclusive, inside the independently verified recovery prefix ending at `0x372000`. The write's digest check and a separate `verify-flash` both passed before reset. No whole-chip erase or eFuse operation occurred. Private transcripts remain with the recovery set.

USB startup reported `version=0.2.0-dev ready=1 sd=1 touch=1 lora=1`, zero drops/errors and approximately 82 KiB free heap before scanning. A controlled mode test then showed:

| Phase | Recorded result |
| --- | --- |
| Recon, about 12 seconds | Last sampled totals 119 Wi-Fi frames, 329 BLE advertisements and four matched observations; LoRa RX remained zero. |
| Switch to Wardriver | Scanning stopped; saving progressed from 1 to 0 before a new session started. |
| Wardriver, about 12 seconds | Final 25 Wi-Fi observations, 460 BLE advertisements and three received LoRa packets. |
| Final Stop | `running=0 saving=0`, SD available, zero reported drops/errors. |

Four matches do not establish four physical devices or prove any named product was present. The test sampled counters rather than exporting private observed identities. Free heap settled near **71,092 bytes** during capture and **75,756 bytes** after Stop; free PSRAM was approximately **8,006,468 bytes**. These brief observations do not establish drive-duration stability or dense RF behavior.

The L76K accepted the documented output configuration sufficiently to emit GSA, GSV and TXT. At the last recorded GPS query, 418 sentences passed checksum and none failed; GGA quality remained 0, RMC V, GSA fix type 1, and satellites used 0. The latest-talker GSV count was 1; it must not be interpreted as a sum of all satellites. No exact `ANTENNA OPEN` warnings were received, which does not establish antenna RF health. No coordinates were printed or fabricated. See [GNSS diagnostics](GPS_DIAGNOSTICS.md) for interpretation; the serial transcript remains private.

**The user confirmed that the new Wardriver/Recon views and clean green radio logo look right.** Physical IO/USER-button acceptance is still pending. SD file readback, a valid outdoor GNSS fix, known-reference Recon/Remote ID detections, battery-only cold boot, endurance/resilience and live service uploads remain open. Native log contents and exported files have not been verified from this device's SD card. Credentials remain storage-only; automatic/manual network uploads are not implemented in this milestone.

## Installed 0.2.1-dev: complete satellite reports

This update retains the accepted 0.2.0 interface and radio paths and adds coordinate-free, per-talker GSV assembly. GP, GB, BD, GL and GN reports remain separate. Visible count, C/N0 values and report age update only after a complete, ordered group; incomplete or malformed groups cannot overwrite the last complete report. An exact documented `ANTENNA OK` response is distinguished from unknown text and OPEN history. It describes the receiver's antenna-status report, not RF reception health.

All **11 GNSS helper tests passed**, including seven new grouping, input-boundary and timeout cases. Combined with the unchanged, already-passed 56 native cases and six BLE AD cases, the current source has **73 passing native cases**; the 29 Python cases remain applicable. Build passed in **27.36 seconds**, with **2,425,091 / 6,553,600 bytes application flash** and **123,988 / 327,680 bytes static RAM**. Application binary length is **2,448,976 bytes**, SHA256 `5680770c9413cc5bfa2c43cde1f4e1bc57a687cde9e560916a7f674021646082`. Factory SHA256 is `9d04d72b83200f5f13d4fca33a30e8a48576b6acd7d7ffd68b0b7835ff119fa9`. Internal checksum and image digest passed. Resolved versions were retained with the private build record.

Before installation, the device reported a stopped, closed session with zero errors/drops; the confirmed device identity matched again. The application-only write remained within `0x10000..0x265FFF`, inside the scoped recovery prefix. Automatic verification and a separate verify operation both passed. Startup then reported `version=0.2.1-dev ready=1 sd=1 touch=1 lora=1`, zero reported drops/errors and 81,764 bytes free heap while stopped.

The recorded idle diagnostic window showed fresh complete reports as follows:

| Talker | Reported visible count | C/N0 observations |
| --- | --- | --- |
| GP (GPS) | Initially 0, later 1 | One positive value at 19 dB-Hz when reported |
| BD (BeiDou) | 0 | No C/N0 values |
| GL (GLONASS) | 1–2 | One or two positive values, maximum 29 dB-Hz |

At the final query in that window, 267 sentences had valid checksums, zero failed, and 35 exact antenna-OK reports had arrived. GGA quality was 0, RMC V, GSA fix type 1 and used-satellite count 0. **GPS is still not fixed.** These data establish usable UART/configuration communication and limited tracked signals; they do not identify the cause of acquisition failure. No warm/cold/factory reset or constellation command was sent. A same-location USB-versus-battery comparison and further reception checks remain useful physical next steps.

The build, image-inspection, GNSS-test and serial records remain private. The 0.2.0 mode/capture test remains the recorded radio-path test; it was not represented as a new radio test of 0.2.1. No SD-file readback or network upload was performed.

The packaged read-only `tools/bench_gps.py` was subsequently exercised for 15 seconds on the confirmed port. It passed with five fresh status/GPS responses, 15 validated sky rows, zero malformed diagnostics, zero omitted lines and `gps_fix=0`. At uptime approximately 257 seconds the receiver reported GP 1 / maximum C/N0 25 dB-Hz, BD 1 / maximum 30 dB-Hz and GL 2 / maximum 29 dB-Hz; 1,920 checksums passed and none failed. Antenna-OK reports totaled 255, but no fix was available. This later sample shows changing signal acquisition, not a positioning repair. The application remained stopped with SD available and zero reported errors/drops. The helper's syntax/help and eight injected parser/serial scenarios were also checked without hardware; its PASS text explicitly permits a no-fix diagnostic result.

## Installed 0.2.2-beta.1: bounded GNSS quiet comparison

Public-release assembly checks also passed: all **39 Python cases** (29 existing exporter/serial cases plus 10 web release tests) and **four Node DOM contract tests**. These reject changed source content, duplicate/unsafe archive entries, incompatible firmware metadata and unavailable installer metadata. They do not open a browser or a serial port and do not prove physical Web Serial flashing.

The beta adds an optional, bounded USB diagnostic that requires capture stopped and the logger idle. It shuts down radio activity and the display/backlight while preserving GNSS power, baud and configuration. An independent 90-second timer restarts the ESP32, and `tunnel resume` can restart sooner. It is not a receiver reset or a proposed GPS fix. Public documentation and synthetic UI previews were also prepared for the beta release.

The complete current host checks passed: **73 native cases in 14.02 seconds and 29 Python cases**. The firmware build passed in **28.67 seconds**. Generated artifact checks were:

| Artifact | Length | SHA-256 |
| --- | --- | --- |
| Application | 2,452,560 bytes | `949a93025d82e653d8361987adeea46fbbf014be4ecf3bf92cae06e80a90937d` |
| Merged factory image | 2,518,096 bytes | `5f9672fcbdeb55a1d5767d367496f601de00ef2e612d6c1561864e098bbecbe1` |

The factory image was inspected as ESP32-S3, DIO, 16 MB flash, 80 MHz, with valid internal image checks. The board/framework still uses QIO flash and OPI PSRAM; the distributable image's DIO header is recorded separately. The application-only write and an independent verify operation passed within the previously guarded recovery range. No full-chip erase or eFuse change occurred. USB startup reported `ready=1 sd=1 touch=1 lora=1` with zero drops/errors.

The quiet comparison reported `radio_shutdown=1 display_shutdown=1`. GNSS continued with fresh checksum-valid sentences and no fix throughout the recorded interval. The final quiet snapshot at approximately 88.8 seconds uptime reported 22,670 bytes, 657 accepted checksums and zero failed checksums; GP/BD visible counts were zero, while GL showed two visible with maximum C/N0 26 dB-Hz. These are individual samples, not whole-window maxima or aggregate satellite counts.

The independent timer restart was observed: the next startup sample at about 1.45 seconds uptime had `quiet=0 ready=1 sd=1 touch=1 lora=1`. A later status at about 5.53 seconds reported 81,740 bytes free heap, zero errors/drops and no fix; its sky sample showed GP three visible at maximum 25 dB-Hz, BD zero and GL two at maximum 25 dB-Hz. No new radio capture, SD-content readback, outdoor fix or upload acceptance is claimed from this update.

Before the update, 0.2.1 still reported no fix after roughly 34 minutes. The user reports that stock firmware previously fixed at the **same location with USB power**. The narrow quiet experiment did not repair positioning and does not prove a causal interference mechanism or a hardware defect. No warm/cold/factory receiver reset, baud change or constellation command was sent. GPS remains an unresolved beta limitation.

After the quiet test, a short beta capture regression passed fresh Start and Stop acknowledgments. The last in-run sample during the approximately ten-second observation window showed 16 Wi-Fi observations and 265 BLE advertisements. The final stopped/closed state, including the Stop request interval, reported **24 Wi-Fi observations, 325 BLE advertisements, zero LoRa packets, zero drops and zero errors**, `running=0 saving=0 sd=1 touch=1 lora=1`, and 81,504 bytes free heap. Zero LoRa packets in this short window is not a reception failure; the earlier 0.2.0 packet count remains separate evidence. This test did not read back SD content or establish a GPS fix.
