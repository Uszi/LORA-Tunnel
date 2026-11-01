# Verified hardware target

Research date: 2026-11-01. The user confirmed **WiFi LoRa 32 Expansion Kit V2, 915 MHz variant**, and subsequently confirmed **United States operation** on the same date. The first receive profile is US Meshtastic LongFast at 906.875 MHz. The purchased kit name supports the V4-R8 target; the precise PCB silkscreen, RF front-end population, GNSS marking, and physical operation still require inspection. The board configuration targets the official **HTIT-WBR8H V4.3.2** mainboard and **Expansion board V2.03** schematic. Never select the V1 expansion pin map or an ordinary V4-R2 firmware image for this configuration.

Connected-unit preflight on 2026-11-01 used esptool 5.4.0 on the confirmed device port and identified an **ESP32-S3 QFN56 revision 0.2, embedded 8 MB PSRAM (AP, 3.3 V), and 16 MB quad flash**. Secure boot and flash encryption were reported disabled. This corroborates the chip/memory target; it does not inspect the expansion PCB, establish PSRAM runtime operation, or validate the display, touch, GNSS, SD, or RF front-end. Full-flash read discrepancies, scoped recovery acquisition and later installation checks are recorded in [VALIDATION.md](VALIDATION.md).

The manufacturer's [V2 product page](https://heltec.org/project/v4-r8-ex/) explicitly distinguishes its firmware from V1. The official [V4-R8 documentation](https://wiki.heltec.org/docs/devices/open-source-hardware/esp32-series/lora-32/wifi-lora-32-v4r8/) links the schematics below. Product pages cannot establish the exact PCB fitted inside an individual unit.

## Electrical source records

- [Mainboard HTIT-WBR8H V4.3.2 schematic](https://resource.heltec.cn/download/WiFi_LoRa_32_V4-R8/Schematic/HTIT-WBR8H_V4.3.2.pdf): visually inspected the full drawing, including power, RF front-end, battery divider, MCU, and connector sections.
- [Expansion board V2.03 schematic](https://resource.heltec.cn/download/WiFi_LoRa_32_Expansion_Kit-V2/schematic/Expansion_board_V2.03.pdf): visually inspected the full drawing, including GNSS, buzzer, LCD/touch connector, and SD wiring.
- [V4-R8 datasheet](https://resource.heltec.cn/download/WiFi_LoRa_32_V4-R8/datasheet/WiFi_LoRa_32_V4R8.pdf).
- [Display module UN280286-B-V01 specification](https://resource.heltec.cn/download/WiFi_LoRa_32_Expansion_Kit/Touchscreen-Data/UN280286-B-V01(1).pdf), page 2: 240 x 320, 2.8-inch, four-wire SPI, 7789P3 controller.
- [CHSC6xxx application note](https://resource.heltec.cn/download/WiFi_LoRa_32_Expansion_Kit/Touchscreen-Data/Application%C2%A0Note%C2%A0for%C2%A0CTPM_CHSC6xxx(1).pdf), sections 1.2-1.3: I2C address and normal touch-report format. The supplied documents establish the family; the exact fitted touch silicon suffix is not verified.

## Cross-checks against source code

The hardware layer uses electrical facts and public driver APIs, with original project code. Meshtastic firmware/UI implementation code was researched, not copied into this project.

| Upstream | Immutable revision | Files inspected |
| --- | --- | --- |
| Heltec ESP32 library | `6ab5cf916e01b0930eaa2245323c728f849705c4` | [Expansion kit factory test](https://github.com/HelTecAutomation/Heltec_ESP32/blob/6ab5cf916e01b0930eaa2245323c728f849705c4/examples/Factory_Test/Lora32_R8_V4.3_Exp_Board_HF/Lora32_R8_V4.3_Exp_Board_HF.ino) |
| Meshtastic firmware master | `6d41e279f1f51bd59f687b9d441c1bf47b1594fc` | [V4-R8 build definitions](https://github.com/meshtastic/firmware/blob/6d41e279f1f51bd59f687b9d441c1bf47b1594fc/variants/esp32s3/heltec_v4_r8/platformio.ini), [board JSON](https://github.com/meshtastic/firmware/blob/6d41e279f1f51bd59f687b9d441c1bf47b1594fc/boards/heltec_v4_r8.json) |
| Meshtastic firmware develop | `86d4d4a06c805bc0ccd812078c0ed1d8eb8a4c54` | [variant definitions](https://github.com/meshtastic/firmware/blob/86d4d4a06c805bc0ccd812078c0ed1d8eb8a4c54/variants/esp32s3/heltec_v4_r8/variant.h), with corrected GNSS direction comments |
| Meshtastic device UI | `a5b80d4d5708d0563ea9e0c0277dc8984e2fb952` | [Heltec display configuration](https://github.com/meshtastic/device-ui/blob/a5b80d4d5708d0563ea9e0c0277dc8984e2fb952/include/graphics/LGFX/LGFX_HELTEC_V4_TFT.h) |
| LovyanGFX 1.2.24 | `e21afa7c8772aa02fd0332bf7a446b9fcada81db` | [SPI implementation](https://github.com/lovyan03/LovyanGFX/blob/e21afa7c8772aa02fd0332bf7a446b9fcada81db/src/lgfx/v1/platforms/esp32/common.cpp), BSD-2-Clause library |

## Board configuration

All GPIOs are ESP32 GPIO numbers, not connector positions. Constants live in `include/board_config.h`. The original PlatformIO definition is `boards/heltec_v4_r8_tft.json`; it uses Arduino's generic `esp32s3` variant only for the core infrastructure, with every peripheral pin set explicitly by this project's board configuration. Its `maximum_ram_size` describes the conventional internal-RAM build accounting limit, not the separate 8 MB PSRAM capacity.

| Function | Verified configuration | Source / qualification |
| --- | --- | --- |
| MCU/memory | ESP32-S3R8, 16 MB external flash, 8 MB octal PSRAM | Mainboard U7/U6; upstream board definition selects QIO flash and OPI PSRAM |
| TFT | ST7789-family / 7789P3, 240 x 320, 2.8-inch | Manufacturer display specification |
| TFT SPI | SCK 16, MOSI 15, MISO 45, CS 47, D/C 48 | Expansion schematic plus V4-R8 TFT build definitions |
| TFT reset/backlight | Reset 21, backlight 44 | Reset is shared physically with touch |
| Touch | CHSC6xxx, I2C SDA 17, SCL 18, address 0x2E | Application note plus expansion schematic; polled in this project |
| Touch interrupt | GPIO43 optional connection | Not required for polling; no assumption that this optional link is populated |
| SD | SPI shared with TFT, CS 3 | Expansion schematic; Heltec factory test starts at 8 MHz |
| Peripheral power | Vext enable 40, active low | Feeds display/SD and touch I2C pullups; do not disable while accessing SD |
| GNSS | Quectel L76K in manufacturer schematic | Module marking remains a physical check; some upstream comments mention CM121 |
| GNSS UART | **ESP32 RX 39, ESP32 TX 38, 9600 baud, 8N1** | Expansion schematic and Heltec factory test `Serial1.begin(9600,SERIAL_8N1,39,38)` agree |
| GNSS power/PPS | Power 42 active low; PPS 41 | Do not repurpose GPIO42 as a separate reset on this V2 configuration |
| LoRa transceiver | SX1262 | Mainboard schematic; one receive frequency/modem configuration at a time |
| LoRa SPI | SCK 9, MOSI 10, MISO 11, CS 8 | Independent from display/SD SPI |
| LoRa control | RESET 12, BUSY 13, DIO1/IRQ 14 | SX1262 DIO2 is RF switching, not an ESP32 interrupt input |
| LoRa oscillator | SX1262 DIO3 controls 1.8 V TCXO | Upstream variant and schematic oscillator circuit |
| RF front-end | KCT8103L, power GPIO7, enable/CSD GPIO2, mode/CTX GPIO5 | V4.3.2 high-band schematic; verify actual revision before enabling reception |
| Buzzer | GPIO4 -> PAM8904 DIN | Expansion schematic and manufacturer factory test agree; GPIO6 belongs to older configurations |
| Buttons | Boot/user GPIO0; secondary GPIO46 | Button behavior and active level need physical validation |
| Battery | ADC GPIO1; 390 kOhm / 100 kOhm divider, factor 4.9 | Mainboard schematic; no old-board ADC-enable pin required |
| Antennas | Supplied LoRa SMA whip via kit connection; board Wi-Fi/BLE antenna; GNSS antenna on expansion | User confirmed 915 MHz hardware and United States operation; initial receive profile is US LongFast, 906.875 MHz |

**GNSS naming trap:** older master-branch comments describe `GPS_TX_PIN` and `GPS_RX_PIN` inconsistently. The actual ESP32 receive pin in this kit is **39**. The manufacturer's test and V2 schematic are the deciding evidence. Firmware uses `GNSS_RX` and `GNSS_TX` strictly from the ESP32 perspective.

## Framework and shared-bus decisions

Use PlatformIO with a pinned pioarduino Arduino-ESP32 3.3.12 platform, LovyanGFX 1.2.24 for ST7789 graphics, the Arduino SD driver, and an original small CHSC6xxx polling adapter. This gives explicit support for the S3 octal PSRAM and matches the display library selected by the upstream V4-R8 target. Full firmware dependencies are pinned in `platformio.ini`; changing framework/library versions requires rebuilding and rerunning the bus checks.

On ESP32-S3, Arduino `HSPI` maps to hardware `SPI3_HOST`, while Arduino `FSPI` maps to `SPI2_HOST`, as defined in [Arduino 3.3.12 SPI HAL](https://github.com/espressif/arduino-esp32/blob/3.3.12/cores/esp32/esp32-hal-spi.h). TFT and SD both use SPI3. SX1262 must use SPI2, with its own pins. The display initializes a shared SPI3 instance with transaction locking; SD must receive `hw::peripheralSpi()`. Initialize this once, then protect all display and SD operations with the same application mutex. Do not call `SPI.end()` or reassign a default SPI bus after tasks start.

Initial clocks are 40 MHz TFT and 8 MHz SD. The factory/upstream examples use different clock limits; 75 MHz is not assumed safe for every SD card or kit. DMA is disabled for this bring-up implementation. Acquire the common mutex for a bounded draw/write batch, finish display transactions before releasing it, and release it between batches. A slow card can still block a transaction; measure maximum UI latency and logging losses on hardware.

The touchscreen adapter reads a seven-byte report with a 10 ms I2C timeout, rejects absent/out-of-range points and released contacts, and uses portrait coordinates. Verify all four corners before accepting the mapping. An I2C ACK proves presence, not correct coordinate orientation.

## Power and receive-only startup

At boot, `hw::begin()` holds front-end CSD low and VFEM power low, places CTX high, and deselects LCD, SD and LoRa. It enables Vext and GNSS with their active-low controls, waits one second for peripherals, then starts display/touch. The GNSS UART is started by the application. It does not initiate a radio transmission or start LoRa reception.

For the verified KCT8103L revision, initialize SX1262 in standby first and configure DIO2 RF-switch handling. Only then call `hw::setLoRaFrontend(true, false)` for receive bypass or `hw::setLoRaFrontend(true, true)` for receive LNA. GPIO7 powers the FEM, GPIO2 enables it, and GPIO5 low selects LNA. When disabled, the helper removes FEM enable before power. Hold the SX1262 in standby during mode changes. A powered-off FEM blocks useful reception; it is not equivalent to receive bypass.

The LNA setting remains configurable because gain can overload the receiver near strong signals. It does not establish a sensitivity improvement in this unit until measured. Short device checks recorded LoRa packet reception, but no controlled LNA/bypass sensitivity comparison, RF calibration or electrical measurement has been performed.

Battery voltage uses calibrated ESP32 ADC millivolts and the schematic divider ratio. It returns unavailable for readings outside a plausible battery range. A valid voltage does not prove a battery is fitted, report charging status, or justify an accurate percentage. A unit-specific multiplier may be set only after comparison with a meter.

## Physical acceptance still required

Before flashing, record PCB labels and stock firmware identity, back up flash using the documented recovery procedure, and retain the matching V4-R8 TFT factory image. Initial validation must cover boot without USB, flash/PSRAM reported sizes, display color/orientation, four-corner touch, GNSS bytes/fix/time, SD read/write and failure behavior, buzzer, battery readings, and RF-front-end settings. A build pass is not any of these results.
