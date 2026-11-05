# Combined mesh development validation — 2026-11-05

Version: **0.5.0-dev**, branch `codex/combined-mesh-decoding`. This is a local development checkpoint, not a published release or physical-device acceptance record.

## Completed software checks

| Check | Result |
| --- | --- |
| Pinned Heltec V4-R8 Expansion V2 firmware build | Passed |
| Pinned Cardputer ADV / U214 firmware build and linker placement policy | Passed |
| Native parser/state/configuration suites | 174 tests across 20 suites passed through the actual PlatformIO runner |
| Python regression tests | 102 passed |
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

## Required physical acceptance

No flash, live capture, browser installation, GPS acquisition, upload receipt or drive was performed for this development checkpoint. Earlier firmware acceptance does not validate this decoder or scheduler.

Before publishing a new beta:

1. Identify the connected target and retain its current recovery image/configuration.
2. Check known Meshtastic and MeshCore transmitters separately with their exact radio settings; compare names, positions and raw packets.
3. Repeat in combined mode, including packet reception at a scheduled switch and long quiet intervals.
4. Check Settings mode save/reboot behavior, stop/start, shared SD writes and hardware compatibility on both targets.
5. Measure decoder/verification latency, input responsiveness, queue drops and runtime heap/stack headroom during sustained capture.
6. Verify GNSS loss/staleness, full/missing storage and orderly shutdown without substituting advertised positions for receiver fixes.

The remaining protocol and integration gaps are listed in [COMBINED_MESH.md](COMBINED_MESH.md#capability-and-validation-ledger). Passing these software checks does not establish full Kismet/LoRaPipe feature parity.
