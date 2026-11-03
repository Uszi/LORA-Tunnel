# Third-party research and attribution

Research and source imports checked on 2026-11-01. Original LORA Tunnel application code is distributed under the [MIT license](../LICENSE), copyright (c) 2026 Valleytech Solutions. This file records the LoRa design references, imported Recon implementation, and direct firmware dependencies. Upstream material retains its own license, and framework packages retain their individual component notices. The root license does not replace third-party license terms or original copyright notices.

| Project | Inspected revision | License at that revision | Relationship to LORA Tunnel |
| --- | --- | --- | --- |
| [krak3n77/lorapipe](https://github.com/krak3n77/lorapipe) | `4ae9f86bb8256f8084f50577128b0facdca7bee2` | [MIT](https://github.com/krak3n77/lorapipe/blob/4ae9f86bb8256f8084f50577128b0facdca7bee2/license.txt), notice credits Scott Powell / rippleradios.com (2025) | Architectural reference for raw LoRa reception and metadata transport; no implementation code incorporated in `mesh_header.h`. The repository has MeshTNC/MeshCore ancestry and the README identifies DataParty.js. Preserve relevant file notices if code is later reused. |
| [kismetwireless/kismet](https://github.com/kismetwireless/kismet) | `28a48f1349a0d9c4e80a9435ff9ce75a002a4bcd` | [GPLv2 default](https://github.com/kismetwireless/kismet/blob/28a48f1349a0d9c4e80a9435ff9ce75a002a4bcd/LICENSE); inspected `phy_meshtastic.cc` explicitly allows GPLv2 or later | Source research for the split between capture helper and host-side decoding, tracking, and logging. No Kismet code incorporated. Check each component's notice before any future port. |
| [meshtastic/firmware](https://github.com/meshtastic/firmware) | `86d4d4a06c805bc0ccd812078c0ed1d8eb8a4c54` | [GPLv3 license text](https://github.com/meshtastic/firmware/blob/86d4d4a06c805bc0ccd812078c0ed1d8eb8a4c54/LICENSE) | Protocol and radio-profile reference for an original byte-wise header parser. No upstream implementation, generated protobuf files, cryptographic code, or private channel keys copied. |

The header parser was written for LORA Tunnel from the documented field layout. It does not implement the Meshtastic network stack, encryption, routing, or rebroadcasting. LORA Tunnel is an independent project, and these references do not imply upstream endorsement or hardware acceptance.

Read-only source snapshots downloaded under `research/lora-source` are inspection material, not firmware dependencies. Keep them out of distributable firmware source archives unless deliberately bundling complete upstream notices and provenance. Dependency libraries added elsewhere in the project require their own pinned-version and license inventory. If LoRaPipe code is later adapted, retain its MIT notice and identify the exact adapted files. Porting Kismet or Meshtastic implementation code requires a fresh license review and appropriate notices/source obligations.

## Imported Recon sources and retained notices

| Component | Pinned revision | License and retained evidence | Relationship to LORA Tunnel |
| --- | --- | --- | --- |
| Dragon Detector / Valleytech Custom Solutions | `7a09930ee8ab112b9b87e94c1c9e47e5e6faaa21` | [Original MIT notice](../lib/recon/LICENSE-DragonDetector); [source manifest and original file hashes](../lib/recon/source-manifest.json); [import notes](../lib/recon/UPSTREAM.md) | The passive parser subset from `detection.h`, `axon.h`, `remote_id.h`, and `vendor_prefixes.h` is imported under `lib/recon/src/dragon`. LORA Tunnel supplies its own bounded adapter and result API, with modified source files marked. Copyright (c) 2026 Valleytech Custom Solutions, Your Pal Kal / `@valleytechsolutions`. |
| [OpenDroneID core C](https://github.com/opendroneid/opendroneid-core-c) | `849c03adbe0cdd68997a462fafab397edcfe53f3` | [Apache-2.0 license](../lib/opendroneid/LICENSE); [upstream record and SHA-256 hashes](../lib/opendroneid/UPSTREAM.md) | The unmodified `opendroneid.c` and `opendroneid.h` are vendored in `lib/opendroneid/src` and used for Remote ID decoding. Original Intel Corporation/OpenDroneID copyright and SPDX notices remain. The source/header/license hashes were checked against this immutable upstream revision. |
| [SquachWatch-CYD](https://github.com/skizzophrenic/SquachWatch-CYD) | `3e9be6c02265b14c798e58da01a0913866143b0f` | GPL-3.0 repository license; [local public-identifier coverage ledger](../lib/recon/data/squachwatch-coverage.json) | Research and public signature/protocol facts inform feature coverage. SquachWatch application implementation, UI and artwork are not copied into this firmware. |

The Dragon Detector import carries inherited **Just Call Me Koko / ESP32Marauder** attribution. Its source explicitly identifies adapted Pwnagotchi peer-beacon handling and Marauder-informed detection heuristics and protocol identifiers. The [original Marauder MIT notice](../lib/recon/notices/MARAUDER-LICENSE.txt), copyright (c) 2020 Just Call Me Koko, is retained. The source project's [archived credits](../lib/recon/DRAGON-CREDITS.md) record its inspected Marauder revisions, including `8ae4622abcc9c9c5729d4e97491907581d7f0c34` and the later audit reference `d3ceb28afeda1196ca27c13adeaa69ada52383f3`. These describe the imported subset's provenance; LORA Tunnel is not a complete Marauder port.

The [Flock Noir MIT notice](../lib/recon/notices/FLOCK-NOIR-LICENSE.txt) is also retained. The archived Dragon credits identify its reference snapshot `0559417e4f5e96f615ca4c4b42c3e0f0d56e2337` and credit Colonel Panic / Flock-You, NitekryDPaul, DeFlockJoplin, and Will Greenberg for the associated research. Those archived credits also discuss other Dragon Detector features and paths that are outside this import; they do not claim that every such feature is bundled here.

The imported vendor data contains 201 public-fact rows with per-row evidence and confidence. The SquachWatch coverage ledger records 158 identifier facts at the pinned revision. Both remain research/evidence datasets, not authentication of device identity or activity. The local library directories, original notices, import manifests, and marked adaptations are part of the distributable source package. OpenDroneID remains Apache-2.0; its inclusion does not replace the MIT terms on the Dragon subset or adapter.

## Direct firmware dependencies

| Dependency | Pinned version | License / source |
| --- | --- | --- |
| pioarduino platform | 55.03.312-1 | [Platform build scripts](https://github.com/pioarduino/platform-espressif32/tree/55.03.312-1), Apache-2.0 base |
| Arduino-ESP32 | 3.3.12 | [Espressif core](https://github.com/espressif/arduino-esp32/tree/3.3.12), LGPL-2.1-or-later core with component-specific exceptions/notices; ESP-IDF dependencies have their own licenses |
| LovyanGFX | 1.2.24 | [Library](https://github.com/lovyan03/LovyanGFX/tree/1.2.24), BSD-2-Clause project plus bundled component notices in license.txt |
| NimBLE-Arduino | 2.5.2 | [Library](https://github.com/h2zero/NimBLE-Arduino/tree/2.5.2), Apache-2.0; bundled NimBLE/tinycrypt notices retained |
| RadioLib | 7.8.1 | [Library](https://github.com/jgromes/RadioLib/tree/7.8.1), MIT |
| TinyGPSPlus | 1.1.0 package | [Library](https://github.com/mikalhart/TinyGPSPlus), LGPL-2.1-or-later per source headers |
| ArduinoJson | 6.21.5 | [Library](https://github.com/bblanchon/ArduinoJson/tree/v6.21.5), MIT |
| M5Cardputer (Cardputer target) | `2d4fa6646e4e5b47e0af96214b003aa7b15b8d81`; upstream manifest reports 1.1.1 | [Pinned source](https://github.com/m5stack/M5Cardputer/tree/2d4fa6646e4e5b47e0af96214b003aa7b15b8d81), MIT SPDX notices; bundled adapted Adafruit TCA8418 driver remains BSD-3-Clause, with supplemental full notice in `third_party/licenses/Adafruit-TCA8418-LICENSE.txt` |
| M5Unified (Cardputer target) | 0.2.29 | [Library](https://github.com/m5stack/M5Unified/tree/0.2.29), MIT |
| M5GFX (Cardputer target) | 0.2.36 | [Library](https://github.com/m5stack/M5GFX/tree/0.2.36), MIT with bundled font/component notices preserved |
| IRremote (Cardputer package dependency) | 4.5.0 | [Library](https://github.com/Arduino-IRremote/Arduino-IRremote/tree/4.5.0), MIT; resolved M5Cardputer package dependency, not an infrared feature of LORA Tunnel |
| LORA Tunnel Recon adapter / Dragon Detector subset | Local adapter 0.1.0; Dragon revision `7a09930ee8ab112b9b87e94c1c9e47e5e6faaa21` | MIT subset and adapter; inherited notices retained under `lib/recon`, as detailed above |
| OpenDroneID | Vendored revision `849c03adbe0cdd68997a462fafab397edcfe53f3` | Apache-2.0; exact local sources and notices under `lib/opendroneid`, with no registry wildcard dependency |

The source archive includes downloaded-library notices in `third_party/licenses`, [framework/runtime notices](../third_party/licenses/framework/README.md), and the local Recon/OpenDroneID sources and notices under `lib`. The same firmware release supplies a separate corresponding-source companion containing the complete installed Arduino core and direct library sources. See [RELINKING.md](RELINKING.md) for its inventory, verification and modified-library rebuild procedure. Preserve this companion, the application source and notices with firmware distribution. This beta's inventory is not a full transitive SPDX audit.

## Web installer

The browser installer uses [ESP Web Tools](https://github.com/esphome/esp-web-tools) **10.4.0**, under Apache-2.0. The web distribution retains its license and bundle provenance under `web/vendor/esp-web-tools-10.4.0/`. Its optional Wi-Fi provisioning features are not implemented by LORA Tunnel merely because the installer library supports them. UI previews in [assets/ui](../assets/ui/README.md) come from LORA Tunnel's drawing code and synthetic fixtures, with disclosed host-rendering approximations.

The installer bundle also contains independently licensed runtime dependencies and ESP flashing loader stubs. The legacy loader stubs are GPL-2.0-or-later; their matching source releases and original notices accompany the web/source distribution. They run as separate installation tooling and are not part of the LORA Tunnel application firmware. The project's MIT license does not relicense these stubs or the other browser dependencies. Keep the web vendor provenance, dependency notices and corresponding stub sources together when redistributing the installer.

WiGLE, WDGWars/Heimdall and uploader-related ESP32Marauder research revisions and their licenses are documented in [EXPORTS.md](EXPORTS.md). The Recon import and its inherited detector attribution are documented separately above.

Current branding uses the green radio-only LORA Tunnel asset and the separately supplied VTS header mark documented in [IMAGEGEN.md](../assets/IMAGEGEN.md). The VTS image is embedded unchanged with a light backing for contrast. BWM is not used by the current splash or source package; earlier branding/reference files remain local historical material. Artwork provenance is separate from software licensing, and upstream software references do not grant upstream logo or trademark rights. Imported software retains its stated licenses and notices alongside the root MIT license for original LORA Tunnel code.
