# Cardputer release packaging

The current release is **0.5.0** for **Cardputer ADV with Cap LoRa-1262 (U214)**. The [release notes](RELEASE_0_5_0.md) describe its features and known limitations; [CARDPUTER_ADV.md](CARDPUTER_ADV.md) covers installation and controls. Earlier 0.4.0-beta.1 measurements remain in [the historical validation record](CARDPUTER_SOFTWARE_VALIDATION.md).

## Build and package

```sh
python -m platformio run -e cardputer-adv-lora1262
python tools/package_cardputer.py --build .pio/build/cardputer-adv-lora1262 --check-only
python tools/package_cardputer.py --build .pio/build/cardputer-adv-lora1262 --output dist/cardputer-0.5.0 --version 0.5.0
python tools/package_source.py dist/LORA-Tunnel-0.5.0-source.zip
python tools/package_relink.py package 0.5.0 dist/LORA-Tunnel-0.5.0-Cardputer-ADV-LoRa1262-relink.zip --env cardputer-adv-lora1262
```

Package the final committed source. The site builder checks every source-archive file against its commit and binds the dependency companion to the exact factory image. Cardputer packaging verifies the ESP32-S3 image checksums/digests, 8 MB partition layout, blank factory NVS, target identity and application size. The Launcher app must equal the factory image's application at `0x10000`.

| File | Installation route |
| --- | --- |
| `LORA-Tunnel-0.5.0-Cardputer-ADV-LoRa1262-FACTORY-0x0.bin` | USB, web installer or M5Burner at address zero; replaces Launcher |
| `LORA-Tunnel-0.5.0-Cardputer-ADV-LoRa1262-LAUNCHER-APP.bin` | Bmorcelli Launcher SD menu; Launcher chooses the destination partition |

Provide both files, the source archive, board-specific dependency/relinking archive, empty-credential SD starter and SHA-256 checksum list. Do not publish flash backups or private SD configuration. Older release artifacts remain immutable.

## Publication record

Full hardware acceptance is separate from release authorization with disclosed limitations. A current stable release can use `result: scoped-release` with its exact `version`, target, factory/application hashes, explicit `publication_authorized`, authorization date, passed software checks, verified USB application installation, and a nonempty `unverified_checks` list. The builder rejects an old `scoped-beta` record for a stable version. Pending checks must not be marked as passed.

The complete acceptance checklist includes display/keyboard, known-source RF, GPS-valid saved observations, power/wake, sustained capture, browser factory flashing, Launcher installation/return and live service receipts. A stable version label does not substitute for these results.

## Two-target web installer

Create a private schema-2 catalog containing the Heltec and Cardputer entries. Each entry includes its version, full source commit, paths and SHA-256 hashes for factory/source/relink artifacts, and a publication record. Cardputer also supplies the application BIN/hash. Each board has its own factory manifest, hardware requirements and guide. The site source may be shared when both targets use the same source release.

```sh
python tools/build_web.py --catalog dist/catalog-0.5.0.json --output build-output/web-0.5.0
```

`--local-preview` permits local review of unpublished candidates; that output must not be deployed. A normal build requires publication evidence for each enabled target. Validate links, hardware switching, confirmation reset and download hashes before deploying. ESP32-S3 chip detection cannot identify which board or cap is attached.

## M5Burner

Update the existing **LORA Tunnel — Cardputer ADV + Cap LoRa-1262** listing at [burner.m5stack.com](https://burner.m5stack.com/). Select Cardputer ADV and upload the factory image. Link the GitHub release and mention the U214 requirement, microSD setup, region selection, combined mesh mode and separate Launcher download. Retain the project logo/cover and professional description.

Submission and public catalog approval are different states. Record the version and actual dashboard result; do not claim public availability while review is pending. Do not upload the Heltec factory image or the Launcher application as the M5Burner standalone firmware.

## Verification after publishing

Check the GitHub release, production installer catalog/manifests and every public download against local SHA-256 hashes. Update Gitea by ordinary manual upload or an authorized Git push; do not use repository migration. Verify M5Burner submission state separately. Keep device-specific captures, credentials and recovery images outside public artifacts.
