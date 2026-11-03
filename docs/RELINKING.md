# Library sources and rebuilding modified firmware

For Cardputer ADV 0.4.0-beta.1, use its separately named matching source and
relink archives and substitute `cardputer-adv-lora1262` for the Heltec environment
in the commands/paths below. Its companion contains M5Cardputer at commit
`2d4fa6646e4e5b47e0af96214b003aa7b15b8d81`, M5Unified 0.2.29, M5GFX 0.2.36,
IRremote 4.5.0 and the common ArduinoJson/NimBLE/RadioLib/TinyGPSPlus libraries.
Complete nested Adafruit keyboard and font sources/notices remain included.
The preserved Heltec 0.3.0-beta.1 companion and binary are unchanged.

Cardputer also includes project build scripts and the pinned ESP-IDF 5.5.5
non-ISR placement policy under `build-inputs/`. The full application source
archive contains the same `tools/cardputer_*.py` build hooks and `linker/cardputer/`
policy directory. A normal complete Cardputer build regenerates
`.pio/build/cardputer-adv-lora1262/cardputer_linker/sections.ld`, checks the exact
SDK/archive/configuration hashes before linking, and verifies the resulting
ELF/map placement afterward. The installed SDK files remain unchanged.

Its matching companion retains the exact generated `sections.ld`, `report.json`,
and `verification.json` under `build-metadata/cardputer_linker/`. These records
use portable SDK/repository-relative names, hashes and archive/object/section
identities. The report binds the source policy and build scripts; verification
binds the generated script, report, ELF and map. Before creating the companion,
the packaging tool regenerates the expected linker output in a temporary
directory, compares it with the build evidence, and repeats the placement checks
against that build's ELF/map. Missing, changed or mismatched evidence fails
packaging. Do not copy these reports from a different build. The Heltec collector
and historical companion continue to use their original SDK placement.

LORA Tunnel's original application is MIT licensed. Its firmware also links
Arduino-ESP32 and TinyGPSPlus under LGPL-2.1-or-later, alongside components
under their own MIT, BSD, Apache and other licenses. The application's MIT
license does not replace those licenses. You may modify the LGPL libraries,
rebuild/relink the application for your own use, and reverse engineer the
firmware to debug those modifications. No project term restricts those rights.

Download both the application source archive and
`LORA-Tunnel-0.3.0-beta.1-relink.zip` from the same release as the firmware.
The companion provides the complete installed Arduino-ESP32 3.3.12 source
package and sources for TinyGPSPlus 1.1.0, ArduinoJson 6.21.5, LovyanGFX 1.2.24,
NimBLE-Arduino 2.5.2 and RadioLib 7.8.1, with their original notices. The
application archive supplies the remaining source, board/build configuration,
local Recon/OpenDroneID libraries and embedded artwork. There is no dependency
on a private application object file or private signing key.

The companion excludes host `.exe` utilities, which are not linked into the
firmware; corresponding Python build utilities remain. Compiler, uploader,
platform and precompiled ESP-IDF SDK packages are downloaded at the pinned
versions in `platformio.ini` and the included platform/package metadata.
The SDK's component lock and configuration are included for provenance. This
companion is a library source/relink package, not a mirror of every build tool
or the whole ESP-IDF source repository.

## Rebuild and relink

Use Python and PlatformIO Core on your host. From the extracted application
source directory, install the pinned dependencies without connecting a device:

```text
python -m platformio pkg install -e heltec-v4-r8-tft
```

Extract the companion next to the application checkout. Its `MANIFEST.json`
records SHA-256 hashes of every included file, the matching released firmware
images, and all application compilation inputs. Check the downloaded archive
against the published release hash and verify its internal inventory:

```text
python tools/package_relink.py verify ../LORA-Tunnel-0.3.0-beta.1-relink.zip
```

The local library sources used by the normal build are:

| Component | Editable build source | Matching companion source |
| --- | --- | --- |
| Arduino core and built-in libraries | `.pio-core/packages/framework-arduinoespressif32/` | `framework/` |
| TinyGPSPlus | `.pio/libdeps/heltec-v4-r8-tft/TinyGPSPlus/` | `libraries/TinyGPSPlus/` |
| Other direct libraries | `.pio/libdeps/heltec-v4-r8-tft/<library>/` | `libraries/<library>/` |

Compare these files with the companion's hashes before editing. The companion
source can be copied over the corresponding installed source directories to
restore the supplied source; retain PlatformIO's local `.piopm` metadata and
host tools. Make the desired library changes in those editable build paths.
Do not run a package update/reinstallation after editing, because that can
replace local changes. Clean the old compiled application and library objects,
then compile and link the modified sources:

```text
python -m platformio run -e heltec-v4-r8-tft -t clean
python -m platformio run -e heltec-v4-r8-tft
```

The new `.pio/build/heltec-v4-r8-tft/firmware.elf` and `firmware.bin` contain the
rebuilt library and application. All required application source is available,
so relinking is accomplished through a complete rebuild rather than a binary
patch. Preserve local modifications separately before changing build packages.
Use the normal hardware verification, backup and flash procedure in
[RECOVERY.md](RECOVERY.md) only when you intend to install your modified build;
none of the commands above opens a device or uploads firmware.

The pinned source/configuration supports rebuilding and modification. Exact
byte-for-byte output across hosts is not promised: compiler metadata, build
paths and timestamps can affect the ELF/image. Published hashes identify the
release artifacts, not every independently rebuilt executable.

## Source and notice provenance

Arduino source: [Espressif 3.3.12](https://github.com/espressif/arduino-esp32/tree/3.3.12).
SDK: ESP-IDF 5.5.5 commit
`b774170ff46c393eeb5e495ea37936038d3f4f4f`; its separately licensed radio
components remain under their own redistribution terms. The exact platform
metadata names the upstream download URLs and toolchain versions.

[Framework/runtime notices](../third_party/licenses/framework/README.md)
supplement the [direct-library notices](../third_party/licenses/) and
[third-party inventory](THIRD_PARTY.md). Their source manifest records
immutable revisions and retrieved-file hashes. LGPL-2.1 sections 4 and 6
describe source and relinking distribution terms; the retained license text
is in `third_party/licenses/LGPL-2.1.txt`.

Release maintainers generate the companion from the isolated packages that
built the preserved release images:

```text
python tools/package_relink.py package 0.3.0-beta.1 dist/LORA-Tunnel-0.3.0-beta.1-relink.zip
```

The tool reads explicit library/framework roots and versioned build artifacts;
it never reads a device, SD configuration, capture log or private backup. It
refuses to overwrite a companion and verifies every archived file afterward.
Publish this companion, its hash and the matching application source beside
every downloadable firmware copy, including the web flasher release page.
An upstream URL by itself is not the corresponding-source release asset.
