#pragma once

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <SPI.h>

namespace hw {
// Initializes power, shared display/SD SPI, display and polled touch bus.
// The caller must serialize display calls and SD operations using one mutex.
// This function does not mount SD, start GNSS UART, or enable the LoRa FEM.
bool begin();
lgfx::LGFX_Device& display();
SPIClass& peripheralSpi();
bool touch(uint16_t& x, uint16_t& y);
bool touchPresent();
enum NavigationButton : uint8_t { Next = 1, Select = 2, Back = 4 };
// Poll every UI tick from one task; consume the returned one-shot bitmask.
// IO: Next on release. USER: Select on short release, Back after 700 ms hold.
// Poll and discard while navigation is disabled; no input event is queued.
uint8_t navigationButtons();
void beep();
float batteryVolts();
// Call only while SX1262 is in standby/RX, with DIO2 RF-switch control set up.
// No transmit operation is provided by this hardware layer.
void setLoRaFrontend(bool enabled, bool lna = false);
// Software sleep only: physical POWER/S3 drives the EC190708 battery latch
// directly and has no MCU connection in the Expansion V2.03 schematic.
// Prepare the USER/GPIO0 active-low wake source while peripherals remain on.
// Returns false if USER is held or wake configuration fails; display stays on.
bool prepareSleepHardware();
// Restore ordinary USER input if a later preparation step aborts before sleep.
// Safe even when no EXT0 wake source is armed; does not alter peripheral power.
void cancelSleepPreparation();
// Call only after prepareSleepHardware() succeeds, the capture/logger/uploader
// are stopped, SD files are flushed/closed and SD.end() has completed, radios
// are off and GNSS UART is ended. Caller must exclusively own the display/SPI
// bus and perform no further hardware work. Wake via USER restarts the firmware.
// This powers down switched peripherals and deep-sleeps ESP32; it does not
// disconnect the battery or claim zero consumption. It never returns.
[[noreturn]] void enterDeepSleep();
}
