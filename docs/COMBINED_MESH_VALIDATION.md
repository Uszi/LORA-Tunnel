# Combined mesh development validation — 2026-11-05

Version: **0.5.0-dev**, branch `codex/combined-mesh-decoding`. This is a local development checkpoint, not a published release. The Heltec installation and idle check below are limited physical evidence; live decoder and combined-scanning acceptance remain pending.

## Completed software checks

| Check | Result |
| --- | --- |
| Pinned Heltec V4-R8 Expansion V2 firmware build | Passed |
| Pinned Cardputer ADV / U214 firmware build and linker placement policy | Passed |
| Native parser/state/configuration suites | 174 tests across 20 suites passed through the actual PlatformIO runner |
| Python regression tests | 104 passed after the USB radio telemetry addition, including 20 bench-helper cases |
| Actual scanner transition fixture | 27 checks passed: dwell, shared SPI contention, complete/in-progress frame races, retune/rearm failures and protocol attribution |
| Capture queue/AP inventory fixtures | 173 queue checks and 278 AP-inventory checks passed |
| Configuration lifetime/allocation fixtures | 370 Cardputer and 91 Heltec checks passed |
| Actual logger serializer capacity | 32 checks passed; maximum projected target arena use 3,084 of 4,096 bytes |
| Actual Cardputer UI/keyboard adapters | 154 UI checks and 214 SDK keyboard assertions passed |
| Actual uploader/session/BLE maintenance fixtures | 17,127 Cardputer, 16,869 Heltec, 97 session/marker/alert and 42 BLE lifecycle checks passed |
| Installer control regression tests | 6 passed; this does not exercise a browser or USB flash |
| Schemas and shipped examples | 3 schemas and 9 example documents/records validated |
| Generated image validation | Heltec factory and Cardputer factory/Launcher images passed layout, identity, integrity and version checks |

Cryptographic interoperability checks use NIST AES-128/AES-256 CTR vectors, an independently generated encrypted Meshtastic NodeInfo packet and a signed synthetic MeshCore advert. The actual parsers run against pinned rweather AES/Ed25519 on the host. Tests reject altered keys, signatures, timestamps and names, identity/low-order point forgeries and noncanonical scalars. Mutable relay paths remain outside the signed advert data. The ESP32 build uses SDK mbedTLS for AES; its actual execution on hardware remains unverified by the host fixture.

Mesh decoding and Recon classification share writer-record storage. Cardputer ingress records are 1,328 bytes and writer records 1,648 bytes; all 12 ingress and 8 writer slots remain available. The receive-profile snapshot adds 48 bytes per record. Tests preserve raw bytes, complete public-key identity for MeshCore deduplication and separate receiver/advertised coordinates. These sizes and linker results do not establish runtime heap, stack or latency headroom on a device.

Packaging rejects nonempty upload credentials and private mesh-key lists in public example configuration. Test keys and packets are synthetic/public fixtures. No captured sessions, device coordinates, live credentials or private SD files were used in these tests.

## Heltec installation and idle check

On 2026-11-05, the attached Heltec identified as ESP32-S3 revision 0.2 with 16 MB flash and embedded 8 MB PSRAM. Its existing firmware reported `0.3.0-beta.1`, idle with SD, touch and LoRa ready. Two fresh acquisitions of the recovery prefix `0x000000..0x372000` and coredump tail `0xFF0000..0x1000000` matched independently. The saved application image digest passed, and its partition table matched the development build. These are scoped recovery copies, not a newly verified full-chip backup; restoration remains untested. See [RECOVERY.md](RECOVERY.md) for the older full-chip read discrepancy and the bounds of this recovery procedure. Device-specific files remain private and excluded from source archives.

An initial legacy-stub attempt failed at flash-size configuration before writing anything. A ROM read probe also hit a Windows console-encoding exception. A fresh reset, UTF-8 output and successful small legacy-stub read preceded the matching recovery acquisitions. The completed reads and installation used the pinned esptool 5.4.0 legacy stub at 2,000,000 baud.

The application-only image was written at `0x10000`; the flasher reported the erase range `0x00010000..0x002BFFFF` inclusive, inside the saved prefix. No factory image, full-chip erase, NVS erase, SD edit or eFuse change was part of this installation. Both the write-time digest verification and separate `verify-flash` succeeded before reboot.

- Installed application length: **2,815,920 bytes**.
- Installed application SHA-256: **`febbf4f28042c757060a629528e72ed778b13d4b61a459b62908158aa6fd20f5`**.
- Both firmware targets rebuilt successfully after adding allowlisted USB radio telemetry (Heltec 28.551 s; Cardputer 33.656 s). The Cardputer factory and Launcher image validator passed; no Cardputer installation occurred in this check.
- Startup reported `0.5.0-dev`, target `heltec-v4-r8-tft`, ready with SD, touch controller and LoRa initialized. Forty-five consecutive polled statuses covered uptime 44,615–89,289 ms without reported errors, drops, allocation failures or reset diagnostics.
- Idle free heap remained **101,448 bytes**, minimum internal free memory **100,676**, largest internal block **61,428**, loop stack watermark **4,444** and writer stack watermark **6,512**. These are an idle sample, not capture or decoder-load headroom measurements.
- GNSS serial data advanced to **17,311 characters**; no position fix was observed in this window. This does not supersede previous working-GPS evidence or establish a new GPS fault.
- The retained mode was Meshtastic-only: **906.875 MHz, BW 250 kHz, SF11, CR 4/5, sync 0x2B**. No capture was started in this idle check. Combined switching, on-screen acceptance, packet reception and decoding remain unverified on this build.

## Required physical acceptance

No live capture, browser installation, GPS acquisition, upload receipt or drive was performed for this development checkpoint. Earlier firmware acceptance does not validate this decoder or scheduler.

Before publishing a new beta:

1. Retain the verified Heltec recovery files; identify and back up each additional target before installation. Preserve private SD configuration before editing it.
2. Check known Meshtastic and MeshCore transmitters separately with their exact radio settings; compare names, positions and raw packets.
3. Repeat in combined mode, including packet reception at a scheduled switch and long quiet intervals.
4. Check Settings mode save/reboot behavior, stop/start, shared SD writes and hardware compatibility on both targets.
5. Measure decoder/verification latency, input responsiveness, queue drops and runtime heap/stack headroom during sustained capture.
6. Verify GNSS loss/staleness, full/missing storage and orderly shutdown without substituting advertised positions for receiver fixes.

The remaining protocol and integration gaps are listed in [COMBINED_MESH.md](COMBINED_MESH.md#capability-and-validation-ledger). Passing these software checks does not establish full Kismet/LoRaPipe feature parity.
