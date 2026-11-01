#include "hardware.h"
#include "board_config.h"
#include "button_input.h"

#include <Wire.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <cmath>

namespace {
// On ESP32-S3 Arduino HSPI is SPI3; FSPI/SPI2 remains available for SX1262.
SPIClass peripheralBus(HSPI);

class TunnelDisplay final : public lgfx::LGFX_Device {
  lgfx::Bus_SPI bus_;
  lgfx::Panel_ST7789 panel_;
  lgfx::Light_PWM light_;

public:
  TunnelDisplay() {
    auto spi = bus_.config();
    spi.spi_host = SPI3_HOST;
    spi.spi_mode = 0;
    spi.freq_write = board::LCD_SPI_HZ;
    spi.freq_read = 8000000;
    spi.spi_3wire = false;
    spi.use_lock = true;
    // Synchronous transfers simplify shared SD ownership in bring-up.
    spi.dma_channel = 0;
    spi.pin_sclk = board::LCD_SCK;
    spi.pin_mosi = board::LCD_MOSI;
    spi.pin_miso = board::LCD_MISO;
    spi.pin_dc = board::LCD_DC;
    bus_.config(spi);
    panel_.setBus(&bus_);

    auto panel = panel_.config();
    panel.pin_cs = board::LCD_CS;
    panel.pin_rst = board::LCD_RESET;
    panel.pin_busy = -1;
    panel.panel_width = board::DISPLAY_WIDTH;
    panel.panel_height = board::DISPLAY_HEIGHT;
    panel.memory_width = board::DISPLAY_WIDTH;
    panel.memory_height = board::DISPLAY_HEIGHT;
    panel.offset_x = 0;
    panel.offset_y = 0;
    panel.offset_rotation = 0;
    panel.readable = false;
    panel.invert = true;
    panel.rgb_order = false;
    panel.bus_shared = true;
    panel_.config(panel);

    auto light = light_.config();
    light.pin_bl = board::LCD_BACKLIGHT;
    light.invert = false;
    light.freq = 20000;
    light.pwm_channel = 7;
    light_.config(light);
    panel_.setLight(&light_);
    setPanel(&panel_);
  }
};

TunnelDisplay screen;
bool hasTouch = false;
tunnel::ButtonInput userButton;
tunnel::ButtonInput secondaryButton(false);

bool buttonPressed(int pin, bool activeLow) {
  const bool high = digitalRead(pin) == HIGH;
  return activeLow ? !high : high;
}

void outputLevel(int pin, int level) {
  // Arduino 3 digitalWrite ignores pins not yet registered with periman.
  // Set the hardware latch directly before enabling output, then register it.
  gpio_set_level(static_cast<gpio_num_t>(pin), level);
  pinMode(pin, OUTPUT);
  digitalWrite(pin, level);
}
}

namespace hw {
bool begin() {
  // Keep the front-end disabled throughout boot and hardware diagnostics.
  outputLevel(board::FEM_ENABLE, LOW);
  outputLevel(board::FEM_POWER, LOW);
  outputLevel(board::FEM_CTX, HIGH);
  outputLevel(board::LORA_CS, HIGH);
  outputLevel(board::SD_CS, HIGH);
  outputLevel(board::LCD_CS, HIGH);
  outputLevel(board::BUZZER, LOW);
  outputLevel(board::VEXT_ENABLE, LOW);
  outputLevel(board::GNSS_ENABLE, LOW);
  pinMode(board::GNSS_PPS, INPUT);
  pinMode(board::USER_BUTTON, INPUT_PULLUP);
  pinMode(board::SECONDARY_BUTTON, INPUT);
  const uint32_t buttonNow = millis();
  userButton.reset(buttonNow, buttonPressed(board::USER_BUTTON, board::USER_BUTTON_ACTIVE_LOW));
  secondaryButton.reset(buttonNow, buttonPressed(board::SECONDARY_BUTTON, board::SECONDARY_BUTTON_ACTIVE_LOW));
  analogSetPinAttenuation(board::BATTERY_ADC, ADC_2_5db);

  delay(1000); // Peripheral power settling, before any concurrent tasks start.
  peripheralBus.begin(board::LCD_SCK, board::LCD_MISO, board::LCD_MOSI,
                      board::SD_CS);
  const bool displayInitialized = screen.init();
  screen.setRotation(board::DISPLAY_ROTATION);
  screen.setBrightness(150);

  // Display reset also reset the CHSC6xxx. It enters normal mapped mode itself.
  Wire.begin(board::TOUCH_SDA, board::TOUCH_SCL, 100000);
  Wire.setTimeOut(10);
  delay(60);
  Wire.beginTransmission(board::TOUCH_ADDRESS);
  hasTouch = Wire.endTransmission() == 0;
  return displayInitialized;
}

lgfx::LGFX_Device& display() { return screen; }
SPIClass& peripheralSpi() { return peripheralBus; }
bool touchPresent() { return hasTouch; }

uint8_t navigationButtons() {
  const uint32_t now = millis();
  const auto io = secondaryButton.update(
      now, buttonPressed(board::SECONDARY_BUTTON, board::SECONDARY_BUTTON_ACTIVE_LOW));
  const auto user = userButton.update(
      now, buttonPressed(board::USER_BUTTON, board::USER_BUTTON_ACTIVE_LOW));
  uint8_t events = 0;
  if (io == tunnel::ButtonEvent::ShortPress) events |= Next;
  if (user == tunnel::ButtonEvent::ShortPress) events |= Select;
  if (user == tunnel::ButtonEvent::LongPress) events |= Back;
  return events;
}

bool touch(uint16_t& x, uint16_t& y) {
  if (!hasTouch) return false;
  // Normal mapped register mode, per CHSC6xxx application note section 1.3.
  Wire.beginTransmission(board::TOUCH_ADDRESS);
  Wire.write(uint8_t{0});
  if (Wire.endTransmission(true) != 0) return false;
  uint8_t report[7];
  if (Wire.requestFrom(board::TOUCH_ADDRESS, uint8_t{sizeof(report)}, true) !=
      sizeof(report)) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (auto& byte : report) byte = static_cast<uint8_t>(Wire.read());
  const uint8_t points = report[2] & 0x07;
  const uint8_t event = report[3] >> 6;
  if (points == 0 || points > 2 || event == 1 || event == 3) return false;
  const uint16_t rawX = ((report[3] & 0x0f) << 8) | report[4];
  const uint16_t rawY = ((report[5] & 0x0f) << 8) | report[6];
  if (rawX >= board::DISPLAY_WIDTH || rawY >= board::DISPLAY_HEIGHT) return false;
  x = rawX;
  y = rawY;
  return true;
}

void beep() {
  // Arduino's bounded-duration tone task turns this off without blocking UI.
  tone(board::BUZZER, 2200, 65);
}

float batteryVolts() {
  uint32_t millivolts = 0;
  for (int i = 0; i < 8; ++i) millivolts += analogReadMilliVolts(board::BATTERY_ADC);
  const float volts = (millivolts / 8000.0f) * board::BATTERY_DIVIDER *
                      board::BATTERY_CALIBRATION;
  // Voltage alone cannot identify a battery, charging state, or accurate SOC.
  return (volts >= 2.5f && volts <= 4.5f) ? volts : NAN;
}

void setLoRaFrontend(bool enabled, bool lna) {
  digitalWrite(board::FEM_ENABLE, LOW);
  if (!enabled) {
    digitalWrite(board::FEM_POWER, LOW);
    digitalWrite(board::FEM_CTX, HIGH);
    return;
  }
  digitalWrite(board::FEM_CTX, lna ? LOW : HIGH);
  digitalWrite(board::FEM_POWER, HIGH);
  delay(2);
  digitalWrite(board::FEM_ENABLE, HIGH);
}
}
