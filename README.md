# LORA Tunnel

**Version 0.5.0 · MIT open source**

LORA Tunnel turns a supported handheld into a standalone Wi-Fi, Bluetooth Low Energy and LoRa wardriving recorder. It captures radio observations, adds receiver time and location when GNSS provides them, and saves sessions to microSD for later review and export. A separate Recon mode groups supported Wi-Fi/BLE indicators and provides optional audible alerts.

The firmware runs on **Heltec WiFi LoRa 32 Expansion Kit V2 / V4-R8** and **M5Stack Cardputer ADV with Cap LoRa-1262 (U214)**. Each board has its own binary. Capture runs on the handheld without a laptop, phone or Kismet server.

> **Fork:** Cardputer ADV dual boot over OTA next to another firmware, with a Power menu entry that returns to it. See [Dual boot](docs/CARDPUTER_ADV.md#dual-boot-with-another-firmware-fork).

[Install with the web flasher](https://valleytech-lora-tunnel.pages.dev/) · [Download 0.5.0](https://github.com/valleytechsolutions/LORA-Tunnel/releases/tag/v0.5.0) · [Project website](https://valleytechsolutions.tech/pages/lora-tunnel) · [Gitea distribution](https://git.churchofmalware.org/Valleytech/LORA-Tunnel)

## What changed in 0.5.0

Wardriver can now listen to **Meshtastic**, **MeshCore**, or alternate between both. Meshtastic decoding includes supported node information, positions, ordinary text and device battery/voltage telemetry. MeshCore decoding includes public node announcements and Ed25519 signature verification. Every LoRa observation retains its raw bytes and the radio settings used to receive it.

Combined mode uses one selected profile per protocol and a default 30-second dwell. The SX1262 receives one configuration at a time; it cannot monitor both protocols simultaneously or hear traffic on every preset. Existing configurations remain Meshtastic-only until the user changes the mode. [Supported fields, settings and limits](docs/COMBINED_MESH.md) · [Release notes](docs/RELEASE_0_5_0.md).

## Supported hardware

| Target | Required hardware | Interface and memory |
| --- | --- | --- |
| Heltec | [WiFi LoRa 32 Expansion Kit V2 / V4-R8](https://heltec.org/project/v4-r8-ex/), 863–928 MHz variant | 240 × 320 touchscreen, 16 MB flash, 8 MB PSRAM, onboard GNSS and microSD |
| Cardputer | [Cardputer ADV](https://docs.m5stack.com/en/core/Cardputer-Adv) with [Cap LoRa-1262, U214](https://docs.m5stack.com/en/cap/Cap_LoRa-1262), 868–923 MHz | 240 × 135 display, physical keyboard, 8 MB flash, no PSRAM; cap supplies LoRa and GNSS |

The Heltec V1, low-band Heltec kits, original Cardputer and other M5Stack caps are separate hardware. Check the board and fitted radio before installing. [Heltec hardware notes](docs/HARDWARE.md) · [Cardputer setup](docs/CARDPUTER_ADV.md).

## Wardriver and Recon

| Function | Behavior |
| --- | --- |
| Wi-Fi | Passive 2.4 GHz AP observations with BSSID, available SSID, channel, RSSI and security metadata. Current Wi-Fi scan channels are 1–11. |
| BLE | Passive advertisements with address type, RSSI, available name and service/manufacturer data. Random addresses are not permanent device identities. |
| LoRa | Receive-only Meshtastic/MeshCore profiles, local supported-payload decoding, RSSI/SNR, raw bytes, per-packet profile snapshots and slow switching. |
| GNSS | Receiver UTC, fix validity, age and available quality information. Missing or stale coordinates remain explicit. Mesh-advertised positions are separate. |
| Storage | Versioned JSONL sessions with bounded queues, checked buffered writes and visible drop/error counters. Repeated sightings remain in the log. |
| Recon | Separate passive Wi-Fi/BLE evidence views, category filters, optional SD rules and once-per-device-per-session alerts. LoRa stops in Recon. |
| Export/upload | Local WiGLE CSV export and optional idle-only WiGLE, Wardrift and WDGWars CSV uploads. Mesh export has a separate, limited contract. |

Wi-Fi and BLE share the ESP32-S3 radio and run on a schedule. A Recon match is an indicator, not proof of ownership, recording or identity. A relayed mesh packet does not establish that its original sender is nearby.

| Heltec Home | Wardriver | Recon |
| --- | --- | --- |
| ![Heltec Home](assets/ui/home.png) | ![Heltec Wardriver](assets/ui/wardriver.png) | ![Heltec Recon](assets/ui/recon.png) |

| Cardputer Home | Wardriver | Recon |
| --- | --- | --- |
| ![Cardputer Home](assets/ui/cardputer/home.png) | ![Cardputer Wardriver](assets/ui/cardputer/wardriver.png) | ![Cardputer Recon](assets/ui/cardputer/recon.png) |

These interface previews are rendered from the drawing code with synthetic data. They are not photographs or recorded networks. [Interface guide](docs/UI_GUIDE.md) · [Preview details](assets/ui/README.md).

## Install and start a session

1. Identify the exact board and retain its current recovery image. Read the [Heltec recovery procedure](docs/RECOVERY.md) or [Cardputer installation guide](docs/CARDPUTER_ADV.md).
2. Prepare a FAT32 microSD card with the supplied `lora-tunnel` folder. Copy `config.example.json` to `config.json` **on the card**. Keep an existing private configuration when updating; the release starter contains empty credentials. [SD setup](sdcard/README.md).
3. Select the matching board in the [web flasher](https://valleytech-lora-tunnel.pages.dev/). Factory images are merged binaries for USB installation at `0x0`. A Cardputer factory install replaces Launcher.
4. To retain Bmorcelli Launcher, download the separate [Cardputer Launcher application](https://github.com/valleytechsolutions/LORA-Tunnel/releases/download/v0.5.0/LORA-Tunnel-0.5.0-Cardputer-ADV-LoRa1262-LAUNCHER-APP.bin) and install it through Launcher's SD menu. Never flash an application-only BIN at `0x0`.
5. On first boot, select the LoRa region for your location or choose **Skip LoRa**. LoRa remains off until a supported region is selected. While stopped, Settings → LoRa cycles through Meshtastic, MeshCore and Both / slow scan.
6. Open Wardriver and start a session. Check storage and GPS status. Use **Stop + Save** and wait for saving to finish before removing power or the card. Native sessions are stored under `/sessions/`.

Heltec uses touch controls, with optional physical navigation buttons. Cardputer uses its keyboard. Software shutdown saves before sleep; wake controls differ by board. [User guide](docs/USER_GUIDE.md) · [Web flasher guide](docs/WEB_FLASHER.md) · [Cardputer controls](docs/CARDPUTER_ADV.md).

## Configuration and uploads

The SD configuration holds radio mode, dwell time, region, detection preferences and optional credentials. The default MeshCore profile is a regional starting point, not automatic discovery of the local network. Match its frequency, bandwidth and spreading factor to the network being observed. Up to four Meshtastic channel keys can be configured privately; unknown or unsupported encrypted payloads remain recorded as such.

WiGLE, Wardrift and WDGWars uploads are opt-in and run only while capture is stopped and saved. Automatic uploads are off by default. Configure home Wi-Fi and the credentials for the chosen service; starting capture interrupts upload work. The Heltec provides an on-screen credential keyboard and the Cardputer uses its physical keyboard. Credentials are plaintext on the removable card and must stay out of source control and shared logs. [Upload setup and eligibility](docs/UPLOADS.md).

WiGLE export requires valid receiver coordinates and UTC. Native logs retain observations that do not qualify. Mesh observations are not ordinary WiGLE rows; the WDGWars mesh candidate exporter remains separate from CSV uploads and its live ingestion is unverified. [Log schema](docs/LOG_FORMAT.md) · [Export instructions](docs/EXPORTS.md).

## Build and flash from source

Use Python 3.10 or newer and PlatformIO Core 6.2.0. The platform and libraries are pinned in `platformio.ini`; each board's wiring and memory limits are isolated in its board configuration.

```sh
python -m pip install platformio==6.2.0
python -m platformio run -e heltec-v4-r8-tft
python -m platformio run -e cardputer-adv-lora1262
```

Select the actual USB port before upload. Keep firmware, partition table and bootloader from the same build. The Heltec test unit requires the documented [esptool 5.4.0 legacy-stub fallback](docs/RECOVERY.md#legacy-stub-installation-fallback) when the default loader fails. A successful build is distinct from a verified flash and a successful radio capture.

Release downloads include board-specific factory images, the Cardputer Launcher app, source, separate dependency/relinking archives, an SD starter and SHA-256 checksums. [Rebuild and licensing materials](docs/RELINKING.md).

## Validation and current limits

The [release record](docs/RELEASE_0_5_0.md) separates source tests, build/image checks, device observations and unverified behavior. The [combined mesh record](docs/COMBINED_MESH_VALIDATION.md) documents the Heltec's live Meshtastic decoding, profile switching and saved-mode restart check.

MeshCore public announcements are implemented, but a known-source MeshCore reception has not yet been verified on hardware. MeshCore encrypted group/direct chat, newer Meshtastic AEAD/PKI modes, arbitrary preset lists and Kismet datasource/PCAP-NG integration are outside this release. Live service upload receipts, physical Launcher installation/return and sustained drive acceptance also remain open. Removing the beta suffix does not turn those pending checks into completed tests.

## Credits and license

Original LORA Tunnel code is released under the [MIT License](LICENSE). Third-party components keep their own licenses. [Full attribution and license inventory](docs/THIRD_PARTY.md).

LoRaPipe and Kismet inspired the capture/profile approach and informed protocol research. LORA Tunnel implements the capture, display, storage and supported decoding paths locally. Meshtastic and MeshCore protocol definitions informed the original parsers. RadioLib supplies the SX1262 driver; Arduino-ESP32, NimBLE-Arduino, TinyGPSPlus, ArduinoJson, LovyanGFX and the M5Stack libraries support the hardware. Crypto and the retained libsodium-derived validation constants have their corresponding notices.

Recon includes the attributed Dragon Detector parser subset and inherited ESP32Marauder/Flock Noir credits; OpenDroneID retains its Apache-2.0 license. The browser installer uses ESPHome's ESP Web Tools with its notices. Heltec and M5Stack are the hardware manufacturers. LORA Tunnel is an independent Valleytech Solutions project.
