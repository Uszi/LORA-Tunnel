# Combined mesh development validation — 2026-11-05

Version: **0.5.0-dev**, branch `codex/combined-mesh-decoding`. This is a local development checkpoint, not a published release. The records below cover installation, live Meshtastic decoder counters, combined-profile switching and saved-mode restart behavior. Controlled MeshCore reception remains unverified.

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

Cryptographic interoperability checks use NIST AES-128/AES-256 CTR vectors, an independently generated encrypted Meshtastic NodeInfo packet and a signed synthetic MeshCore advert. The actual parsers run against pinned rweather AES/Ed25519 on the host. Tests reject altered keys, signatures, timestamps and names, identity/low-order point forgeries and noncanonical scalars. Mutable relay paths remain outside the signed advert data. The ESP32 build uses SDK mbedTLS for AES. The later live Meshtastic observations exercise the device decoder path; the host fixture itself does not validate ESP32 execution.

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
- The retained mode was Meshtastic-only: **906.875 MHz, BW 250 kHz, SF11, CR 4/5, sync 0x2B**. No capture was started in this initial idle check.

## Subsequent Meshtastic observation

The user subsequently confirmed that the screen responds, that the Both / slow scan selection saved, and that Meshtastic decoded on the device. A later USB check at approximately 4,640 seconds uptime found the device stopped with **15 Meshtastic receptions**, **14 decoder successes**, **one other result**, **85,463 ms of Meshtastic listening time**, and zero reported drops, errors or allocation failures. The current mode at this later check was **Meshtastic-only**, with zero switches and zero MeshCore listening time. This records the observed mode without assuming why it differed from the earlier user report.

These results provide live Meshtastic receive/decoder-path evidence on the installed application. No captured identities, messages, positions or credentials were printed. Packet fields have not been compared against a known transmitter's values, the SD session has not been read back, and the one other result has not been classified. Decoder success is not sender authentication. This first session does not verify a MeshCore reception or combined-profile switching. The next check isolates the combined-mode result.

## Subsequent combined-mode check

After the user reselected Both / slow scan, USB telemetry confirmed mode 2 and both receive profiles. A short capture recorded **four profile switches**, **77,735 ms on Meshtastic**, **60,006 ms on MeshCore**, **three Meshtastic receptions and three decoder successes**, with no MeshCore receptions. The profiles were Meshtastic 906.875 MHz / 250 kHz / SF11 / CR4/5 / sync 0x2B and MeshCore 910.525 MHz / 62.5 kHz / SF7 / CR4/5 / sync 0x12.

The firmware reported zero switch deferrals, receive timeouts, forced interruptions, queue drops, errors or allocation failures. The same session accumulated 297 Wi-Fi and 4,038 BLE observations. Minimum internal free memory was 96,008 bytes; the largest block was 59,380 bytes, with loop and writer stack watermarks of 3,768 and 4,976 bytes. Stop completed with capture stopped, saving finished, storage ready and no upload active. Both / slow scan remained selected after reboot and the later application-only 0.5.0 update.

These are firmware counters from the development image identified above, not an independent RF measurement. No known MeshCore transmitter was available. Profile initialization and scheduled listening do not establish MeshCore packet reception. No private packet contents or coordinates are included here.

## Remaining physical checks

Final 0.5.0 image installation, Cardputer navigation and its combined-capture results are recorded separately in [RELEASE_0_5_0.md](RELEASE_0_5_0.md). Further testing should cover:

1. Known Meshtastic and MeshCore transmitters with exact radio settings, comparing decoded fields and saved raw packets.
2. Packets that cross a scheduled profile switch, plus extended quiet intervals and sustained captures.
3. Independent SD-session/geotag readback and measured decoder/signature latency under load.
4. Browser and Launcher installation, return to Launcher, and board-specific power behavior.
5. GNSS loss/staleness, storage faults and live upload receipts.

The protocol and integration limits are listed in [COMBINED_MESH.md](COMBINED_MESH.md#capability-and-validation-ledger). These results do not establish full Kismet/LoRaPipe feature parity.
