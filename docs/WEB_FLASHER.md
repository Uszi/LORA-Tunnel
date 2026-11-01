# LORA Tunnel web installer

**Beta release target: 0.2.2-beta.1, Heltec Expansion Kit V2 / V4-R8, 915 MHz US configuration.** GPS position fixing remains unresolved; automatic uploads and full mesh payload decoding are unfinished. Read the [user guide](USER_GUIDE.md) and [validation record](VALIDATION.md) before installation.

Open the [LORA Tunnel web installer](https://valleytech-lora-tunnel.pages.dev/) or the [Valleytech Solutions project page](https://valleytechsolutions.tech/pages/lora-tunnel). Source is hosted on [GitHub](https://github.com/valleytechsolutions/LORA-Tunnel); a Gitea mirror is pending publication. Source builds remain available through the [README](../README.md#build-and-flash-from-source). Physical flashing through the browser remains unverified; successful command-line installation is recorded separately.

## Install

1. Confirm the actual [Heltec Expansion Kit V2 / V4-R8](https://heltec.org/project/v4-r8-ex/) board and 915 MHz variant. ESP32-S3 chip detection alone cannot distinguish compatible display, expansion wiring or flash/PSRAM variants. This image is not for Expansion Kit V1 or arbitrary ESP32-S3 boards.
2. Preserve the current firmware and board details using [RECOVERY.md](RECOVERY.md). Installation replaces firmware; it is not a stock-firmware backup operation. Prepare your microSD card using [sdcard/README.md](../sdcard/README.md).
3. Open the verified installer over HTTPS in a desktop browser with Web Serial support, such as Chrome or Edge. Use a USB data cable and close serial monitors or other applications holding the port. ESP Web Tools' [official documentation](https://esphome.github.io/esp-web-tools/) describes the browser/security requirements.
4. Select the exact beta release and matching board on the installer, connect, and choose the confirmed device port. Review the release notes and install prompt before writing. **Leave “Erase device” unchecked when relying on scoped recovery.** A full-chip erase changes regions outside that recovery set and requires a verified full-chip backup/recovery plan; the test unit's full backup did not pass that check.
5. Wait for installation to finish. Reconnect/restart if prompted, then check that LORA Tunnel boots, storage is available and the screen responds. Installation success alone does not prove GPS, SD file integrity or RF coverage.
6. Choose Wardriver or Recon and start a short test. Stop and let saving finish. Follow the [hardware checklist](TEST_CHECKLIST.md) before relying on a drive session.

If the port is unavailable, first check the cable, power, permissions and other serial applications. Use the documented V4-R8 bootloader procedure in [RECOVERY.md](RECOVERY.md); button names and sequences from unrelated ESP32 boards are not interchangeable. If flashing stops at flash-size configuration, the test unit required esptool 5.4.0's legacy stub; follow the explicit [command-line fallback](RECOVERY.md#legacy-stub-installation-fallback). A browser console is optional after installation, and standalone firmware does not wait for one.

## Images and updates

The installer manifest must pair one published version with its matching merged ESP32-S3 image and checksums. A merged factory image contains bootloader, partitions and application at their intended offsets. The application-only `firmware.bin` must not be substituted at address 0. Use the release's exact manifest/artifacts or PlatformIO's upload target rather than manually combining unrelated versions.

The current beta stores credentials on microSD and does not implement Improv Wi-Fi provisioning or an uploader. ESP Web Tools' optional Wi-Fi features do not add those capabilities to this firmware. Keep the SD card's private configuration and sessions out of published firmware/source artifacts.

The web installer uses [ESP Web Tools](https://github.com/esphome/esp-web-tools), pinned to 10.4.0 under Apache-2.0. Its retained license and provenance are under `web/vendor/esp-web-tools-10.4.0/`. Original firmware code is MIT licensed; imported component licenses remain separate. [Full attribution](THIRD_PARTY.md).
