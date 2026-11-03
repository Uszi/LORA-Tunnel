#pragma once

#include <cstdint>

// Board-specific electrical and resource profiles. The Cardputer target is
// ADV + U214 only. The Heltec target is V4-R8 / Expansion V2, never V1.
// Electrical references and validation boundaries are in docs/HARDWARE.md.
namespace board {
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
inline constexpr char TARGET_ID[] = "cardputer-adv-lora1262";
inline constexpr char NAME[] = "Cardputer ADV + LoRa-1262 U214";
inline constexpr uint32_t FLASH_BYTES = 8U * 1024U * 1024U;
inline constexpr uint32_t PSRAM_BYTES = 0;
inline constexpr bool HAS_TOUCH = false;
inline constexpr bool HAS_KEYBOARD = true;
inline constexpr bool DISPLAY_SHARED_WITH_SD = false;
inline constexpr bool LORA_SHARED_WITH_SD = true;
inline constexpr bool GNSS_PCAS = false;
inline constexpr float LORA_MIN_MHZ = 868.0f;
inline constexpr float LORA_MAX_MHZ = 923.0f;
inline constexpr uint16_t CAPTURE_RECORDS = 12;
inline constexpr uint16_t WRITER_RECORDS = 8;
// HW4 measured 7,192 bytes used; retain >3 KiB margin for HW5 validation.
inline constexpr uint32_t WRITER_STACK_BYTES = 10240;
inline constexpr uint16_t MANAGEMENT_RESERVE = 2;
inline constexpr uint16_t SUMMARY_RECORDS = 32;
inline constexpr uint16_t DISPLAY_WIDTH = 240;
inline constexpr uint16_t DISPLAY_HEIGHT = 135;
inline constexpr uint8_t DISPLAY_ROTATION = 1;
inline constexpr uint8_t DISPLAY_COLOR_DEPTH = 8;

inline constexpr int LCD_SCK = 36;
inline constexpr int LCD_MOSI = 35;
inline constexpr int LCD_MISO = -1;
inline constexpr int LCD_CS = 37;
inline constexpr int LCD_DC = 34;
inline constexpr int LCD_RESET = 33;
inline constexpr int LCD_BACKLIGHT = 38; // Also supplies the Stamp-S3A RGB LED.
inline constexpr uint32_t LCD_SPI_HZ = 40000000;
inline constexpr int SD_SCK = 40;
inline constexpr int SD_MOSI = 14;
inline constexpr int SD_MISO = 39;
inline constexpr int SD_CS = 12;
inline constexpr uint32_t SD_SPI_HZ = 8000000;
inline constexpr int INTERNAL_SDA = 8;
inline constexpr int INTERNAL_SCL = 9;
inline constexpr int KEYBOARD_IRQ = 11;
inline constexpr uint8_t KEYBOARD_ADDRESS = 0x34;
inline constexpr uint8_t CAP_IO_ADDRESS = 0x43;

inline constexpr int GNSS_RX = 15; // ESP32 input, ATGM336H-6N TXD.
inline constexpr int GNSS_TX = 13;
inline constexpr uint32_t GNSS_BAUD = 115200;
// The cap exposes no MCU-controlled GNSS supply/reset or PPS connection.
inline constexpr int GNSS_ENABLE = -1;
inline constexpr int GNSS_PPS = -1;
inline constexpr int LORA_SCK = SD_SCK;
inline constexpr int LORA_MOSI = SD_MOSI;
inline constexpr int LORA_MISO = SD_MISO;
inline constexpr int LORA_CS = 5;
inline constexpr int LORA_RESET = 3;
inline constexpr int LORA_IRQ = 4;
inline constexpr int LORA_BUSY = 6;
inline constexpr float LORA_TCXO_VOLTS = 3.0f;
inline constexpr bool LORA_USE_LDO = true;
inline constexpr int USER_BUTTON = 0;
inline constexpr bool USER_BUTTON_ACTIVE_LOW = true;
inline constexpr int BATTERY_ADC = 10;
inline constexpr float BATTERY_DIVIDER = 2.0f;
inline constexpr float BATTERY_CALIBRATION = 1.0f;
#else
inline constexpr char TARGET_ID[] = "heltec-v4-r8-tft";
inline constexpr char NAME[] = "Heltec V4-R8 Expansion V2";
inline constexpr uint32_t FLASH_BYTES = 16U * 1024U * 1024U;
inline constexpr uint32_t PSRAM_BYTES = 8U * 1024U * 1024U;
inline constexpr uint16_t DISPLAY_WIDTH = 240;
inline constexpr uint16_t DISPLAY_HEIGHT = 320;
inline constexpr uint8_t DISPLAY_ROTATION = 0;
inline constexpr uint8_t DISPLAY_COLOR_DEPTH = 16;
inline constexpr bool HAS_TOUCH = true;
inline constexpr bool HAS_KEYBOARD = false;
inline constexpr bool DISPLAY_SHARED_WITH_SD = true;
inline constexpr bool LORA_SHARED_WITH_SD = false;
inline constexpr bool GNSS_PCAS = true;
inline constexpr float LORA_MIN_MHZ = 863.0f;
inline constexpr float LORA_MAX_MHZ = 928.0f;
inline constexpr uint16_t CAPTURE_RECORDS = 48;
inline constexpr uint16_t WRITER_RECORDS = 32;
inline constexpr uint32_t WRITER_STACK_BYTES = 12288;
inline constexpr uint16_t MANAGEMENT_RESERVE = 8;
inline constexpr uint16_t SUMMARY_RECORDS = 64;

inline constexpr int VEXT_ENABLE = 40; // Active low; display, SD, touch pullups.
inline constexpr int LCD_SCK = 16;
inline constexpr int LCD_MOSI = 15;
inline constexpr int LCD_MISO = 45;
inline constexpr int LCD_CS = 47;
inline constexpr int LCD_DC = 48;
inline constexpr int LCD_RESET = 21; // Physically shared with touch reset.
inline constexpr int LCD_BACKLIGHT = 44;
inline constexpr uint32_t LCD_SPI_HZ = 40000000;
inline constexpr int SD_CS = 3;
inline constexpr int SD_SCK = LCD_SCK;
inline constexpr int SD_MOSI = LCD_MOSI;
inline constexpr int SD_MISO = LCD_MISO;
inline constexpr uint32_t SD_SPI_HZ = 8000000;

inline constexpr int TOUCH_SDA = 17;
inline constexpr int TOUCH_SCL = 18;
inline constexpr uint8_t TOUCH_ADDRESS = 0x2e;
// TP_INT is an optional GPIO43 connection in the V2.03 drawing; poll instead.
inline constexpr int TOUCH_INTERRUPT = -1;

inline constexpr int GNSS_RX = 39; // ESP32 input, L76K TXD.
inline constexpr int GNSS_TX = 38; // ESP32 output, L76K RXD.
inline constexpr int GNSS_ENABLE = 42; // Active low.
inline constexpr int GNSS_PPS = 41;
inline constexpr uint32_t GNSS_BAUD = 9600;

inline constexpr int LORA_SCK = 9;
inline constexpr int LORA_MOSI = 10;
inline constexpr int LORA_MISO = 11;
inline constexpr int LORA_CS = 8;
inline constexpr int LORA_RESET = 12;
inline constexpr int LORA_IRQ = 14;
inline constexpr int LORA_BUSY = 13;
inline constexpr float LORA_TCXO_VOLTS = 1.8f;
inline constexpr bool LORA_USE_LDO = false;
inline constexpr int FEM_POWER = 7;
inline constexpr int FEM_ENABLE = 2;
inline constexpr int FEM_CTX = 5; // Low=LNA RX; high=bypass when SX DIO2 is low.

inline constexpr int BUZZER = 4;
inline constexpr int USER_BUTTON = 0;
inline constexpr int SECONDARY_BUTTON = 46;
// V4.3.2 R12 pulls USER up; Expansion V2.03 S4 presses it to ground.
inline constexpr bool USER_BUTTON_ACTIVE_LOW = true;
// Expansion V2.03 S1/R17 drives KEY high; V4.3.2 R47 biases GPIO46 low.
// Keep GPIO46 INPUT: external bias exists. Verify fitted values/levels on PCB.
inline constexpr bool SECONDARY_BUTTON_ACTIVE_LOW = false;
inline constexpr int BATTERY_ADC = 1;
inline constexpr float BATTERY_DIVIDER = 4.9f; // (390k + 100k) / 100k.
// Keep 1.0 until a multimeter calibration is measured for the actual unit.
inline constexpr float BATTERY_CALIBRATION = 1.0f;
#endif
}
