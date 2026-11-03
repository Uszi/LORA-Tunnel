# Cardputer 0.4.0-beta.1 release preparation

**Status: local preparation; physical acceptance and publication pending.**
Do not push, deploy, upload assets, submit M5Burner, or claim hardware acceptance
until the actual Cardputer ADV + U214 acceptance run passes. Source, build,
package and browser previews are separate evidence from physical checks.

## Artifacts and source provenance

Build `cardputer-adv-lora1262`, run host checks and package its clean outputs:

```sh
python tools/package_cardputer.py --build .pio/build/cardputer-adv-lora1262 --output dist/cardputer-0.4.0-beta.1 --version 0.4.0-beta.1
python tools/package_source.py dist/LORA-Tunnel-0.4.0-beta.1-source.zip
python tools/package_relink.py package 0.4.0-beta.1 dist/LORA-Tunnel-0.4.0-beta.1-relink.zip --env cardputer-adv-lora1262 --build .pio/build/cardputer-adv-lora1262
```

Create the source archive from the final local commit. The site builder verifies
every packaged source file against its commit, so repackage after any final
source change. The dependency companion retains M5Cardputer, M5Unified, M5GFX,
RadioLib, TinyGPSPlus, NimBLE, ArduinoJson and the transitive IRremote package
sources/notices with hashes. Nested Adafruit keyboard and M5GFX font notices
remain in the packaged library trees.

Publish these exact named Cardputer binaries after acceptance:

- `LORA-Tunnel-0.4.0-beta.1-Cardputer-ADV-LoRa1262-FACTORY-0x0.bin`
- `LORA-Tunnel-0.4.0-beta.1-Cardputer-ADV-LoRa1262-LAUNCHER-APP.bin`

Include matching source, dependency companion, SD setup and SHA256SUMS.
Keep the original Heltec 0.3.0-beta.1 assets immutable. The Heltec build from
current source is a regression check; it is not a newly published Heltec image.

## Stage the two-target installer

Create a **local**, untracked input catalog. Paths resolve relative to that
catalog file. Use actual file hashes and commit IDs, not the placeholders below.
The `site` source is the new 0.4 source; each firmware entry independently binds
its own version, source archive and commit. The old Heltec guides/images are
rendered from the preserved 0.3 source under its target directory.

```json
{
  "schema": 2,
  "github": "https://github.com/valleytechsolutions/LORA-Tunnel",
  "gitea": "https://git.churchofmalware.org/Valleytech/LORA-Tunnel",
  "site": {
    "version": "0.4.0-beta.1", "source": "LORA-Tunnel-0.4.0-beta.1-source.zip",
    "source_sha256": "REPLACE", "source_commit": "REPLACE"
  },
  "targets": [
    {
      "target": "heltec-v4-r8-tft-high-band", "version": "0.3.0-beta.1",
      "factory": "LORA-Tunnel-0.3.0-beta.1-Heltec-V4-R8-TFT-HIGH-BAND-FACTORY-0x0.bin",
      "factory_sha256": "e6905bfe5521e2f9887a9628dcaf55870b8b44cba2c92c1f8e3c671ee1533a77",
      "source": "LORA-Tunnel-0.3.0-beta.1-source.zip",
      "source_sha256": "519d91704d2572c8b705912bb17105e1dc348e9e3a7db3141ef2f876e558dd85",
      "source_commit": "50da5b915fb75d5e12b2bfaae6f7479a4fafec07",
      "relink": "LORA-Tunnel-0.3.0-beta.1-relink.zip",
      "relink_sha256": "8d2d82cd3b9c76d54885889f56d1f3773d62ffd6555c1541b120c0502899326f",
      "sd": "LORA-Tunnel-0.3.0-beta.1-SD-setup.zip",
      "public_enabled": true
    },
    {
      "target": "cardputer-adv-lora1262", "version": "0.4.0-beta.1",
      "factory": "cardputer-0.4.0-beta.1/LORA-Tunnel-0.4.0-beta.1-Cardputer-ADV-LoRa1262-FACTORY-0x0.bin",
      "factory_sha256": "REPLACE",
      "app": "cardputer-0.4.0-beta.1/LORA-Tunnel-0.4.0-beta.1-Cardputer-ADV-LoRa1262-LAUNCHER-APP.bin",
      "app_sha256": "REPLACE", "source": "LORA-Tunnel-0.4.0-beta.1-source.zip",
      "source_sha256": "REPLACE", "source_commit": "REPLACE",
      "relink": "LORA-Tunnel-0.4.0-beta.1-relink.zip", "relink_sha256": "REPLACE",
      "public_enabled": false
    }
  ]
}
```

```sh
python tools/build_web.py --catalog dist/catalog-input.json --output build-output/web-cardputer-local --local-preview
python -m http.server 8765 --bind 127.0.0.1 --directory build-output/web-cardputer-local
```

Open `http://127.0.0.1:8765/?acceptance=1`. Pending Cardputer installation is
enabled only when the build is explicitly a local preview, the hostname is
loopback and the acceptance query is present. It still requires hardware
selection/confirmation. Do not deploy the preview directory. `PUBLICATION.json`
records that it is not publishable.

A production build omits `--local-preview`. It rejects a pending Cardputer
entry. Set `public_enabled: true` and supply `acceptance` pointing to the actual
hardware record only after acceptance. The record requires target, `result`
`passed`, exact factory/app hashes, `tested_at`, `launcher_version`, and every
following boolean under `checks`:

- `factory_boot`: clean merged installation and cold boot, no serial monitor required.
- `keyboard_display`: colors, readable landscape pages, every navigation/text control.
- `lora_receive`: region selection and receive evidence with U214; missing cap handled.
- `gps_fix`: fresh valid fix and UTC through U214 GNSS.
- `sd_readback`: saved sessions independently read, valid records/geotags and clean stop.
- `cold_start_region`: region/Skip LoRa persists after complete restart.
- `power_wake`: Cancel, stop/save, power/sleep and documented wake behavior.
- `sustained_capture`: at least 30 minutes, responsive UI, heap/queue/drop counters observed.
- `web_flash`: exact merged binary installed through the local browser installer.
- `launcher_install`: exact application binary installed through Launcher's SD menu.
- `launcher_return`: stop/save, power cycle, Enter at Launcher startup, relaunch and auto-start.
- `upload_idle_resume`: configured home Wi-Fi only while stopped/saved; starting capture interrupts upload work and promptly resumes scanning, with safe retry afterward.
- `wigle_upload`: explicitly enable WiGLE with the user's API name/token and confirm a consenting test session's actual service receipt.
- `wardrift_upload`: explicitly enable Wardrift with the user's token and confirm its actual accepted/duplicate receipt.
- `wdgwars_upload`: explicitly enable WDGWars with the user's token and confirm completion of its asynchronous upload job, not merely HTTP 202.

At the physical milestone the user supplies home Wi-Fi credentials and enables
each service they want to test. Keep tokens, coordinates and captures private.
Record live service results separately from software fixtures. An unavailable
credential or untested service remains pending; neither a host test nor a saved
setting is a successful live upload. The release gate remains pending until
the listed live checks are recorded against these exact binaries.

Retain test details and logs privately; publish only the sanitized validation
summary. A file containing booleans is a record of testing, not a substitute
for carrying out those tests. If a fix changes a binary, retest that binary and
update its hashes before publication.

## Publication after acceptance

1. Publish source and a new GitHub prerelease with checked assets. Verify remote
   source commit, asset size and SHA-256 by downloading the public files.
2. Update the existing Gitea **packaged distribution**, preserving the Heltec
   download and identifying the full source ZIP accurately. Do not turn it into
   a Git-history mirror. Read back every uploaded file and checksum.
3. Build the production installer from accepted artifacts, inspect both target
   selectors and 1440/390/320 px layouts, then deploy the checked directory.
   Confirm downloaded metadata, binaries, guides/images and legacy Heltec URLs.
4. Submit the Cardputer factory BIN to M5Burner. Record submission, review and
   public catalog availability separately. Submission success does not establish
   catalog approval. Check the public download's hash when approved.

## M5Burner submission draft

**Name:** LORA Tunnel — Cardputer ADV + Cap LoRa-1262

**Version:** 0.4.0-beta.1 · **Device:** Cardputer ADV only · **Flash offset:** 0x0

**Description:** LORA Tunnel for M5Stack Cardputer ADV with Cap LoRa-1262 (U214).
Use the physical keyboard to operate standalone Wi-Fi, BLE and LoRa observation
sessions, view passive Recon evidence, and save versioned logs to microSD.
U214 provides LoRa and GNSS. Choose a compatible region on first start or skip
LoRa. Requires the ADV keyboard model and U214; original Cardputer and other
radio caps are not this target. Back up your firmware and SD files before
installation. Factory installation replaces Launcher; a separate Launcher
application is available in the GitHub release. See the linked guide for
controls, setup, current feature limits and exact hardware-test evidence.

**Project:** https://github.com/valleytechsolutions/LORA-Tunnel

**Cover candidate:** `assets/ui/cardputer/m5burner-cover.png`, generated by
`python tools/make_cardputer_cover.py` (Pillow required), combines the existing
green LORA Tunnel radio logo and a labeled
240 × 135 source-rendered Cardputer UI preview. Label simulated previews as
such; do not present generated or host-rendered artwork as a device photograph.
Keep required component credits in the source/docs and submission source link.
