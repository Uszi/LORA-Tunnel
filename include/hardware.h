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
}
