# LORA Tunnel 0.5.0

Version 0.5.0 brings Meshtastic and MeshCore observation to both supported handhelds. It is the first release without a beta suffix. The supported feature set and the physical tests below define its scope; pending checks remain pending regardless of the release label.

## Radio observation

- Choose Meshtastic, MeshCore or Both / slow scan while the session is stopped. The setting saves to microSD.
- Combined mode alternates one selected profile for each protocol. Default dwell is 30 seconds, configurable up to 600 seconds. Receive work is serviced before switching, with bounded deferral for a packet already in progress.
- Meshtastic decoding covers supported node information, ordinary text, positions and device battery/voltage telemetry. Public/default-key decoding and up to four privately configured channel keys are supported.
- MeshCore public announcements expose the available name, public identity key, reported role and advertised position. Ed25519 verification is reported separately from parsing; failed signatures cannot replace node summaries.
- Packet logs retain raw bytes, receive settings, RSSI/SNR and supported protocol metadata. Receiver coordinates remain separate from advertised node coordinates.
- USB diagnostics expose allowlisted receiver settings, listen times, switches and decoder counts. They do not print payloads, credentials or positions.

The SX1262 receives one modem configuration at a time. Neither combined scanning nor decoding discovers every network or makes a missing packet recoverable. [Profiles, configuration and supported fields](COMBINED_MESH.md).

## Hardware and downloads

| Download | Hardware and use |
| --- | --- |
| `LORA-Tunnel-0.5.0-Heltec-V4-R8-TFT-HIGH-BAND-FACTORY-0x0.bin` | Heltec Expansion Kit V2 / V4-R8, 863–928 MHz variant; USB/web installation at address zero |
| `LORA-Tunnel-0.5.0-Cardputer-ADV-LoRa1262-FACTORY-0x0.bin` | Cardputer ADV with U214 Cap LoRa-1262; standalone USB/web/M5Burner installation at address zero |
| `LORA-Tunnel-0.5.0-Cardputer-ADV-LoRa1262-LAUNCHER-APP.bin` | The same Cardputer application, installed through Bmorcelli Launcher's SD menu |

Factory installation replaces the current firmware, including Launcher. Never write the Launcher application at address zero. Retain a recovery copy for the same device and preserve private SD configuration before installing.

The release includes one application source archive, a separate dependency/relinking archive for each board, a blank-credential SD starter, and SHA-256 checksums. [GitHub downloads](https://github.com/valleytechsolutions/LORA-Tunnel/releases/tag/v0.5.0) · [Web flasher](https://valleytech-lora-tunnel.pages.dev/) · [Cardputer instructions](CARDPUTER_ADV.md) · [Heltec recovery](RECOVERY.md).

## Validation

The pinned firmware builds pass for both targets. The final release tooling passed 109 Python tests and seven installer control tests; the unchanged firmware implementation had already passed 174 native tests across 20 suites and the focused adapter/crypto checks listed in the detailed record. Host checks cover packet boundaries, cryptographic fixtures, profile-switch races, memory/queue limits, configuration lifetime, logger capacity, upload lifecycle, keyboard mapping and installer controls. Image checks bind the application, factory image, source archive and dependency material to the release artifacts.

On the preceding 0.5.0 development application, the Heltec received 15 Meshtastic packets with 14 decoder successes, followed by a combined-mode session with four profile switches and three further Meshtastic decoder successes. That combined session recorded 77,735 ms on Meshtastic and 60,006 ms on MeshCore, zero reported drops/errors/allocation failures, and a completed stop/save transition. Both / slow scan remained selected after reboot. These are development-image observations; the [detailed record](COMBINED_MESH_VALIDATION.md) retains their exact scope and image identity.

Both final 0.5.0 applications were installed at `0x10000` on 2026-11-05. Write-time verification and a separate `verify-flash` passed on each board. Existing partitions, NVS and SD files were preserved. Heltec recovery consists of matching scoped prefix/tail reads; the Cardputer has two matching full 8 MB recovery reads. Restoration was not tested.

| Final-image check | Heltec Expansion V2 | Cardputer ADV + U214 |
| --- | --- | --- |
| Startup | SD, touch/input and LoRa ready; zero errors/drops/allocation failures | SD, keyboard/input and LoRa ready; zero startup errors/drops/allocation failures |
| Saved mode / controls | Both mode retained through update | User confirmed working screen/navigation and saved Both mode; USB confirmed mode 2 |
| Further capture | Live decoding was checked on the preceding development image | Nine profile switches; 156,376 ms Meshtastic and 151,105 ms MeshCore listening; no LoRa packet received in this sample |
| Capture pressure | Development-image results linked above | 944 Wi-Fi and 11,436 BLE observations, 55 reported queue drops, zero errors/allocation failures |
| Cardputer headroom / responsiveness | — | Minimum internal free 33,444 bytes; largest block 27,636; loop/writer stack watermarks 4,416 / 3,068 bytes; 26 UI samples at 17 ms maximum |
| GNSS / stop | No new fix claim for the final image | Valid fix with five satellites reported; Stop + Save completed with storage ready |

Cardputer reported two bounded receive deferrals/timeouts and zero forced interruptions. No packet was attributed to them. Queue drops show that busy capture can exceed the bounded logging pipeline; this is not a lossless or sustained-drive guarantee. The UI samples do not measure end-to-end key latency. The GNSS flag is not independent SD geotag verification. Host fixtures and firmware counters are not independent RF measurements.

| Artifact | SHA-256 |
| --- | --- |
| Heltec application | `493b7c66d020f736e259662a7241b8dffd9727d2500d25d42d85961eee377656` |
| Heltec factory | `8c6447f0df52511a7291979d0f276619bda44a6b8882ebbc0bc3aba22856f1d5` |
| Cardputer application / Launcher | `476e65488bb15183bfd37702c5217d48402df07ae5a7b4fae1118a11808721ed` |
| Cardputer factory | `28da8c73521c13de1a0e30ebe1bf713e6c93bf76aae4a3665911d0cf9c57a905` |

The release checksum file covers the downloadable archives as well. Older published releases remain unchanged.

## Known limitations

- No known-source MeshCore packet has yet been received in a controlled hardware check. Public-announcement decoding and signature handling pass host fixtures; the live radio path still needs that check.
- MeshCore encrypted group/direct chat, newer Meshtastic AEAD/PKI modes, compressed text and unsupported application payloads remain encrypted/unknown or unsupported. The original raw packet is retained.
- Only one selected profile per protocol is scheduled. Arbitrary preset lists, a Kismet datasource bridge and PCAP-NG export are not included.
- Live WiGLE, Wardrift and WDGWars firmware upload receipts, physical browser flashing, Launcher installation/return, sustained-drive behavior and several power/GNSS/storage fault scenarios remain unverified on the final release.
- WDGWars mesh ingestion is distinct from Wi-Fi/BLE CSV upload. The native decoded fields do not establish successful mesh export or server acceptance.

## Updating

Keep the existing `/lora-tunnel/config.json` and session files. Older configurations default to Meshtastic-only. To use both protocols, stop and save, then choose Settings → LoRa → Both / slow scan. Match the regional MeshCore preset to the local community; `AUTO` chooses a starting preset and does not detect the local network.

New installations leave LoRa off until a region is selected. Automatic uploads remain opt-in. A firmware update does not require uploading credentials, logs or private channel keys to any project repository.

Original project code remains MIT licensed. Third-party licenses, library sources and protocol attribution are retained in [THIRD_PARTY.md](THIRD_PARTY.md) and the matching [relinking materials](RELINKING.md).
