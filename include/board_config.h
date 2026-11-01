#pragma once

#include <cstdint>

// Heltec WiFi LoRa 32 Expansion Kit V2, ESP32-S3R8 / V4.3.2 mainboard.
// Electrical facts verified against the official V4-R8 and V2.03 schematics.
// See docs/HARDWARE.md. This pin map must not be used for Expansion Kit V1.
namespace board {
inline constexpr char NAME[] = "Heltec V4-R8 Expansion V2";
inline constexpr uint32_t FLASH_BYTES = 16U * 1024U * 1024U;
inline constexpr uint32_t PSRAM_BYTES = 8U * 1024U * 1024U;
inline constexpr uint16_t DISPLAY_WIDTH = 240;
inline constexpr uint16_t DISPLAY_HEIGHT = 320;
inline constexpr uint8_t DISPLAY_ROTATION = 0;

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
}
