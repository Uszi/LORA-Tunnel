# Backup and recovery: Heltec Expansion Kit V2 / V4-R8 TFT

This operator procedure was first prepared before device access. Subsequent
connected-unit identification and read-only probes are recorded in
[VALIDATION.md](VALIDATION.md); consult that record for the latest backup and
hardware acceptance results. Back up the unit before the first custom-firmware
flash. A successful read or flash verification alone does not prove recovery.

## Identify the unit and preserve its original state

Confirm the actual PCB/kit revision, 915 MHz band, fitted antenna and the V4-R8
target. Record the current firmware name/version, settings and a photograph of
its normal screen. Use the V2 wiring in `docs/HARDWARE.md`; V1 firmware and
ordinary V4-R2 images are different targets.

This build selects **16 MB flash and 8 MB octal PSRAM**. In PlatformIO the memory
selection is `qio_opi`: QIO flash plus OPI PSRAM. `qio_opi` is a framework build
setting, not a valid esptool `--flash-mode` value. Flash ID can check flash size;
PSRAM requires a successful firmware memory report/test. Stop if the unit's
detected flash size or physical identity differs from the expected target.

Use a reliable data-capable USB-C cable and stable power. Close serial monitors
and other applications that hold the port. Copy the microSD contents separately:
a flash dump does not include the SD card. Full flash backups can contain device
identity, mesh configuration and stored credentials; keep them private and out
of source control.

## Enter the ROM bootloader and confirm the port

The [Heltec V2 flashing instructions](https://wiki.heltec.org/docs/devices/open-source-hardware/esp32-series/lora-32/wifi-lora-32-v4r8/meshtastic-qs)
specify: hold **USER/BOOT**, press and release **RST**, then release USER. The
bootloader port may differ from the running-firmware port. Check Windows Device
Manager or `pio device list` after entering boot mode, and identify the device
by disconnect/reconnect if necessary. Never reuse a remembered COM number or
let a flash command guess among devices.

The PowerShell examples use an explicit Python environment and pinned esptool 5.4.0. Activate your preferred virtual environment first, then install that version. This may be a separate recovery environment; it does not require changing PlatformIO's pinned packages. Replace the port and private-directory placeholders before continuing. Keep recovery files outside the checkout.

```powershell
python -m pip install esptool==5.4.0
$TunnelPython = (Get-Command python -ErrorAction Stop).Source
& $TunnelPython -c 'import importlib.metadata as m; assert m.version("esptool") == "5.4.0", "esptool 5.4.0 required"'
if ($LASTEXITCODE -ne 0) { throw 'Use the pinned esptool environment.' }
$TunnelPort = 'COM_REPLACE_ME'
if ($TunnelPort -notmatch '^COM[1-9][0-9]*$') { throw 'Set the confirmed bootloader COM port first.' }
$TunnelBackupDir = 'REPLACE_WITH_ABSOLUTE_PRIVATE_DIRECTORY'
if (-not [System.IO.Path]::IsPathRooted($TunnelBackupDir)) { throw 'Choose an absolute private backup directory outside the checkout.' }
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
& $TunnelPython -m esptool version
```

Original device operations used the project-local esptool 5.4.0 package. The standalone pinned package above exposes the same version-specific command interface without depending on a developer's installation path. Confirm the displayed version. UTF-8 settings prevent console-encoding failures. On Linux/macOS use your Python environment and confirmed serial device with the same esptool arguments; the surrounding variable/file checks above are PowerShell syntax. Older releases use underscore command names and may lack this fallback.

Firmware builds retain `core_dir = .pio-core` to isolate Arduino 3.3.12 / ESP-IDF 5.5.5 and compiler dependencies. Do not replace those packages to address a serial-loader problem.

## Read fallback verified on this connected unit

On the confirmed test unit during the 2026-11-01 preflight, esptool 5.4.0's default stub failed at
`Configuring flash size...` with `Serial data stream stopped`, at both 460800
and 115200 baud. Those failed attempts produced no backup file. After a fresh
ROM reset, a **4096-byte ROM-only read** and a **4096-byte legacy-stub read**
succeeded. These probes establish a working read path, not a complete backup;
see [VALIDATION.md](VALIDATION.md) for subsequent full-read results and hashes.

The installed 5.4.0 source and bundled files were inspected: global
`--stub-version 1` selects the legacy stub and `--stub-version 2` selects the
new/default stub. Both ESP32-S3 stub files are present. This is a **hidden
option, explicitly outside esptool's semantic-versioning guarantees**; it is
verified for this installation rather than a permanent general recommendation.
The fallback changes the temporary RAM loader and does not write flash or
eFuses. [esptool 5.4.0 option definition](https://github.com/espressif/esptool/blob/v5.4.0/esptool/__init__.py),
[stub loader selection](https://github.com/espressif/esptool/blob/v5.4.0/esptool/loader.py).

Re-enter the ROM bootloader after a failed stub attempt before selecting a
different stub. A running old stub can otherwise be detected and reused.
For a new read-only probe, use a fresh filename:

```powershell
$TunnelProbe = Join-Path $TunnelBackupDir 'probe-new-4KB.bin'
if (Test-Path -LiteralPath $TunnelProbe) { throw 'Choose a new probe filename.' }
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $TunnelProbe) | Out-Null
& $TunnelPython -m esptool --chip esp32s3 --port $TunnelPort --baud 115200 --before no-reset --after no-reset --stub-version 1 read-flash --flash-size 16MB 0x0 0x1000 $TunnelProbe
if ($LASTEXITCODE -ne 0 -or (Get-Item -LiteralPath $TunnelProbe).Length -ne 4096) {
    throw 'Legacy read probe failed; no full backup has been established.'
}
```

The documented alternative is the global `--no-stub` option, with
`read-flash --flash-size 16MB` after it. ROM reads use 64-byte transactions and
are much slower. The explicit size configures ROM flash bounds; it does not
alter stored image headers. Do not guess flash pins, change voltage, or alter
eFuses to work around this stub-specific failure. [Espressif ROM-only option](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/flasher-stub.html),
[read-flash size guidance](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html#read-flash-contents-read-flash).

## Read and verify a complete 16 MB backup first

Run these only after the port has been positively identified and the board is
in bootloader mode. They read device information/flash; they do not write flash.
`--after no-reset` keeps the application from booting between the reads and
changing its configuration in flash.

```powershell
& $TunnelPython -m esptool --chip esp32s3 --port $TunnelPort --before no-reset --after no-reset --no-stub flash-id
& $TunnelPython -m esptool --chip esp32s3 --port $TunnelPort --before no-reset --after no-reset --no-stub get-security-info
```

Check the results before continuing. Require detected **16 MB** flash. If secure
boot, flash encryption, or a restricted download mode is reported, do not use
the generic restore instructions or override protections; obtain a recovery
procedure for that secured unit. These steps do not change eFuses.

Select a new private backup directory and perform two full reads. The commands
below use the legacy-stub fallback verified above for this unit. Start the
first read from a fresh ROM bootloader state; keep the application stopped
between both reads so it cannot change stored configuration.

```powershell
New-Item -ItemType Directory -Force -Path $TunnelBackupDir | Out-Null
$TunnelBackupA = Join-Path $TunnelBackupDir 'full-read-a.bin'
$TunnelBackupB = Join-Path $TunnelBackupDir 'full-read-b.bin'
if ((Test-Path -LiteralPath $TunnelBackupA) -or (Test-Path -LiteralPath $TunnelBackupB)) {
    throw 'Choose new backup filenames; do not overwrite an existing recovery image.'
}
& $TunnelPython -m esptool --chip esp32s3 --port $TunnelPort --baud 460800 --before no-reset --after no-reset --stub-version 1 read-flash --flash-size 16MB 0x0 0x1000000 $TunnelBackupA
if ($LASTEXITCODE -ne 0) { throw 'First full-flash read failed.' }
& $TunnelPython -m esptool --chip esp32s3 --port $TunnelPort --baud 460800 --before no-reset --after no-reset --stub-version 1 read-flash --flash-size 16MB 0x0 0x1000000 $TunnelBackupB
if ($LASTEXITCODE -ne 0) { throw 'Second full-flash read failed.' }
if ((Get-Item -LiteralPath $TunnelBackupA).Length -ne 16777216 -or
    (Get-Item -LiteralPath $TunnelBackupB).Length -ne 16777216) {
    throw 'Backup has the wrong length.'
}
$TunnelHashA = (Get-FileHash -LiteralPath $TunnelBackupA -Algorithm SHA256).Hash
$TunnelHashB = (Get-FileHash -LiteralPath $TunnelBackupB -Algorithm SHA256).Hash
if ($TunnelHashA -ne $TunnelHashB) { throw 'Full-flash reads do not match.' }
Write-Output "Verified backup SHA256: $TunnelHashA"
```

If communication is unstable, recheck the cable/port and repeat with new backup
filenames at 115200 baud. A matching pair verifies repeatable acquisition; it
does not prove the stock application itself is healthy. Save the checksum,
detected chip/flash information and original version alongside the files. Keep
an additional copy away from the development workspace.

The explicit 0x1000000-byte length covers the full 16 MB chip, including
bootloader, partition table, application, NVS and filesystem partitions. An
application-only `firmware.bin` is not a full backup. Espressif documents the
address/size/file form of [read-flash](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html#read-flash-contents-read-flash).

## Scoped recovery for this unit's unstable read region

The 2026-11-01 investigation found two full 16 MB acquisitions differing by one
bit at `0x3F328B`; further small reads also showed a discrepancy at `0x3F332C`.
Keep every acquisition privately with its own hash. They are evidence of an
unresolved read discrepancy, **not a verified bit-identical full-chip backup**.
The original application beginning at `0x10000` has an internally valid image
SHA-256 in both full copies, with the inspected image ending at `0x371B50`
(exclusive). Its partition extends to `0x650000`. An image digest validates the
image bytes, not all padding, other partitions, the flash chip, or physical
recovery. Final acquisition hashes and results belong in [VALIDATION.md](VALIDATION.md)
and the private backup manifest.

A narrower recovery set can cover all flash regions the current prototype is
expected to change, without rewriting the unstable area:

| Saved range, end exclusive | Length | Reason |
| --- | --- | --- |
| `0x000000..0x372000` | `0x372000` / 3,612,672 bytes | Bootloader, partition table, NVS, OTA selection data, complete old app image, and the entire new factory write footprint |
| `0xFF0000..0x1000000` | `0x10000` / 65,536 bytes | Current framework writes crash dumps here if a panic occurs |

**The scoped backup acquisition and hashes are now verified; restoration is
still untested.** Three fresh prefix reads agreed while the application was
held stopped. The two fresh coredump reads agreed with each other and with the
corresponding tails of both earlier full acquisitions.

Verified region hashes are in the [public validation summary](VALIDATION.md). Concrete acquisition filenames and provenance remain in the private recovery record. These are results for one unit, not expected hashes for another board.

The fresh prefix differs from the earlier full acquisitions by 83 bytes, all
inside the NVS sector beginning at `0x9000`, after the user's power cycle. There
are no prefix differences outside NVS. In particular, the entire original app
at `0x10000..0x371B50` is unchanged from both full acquisitions and retains its
valid internal SHA-256. The repeatable fresh prefix is the recovery snapshot
for the current pre-custom-firmware state; do not mix its hash with the older
NVS contents or claim that the whole old and fresh prefixes are identical.

The private manifest records acquisition provenance. Binaries and device-specific filenames remain private and outside source control. Retain the immutable prefix/tail files, lengths, independently recorded hashes and both full acquisitions. Matching saved regions do not resolve the mismatch near `0x3F3300`.

The need for the second region comes from inspected installed sources:

- `framework-arduinoespressif32/tools/partitions/default_16MB.csv` puts NVS at
  `0x9000..0xE000`, OTA data at `0xE000..0x10000`, app0 at
  `0x10000..0x650000`, app1 at `0x650000..0xC90000`, SPIFFS at
  `0xC90000..0xFF0000`, and coredump at `0xFF0000..0x1000000`.
- `framework-arduinoespressif32-libs/esp32s3/qio_opi/include/sdkconfig.h:990`
  enables `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH`; a panic may therefore change
  the last 64 KiB after boot even when the flashing command never writes there.
  [Espressif core-dump storage](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/core_dump.html#core-dump-to-flash)
- Arduino `cores/esp32/esp32-hal-misc.c:319` checks pending OTA state and can
  update OTA metadata. Lines 336 onward initialize NVS and can erase its
  partition when recovery is necessary. NimBLE-Arduino 2.5.2
  `src/NimBLEDevice.cpp:904` likewise initializes NVS and can erase/reinitialize
  that default NVS partition. These regions are inside the saved prefix.
  [Espressif NVS initialization](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/storage/nvs_flash.html#initialization)

The current application writes session logs and configuration to microSD. It
has no mounted SPIFFS/LittleFS, OTA updater, application-partition write, or
eFuse-write operation. Wi-Fi persistence is disabled and BLE uses passive
advertisement scanning. Source inspection identified no additional intended
runtime flash writes outside the prefix and coredump range. That is a bounded
source-review conclusion, not a guarantee about a faulty chip, unexpected
runtime corruption, or future firmware. Reassess the recovery ranges when
adding an internal filesystem, OTA, another NVS partition, or any flash writer.

For the proposed factory image at address zero, calculate its actual sector
footprint from the final binary length; do not rely on an approximate size in
chat. The published 0.2.2-beta.1 factory image ends at `0x267000` after 4 KiB
rounding; calculate again for every later image. Require that rounded end to be at or below `0x372000` before using
this plan. Standard `write-flash` erases only sectors touched by its input
files, unless `--erase-all` is requested. The installed esptool 5.4.0
`esptool/cmds.py:1280` defaults `erase_all` to false, lines 1539-1540 call a
full erase only when it is true, and lines 1730 onward calculate/report the
sector-rounded write range. `_update_image_flash_params()` at lines 740-743
leaves bytes unchanged when mode, size and frequency are all `keep`.
[Espressif write/erase boundaries](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html#erasing-flash-before-write)

The inspected upstream legacy-stub implementation agrees with that boundary:
`handle_flash_begin()` computes the required 4 KiB sectors, block erases are
used only where an entire aligned 64 KiB block remains inside that range, and
`handle_flash_data()` clamps a padded final transport block to the remaining
requested byte count. Transport padding therefore does not extend this write
to another whole 16 KiB block. [Legacy stub write implementation](https://github.com/espressif/esptool-legacy-flasher-stub/blob/master/flasher_stub/stub_write_flash.c)

Do not use `erase-flash`, `--erase-all`, whole-flash padding, filesystem upload,
`--force`, or differential/trust-flash shortcuts with this scoped plan. Keep
secure boot and flash encryption checks from the identification procedure;
this generic plaintext recovery requires both disabled. Record the actual
flasher-reported erase extent and verify the newly written image before its
first boot. The untouched unstable area must not be included in a speculative
repair write.

To restore the scoped backup later, enter ROM mode, identify the current port,
and use the pinned esptool 5.4.0 setup above. Set saved-region paths and independently recorded hashes from that same unit's private manifest. Placeholders below intentionally fail until replaced; these ranges are specific to the reviewed prototype, not a general recipe for other firmware:

```powershell
$TunnelPrefix = 'REPLACE_WITH_VERIFIED_PREFIX_FILE'
$TunnelTail = 'REPLACE_WITH_VERIFIED_TAIL_FILE'
$TunnelPrefixHash = 'REPLACE_WITH_INDEPENDENTLY_RECORDED_SHA256'
$TunnelTailHash = 'REPLACE_WITH_INDEPENDENTLY_RECORDED_SHA256'
if ($TunnelPrefixHash -notmatch '^[0-9A-Fa-f]{64}$' -or
    $TunnelTailHash -notmatch '^[0-9A-Fa-f]{64}$') { throw 'Load recorded recovery hashes first.' }
if ((Get-Item -LiteralPath $TunnelPrefix).Length -ne 0x372000 -or
    (Get-Item -LiteralPath $TunnelTail).Length -ne 0x10000) { throw 'Recovery region length mismatch.' }
if ((Get-FileHash -LiteralPath $TunnelPrefix -Algorithm SHA256).Hash -ne $TunnelPrefixHash -or
    (Get-FileHash -LiteralPath $TunnelTail -Algorithm SHA256).Hash -ne $TunnelTailHash) {
    throw 'Recovery file hash mismatch.'
}
if ($TunnelPort -notmatch '^COM[1-9][0-9]*$') { throw 'Reconfirm the bootloader port.' }
& $TunnelPython -m esptool --chip esp32s3 --port $TunnelPort --baud 115200 --before no-reset --after no-reset --stub-version 1 write-flash --flash-mode keep --flash-size keep --flash-freq keep 0x0 $TunnelPrefix 0xFF0000 $TunnelTail
if ($LASTEXITCODE -ne 0) { throw 'Scoped restore failed; remain in bootloader mode.' }
& $TunnelPython -m esptool --chip esp32s3 --port $TunnelPort --baud 115200 --before no-reset --after no-reset --stub-version 1 verify-flash 0x0 $TunnelPrefix 0xFF0000 $TunnelTail
if ($LASTEXITCODE -ne 0) { throw 'Scoped restore verification failed.' }
```

The two address/file pairs do not fill or erase the gap between them. Read back
both restored regions and compare their SHA-256 hashes to the manifest before
booting. This restores the covered original bytes; it does not establish that
the complete 16 MB is bit-identical, or resolve the pre-existing discrepancy
near `0x3F3300`. Finally verify normal original-firmware boot, display/touch,
GNSS, SD, configured radio behavior, and standalone cold boot. Until that test
is performed, describe the state as **backup acquisition verified; restore
untested**. A future readback match proves restored bytes only, until the
original firmware's physical behavior is also checked.

## Legacy-stub installation fallback

PlatformIO's normal upload command and browser flashing may use a different loader. If the default esptool 5.4.0 stub stops at flash-size configuration on this target, use the inspected legacy stub rather than repeating an unmodified failing upload. That path passed the recorded application writes and independent verification. Browser flashing through ESP Web Tools has not been physically verified by those command-line results.

**Leave the browser's “Erase device” checkbox unchecked when relying on scoped recovery.** The prompt is enabled so an erase can be declined. A full-chip erase changes regions outside the saved prefix/tail and requires a separately verified full-chip backup/recovery plan. The test unit's full-chip comparison failed, so its scoped plan does not permit that erase.

After preserving recovery data and confirming the target/port, enter a fresh ROM bootloader state and run the pinned environment setup above. From the project directory, the following installs the **merged factory image**, not the application-only binary. For a downloaded release, select its matching merged image and compare its SHA-256 with the published release record first.

```powershell
$TunnelFactory = Join-Path (Get-Location) '.pio\build\heltec-v4-r8-tft\firmware.factory.bin'
if (-not (Test-Path -LiteralPath $TunnelFactory)) { throw 'Build or select the matching merged factory image first.' }
$TunnelLength = (Get-Item -LiteralPath $TunnelFactory).Length
$TunnelRoundedEnd = [math]::Ceiling($TunnelLength / 4096.0) * 4096
if ($TunnelLength -le 0 -or $TunnelRoundedEnd -gt 0x372000) { throw 'Reassess this scoped recovery plan for the image size.' }
Get-FileHash -LiteralPath $TunnelFactory -Algorithm SHA256
& $TunnelPython -m esptool --chip esp32s3 --port $TunnelPort --baud 115200 --before no-reset --after no-reset --stub-version 1 write-flash --flash-mode keep --flash-size keep --flash-freq keep 0x0 $TunnelFactory
if ($LASTEXITCODE -ne 0) { throw 'Write failed; retain bootloader mode and inspect the result.' }
& $TunnelPython -m esptool --chip esp32s3 --port $TunnelPort --baud 115200 --before no-reset --after no-reset --stub-version 1 verify-flash 0x0 $TunnelFactory
if ($LASTEXITCODE -ne 0) { throw 'Independent verification failed; do not claim a successful installation.' }
```

After verification, press RST and check startup. The `keep` flags preserve the verified image's header parameters. No `erase-flash`, `--erase-all` or eFuse operation is part of this fallback. These commands document the supported procedure; the release record distinguishes factory-image inspection from the application-only update actually used on the test unit.

## Restore this same unit from its own full backup

**Connected-unit exception:** the full reads discussed in the scoped procedure
above are not bit-identical. Do not use either as a verified full-chip restore
image. This full-chip procedure applies only when a complete matching pair has
actually been established for the unit.

This replaces the unit's current flash contents. Preserve any newer SD logs and
configuration first. Use only the verified full backup for this same device,
not a dump from another unit. Re-enter bootloader mode and reconfirm the port.
Do not proceed with an incomplete dump or a failed integrity check.

```powershell
$TunnelRestore = $TunnelBackupA
if ((Get-Item -LiteralPath $TunnelRestore).Length -ne 16777216) { throw 'Not a full 16 MB dump.' }
if ((Get-FileHash -LiteralPath $TunnelRestore -Algorithm SHA256).Hash -ne $TunnelHashA) {
    throw 'Backup checksum differs from the recorded value.'
}
& $TunnelPython -m esptool --chip esp32s3 --port $TunnelPort --baud 460800 --before no-reset --after no-reset --stub-version 1 write-flash --flash-mode keep --flash-size keep --flash-freq keep 0x0 $TunnelRestore
if ($LASTEXITCODE -ne 0) { throw 'Restore failed; retain bootloader mode and inspect the error.' }
& $TunnelPython -m esptool --chip esp32s3 --port $TunnelPort --before no-reset --after no-reset --stub-version 1 verify-flash 0x0 $TunnelRestore
if ($LASTEXITCODE -ne 0) { throw 'Restored bytes did not verify.' }
```

If running this in a later shell, explicitly set `$TunnelBackupA`,
`$TunnelHashA` from the saved record, `$TunnelPython`, and the newly confirmed
`$TunnelPort` first. Never compute the expected hash from an untrusted restore
file and treat that as an independent check.

`keep` preserves the original bootloader image settings. No separate
`erase-flash` is needed for a complete full-chip write: esptool erases sectors
covered by the write. Do not use `--force`, encryption overrides, or
`--trust-flash-content` to bypass a failed restore. After verification, press
RST and verify normal stock startup, screen, input, GNSS, SD and configured
radio behavior. Recheck cold boot without USB. Verified bytes alone do not
establish successful standalone recovery.

## Manufacturer firmware route when a full backup is unavailable

Start from the [Heltec Expansion Kit V2 product page](https://heltec.org/project/v4-r8-ex/)
and its [V2 Meshtastic firmware directory](https://resource.heltec.cn/download/WiFi_LoRa_32_Expansion_Kit-V2/firmware/Meshtastic).
The manufacturer names the target **`firmware-heltec-v4-r8-tft-….factory.bin`**.
The directory inspected on 2026-11-01 listed
`firmware-heltec-v4-r8-tft-2.7.26.54e0d8d.factory.bin`.
This verifies a manufacturer download route, not the exact firmware shipped
on this particular device. The file was not flashed or validated on hardware.

Download a matching V4-R8 TFT **factory** image from that route, record its
source/version/hash, and follow Heltec's local-file flashing procedure. Its
current documentation specifies V2 support from Meshtastic 2.7.25 and local
firmware selection. Do not substitute the V1 target, an OLED/V4-R2 build, or a
plain application/update image. The upstream
[Meshtastic installer](https://github.com/meshtastic/firmware/blob/6d41e279f1f51bd59f687b9d441c1bf47b1594fc/bin/device-install.sh)
places its factory image at address 0x0 and may also require release-bundle
companion files. Follow the matched release's installation manifest/script
instead of inventing offsets for separate files.

An offline `esptool image-info PATH_TO_IMAGE` can inspect the ESP32 image header;
it cannot establish the correct peripheral pin map or confirm the image is the
same as the one originally shipped. Restoring a manufacturer release without
your full dump may lose prior identities, settings, keys, and data. Recover the
original configuration only from a retained backup/export.

## If the screen is blank or USB disappears

First distinguish application failure from bootloader connectivity. Re-enter
ROM bootloader using USER plus RST, re-enumerate the port, and check flash/chip
information before any write. A blank LCD alone is not evidence that the MCU
cannot be recovered. An apparently successful upload does not prove the TFT,
touch, GNSS, or SD driver is compatible. Preserve the failing binary and boot
log for diagnosis, then restore the verified backup if needed. Do not erase
first merely because the screen is blank.
