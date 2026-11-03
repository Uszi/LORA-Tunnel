# Cardputer ADV + Cap LoRa-1262

LORA Tunnel **0.4.0-beta.1** adds a dedicated `cardputer-adv-lora1262` build for
**M5Stack Cardputer ADV with Cap LoRa-1262 (U214)**. This is local test preparation:
physical acceptance and publication are pending. The existing Heltec
0.3.0-beta.1 release remains available unchanged.

The target uses ESP32-S3, **8 MB flash, no PSRAM**, the 240 × 135 landscape
display, ADV keyboard, microSD, speaker and battery support. U214 supplies
SX1262 LoRa and GNSS. Its supported hardware range is **868–923 MHz**; the
regional selector must respect that range. Original Cardputer and other LoRa
caps are separate hardware and are not certified by this build.

Official references: [Cardputer ADV](https://docs.m5stack.com/en/core/Cardputer-Adv),
[Cap LoRa-1262](https://docs.m5stack.com/en/cap/Cap_LoRa-1262).

## Start a session

1. Fit U214 and the appropriate antenna with power off. Use a FAT32 microSD card.
2. Copy the supplied `lora-tunnel` folder to the card root, then copy
   `config.example.json` to `config.json` **on the card**. Keep credentials and
   captured data out of source archives.
3. Start LORA Tunnel and choose a supported LoRa region, or choose Skip LoRa.
   Fresh configuration leaves LoRa UNSET/off until this step.
4. Check SD and GNSS status, open Wardriver and start capture. Missing GPS is
   reported explicitly; it is not replaced with a fabricated location.
5. Stop and save before removing the card, resetting, or switching firmware.
   Inspect the saved `/sessions` files after a short first capture.

The physical keyboard replaces the Heltec touchscreen:

| Screen | Controls |
| --- | --- |
| Navigation | Fn arrows or bare `;` up, `.` down, `,` left, `/` right; Enter selects; Esc/backtick goes back |
| Home | W Wardriver, R Recon, S Settings |
| Wardriver / Recon | Space starts/stops; S Settings; Esc Home (capture continues) |
| Wardriver | Left/right changes radio tab |
| Recon | Left/right category; up/down match; Enter evidence; M sound |
| Credentials | Type using Shift/Fn as needed; Backspace deletes; Enter saves; Esc cancels; secrets are masked |
| Region | Up/down chooses; Enter saves; Esc goes back, or skips LoRa during first setup |
| Power | Defaults to Cancel; select Power Off to save and sleep; G0 wakes (physical acceptance pending) |

Settings includes region, credentials, uploads, alert sound, retry SD and power.
For service configuration and the idle-only upload rules see [Uploads](UPLOADS.md).
No touchscreen is required.

## Choose the correct installation file

| File suffix | Use |
| --- | --- |
| `Cardputer-ADV-LoRa1262-FACTORY-0x0.bin` | Web flasher, M5Burner or esptool, written at **0x0**; replaces installed firmware/Launcher |
| `Cardputer-ADV-LoRa1262-LAUNCHER-APP.bin` | Install through Bmorcelli Launcher's **SD** menu; application only |

Both files contain the same application. The factory file adds its matching
bootloader, partition table and OTA boot data. **Never write the Launcher
application at 0x0.** No additional flash filesystem image is required.

Before USB installation, identify the actual device and port, back up existing
firmware/settings and microSD files, and close serial monitors. Preserve any
device dump privately. Use the Cardputer ADV boot/download procedure from
M5Stack when a running firmware does not expose a usable port. The browser
detects ESP32-S3, not the board or cap: confirm the exact hardware yourself.

## Install using Launcher

1. Install the current official [Bmorcelli Launcher](https://github.com/bmorcelli/Launcher)
   build for Cardputer/ADV.
2. Copy the checksummed `LAUNCHER-APP.bin` onto microSD. In Launcher select
   **SD**, choose the binary and install it. Follow its partition-space prompt;
   preserve other apps/data before allowing repartitioning.
3. Confirm LORA Tunnel boots and can use the keyboard, SD, GNSS and LoRa.
4. Stop capture and wait for saving and any upload to finish, or cancel the
   upload and wait for network/storage ownership to release. Power-cycle the Cardputer.
   At the Launcher startup screen press **Enter** to enter its menu, then
   launch LORA Tunnel again. Let the timer expire on another cold start to
   verify automatic launch under Launcher's default settings.

Launcher allocates the app's OTA location. LORA Tunnel does not replace its
bootloader or hardcode a Launcher application offset. Preserve Launcher's NVS
and its other installed applications. The actual tested Launcher version will
be recorded with physical acceptance; host checks alone do not certify it.

## Build from source

```sh
python -m platformio run -e cardputer-adv-lora1262
python tools/package_cardputer.py --build .pio/build/cardputer-adv-lora1262 --output dist/cardputer-check --version 0.4.0-beta.1
```

The pinned default_8MB partition table gives app0/app1 **0x330000 bytes each**.
Packaging rejects oversized applications, wrong boards/versions, invalid
image checksums/digests, mismatched factory/app content, wrong partitions and
nonblank factory NVS. Use the matching release source and dependency companion.

## Internal-memory configuration

This target retains the pinned SDK's BLE role configuration for linker
compatibility; the application uses passive scanning only. A project-owned
forced-include header reduces connection capacity to one and trims unused
connection-oriented pool reservations after loading the SDK defaults. The
Heltec target keeps its existing settings.

Cardputer reserves four blocks in each MSYS pool and four controller-to-host
ACL blocks. The SDK defaults are 12, 24 and 24 respectively. MSYS block sizes
(256/320 bytes), HCI event counts/sizes, task stack size and the application's
512-byte evidence capacity are unchanged. Advertising reports use the HCI
event path; connection-oriented reservations are reduced. The two MSYS changes
remove 8,448 bytes of requested heap allocation. The compiled ACL pool removes
another 6,000 bytes (20 aligned 300-byte blocks), for 14,448 bytes of requested
pool savings. Controller/allocator overhead is separate and measured on-device.

The same Cardputer-only header sets the supported Wi-Fi initialization field
`csi_enable` to zero through `WIFI_INIT_CONFIG_DEFAULT`. This firmware does not
use channel-state samples; Wi-Fi AP/probe capture and TLS remain enabled. The
[Espressif configuration source](https://github.com/espressif/esp-idf/blob/v5.5.3/components/esp_wifi/Kconfig#L143-L150)
describes CSI memory proportional to the static RX buffer count; actual savings
for this binary require device measurement. Other Wi-Fi settings and BLE roles
retain their existing configuration.

`tools/cardputer_build.py` places the full SHA-256 of the forced header in the
Cardputer compiler flags before framework/library environments are cloned. A
header change therefore invalidates cached library objects as well as project
objects. Heltec receives neither the header nor the digest definition. Physical
reception, upload TLS memory and repeated capture/upload transitions must still
pass acceptance.

## Validation status

Cardputer firmware, Launcher installation, browser flashing, U214 reception,
GNSS, SD readback, keyboard, battery/power and sustained capture require the
physical device. See the [acceptance and publication checklist](CARDPUTER_RELEASE.md).
Heltec test results do not establish Cardputer hardware behavior.
