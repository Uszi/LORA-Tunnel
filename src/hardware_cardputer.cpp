#include "board_config.h"

#if defined(LORA_TUNNEL_CARDPUTER_ADV)
#include "hardware.h"
#include "button_input.h"
#include "key_event_mapper.h"
#include "cardputer_keyboard_map.h"

#include <M5Cardputer.h>
#include <utility/Adafruit_TCA8418/Adafruit_TCA8418_registers.h>
#include <utility/PI4IOE5V6408_Class.hpp>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <cmath>

namespace {
// M5GFX owns SPI3 for the LCD. SD and the U214 SX1262 share this SPI2 instance.
SPIClass peripheralBus(FSPI);
m5::PI4IOE5V6408_Class capIo(board::CAP_IO_ADDRESS, 400000, &m5::In_I2C);
bool capReady = false;
bool keyboardReady = false;
bool initialized = false, hardwareReady = false;
uint32_t lastKeyboardPoll = 0;
tunnel::ButtonInput userButton;
input::KeyEventMapper keyMapper;

bool keyboardConfigured() {
  // The vendor reader's begin() is void. Check the actual matrix and event
  // configuration so an I2C ACK alone cannot mark failed initialization ready.
  const int i2cPort = static_cast<int>(m5::In_I2C.getPort());
  const int i2cSda = m5::In_I2C.getSDA(), i2cScl = m5::In_I2C.getSCL();
  // isEnabled() alone accepts the SDK's I2C_NUM_MAX sentinel; validate its
  // configured port and pins before attempting diagnostic register reads.
  const bool busValid = i2cPort >= 0 && i2cPort < SOC_I2C_NUM &&
      i2cSda >= 0 && i2cSda < GPIO_NUM_MAX &&
      i2cScl >= 0 && i2cScl < GPIO_NUM_MAX;
  uint8_t rows = 0, columns = 0, config = 0;
  bool rowsRead = false, columnsRead = false, configRead = false;
  if (busValid) {
    rowsRead = m5::In_I2C.readRegister(board::KEYBOARD_ADDRESS,
        TCA8418_REG_KP_GPIO_1, &rows, 1, 400000);
    columnsRead = m5::In_I2C.readRegister(board::KEYBOARD_ADDRESS,
        TCA8418_REG_KP_GPIO_2, &columns, 1, 400000);
    configRead = m5::In_I2C.readRegister(board::KEYBOARD_ADDRESS,
        TCA8418_REG_CFG, &config, 1, 400000);
  }
  Serial.printf("LT KEYBOARD board=%u display_board=%u i2c_enabled=%d i2c_valid=%d i2c_port=%d i2c_sda=%d i2c_scl=%d rows_ok=%d rows=0x%02X columns_ok=%d columns=0x%02X cfg_ok=%d cfg=0x%02X\n",
      static_cast<unsigned>(M5.getBoard()), static_cast<unsigned>(M5.Display.getBoard()),
      m5::In_I2C.isEnabled(), busValid, i2cPort, i2cSda, i2cScl,
      rowsRead, rows, columnsRead, columns, configRead, config);
  return M5.getBoard() == m5::board_t::board_M5CardputerADV &&
      rowsRead && rows == 0x7f && columnsRead && columns == 0xff &&
      configRead && (config & TCA8418_REG_CFG_KE_IEN);
}

void outputLevel(int pin, int level) {
  gpio_set_level(static_cast<gpio_num_t>(pin), level);
  pinMode(pin, OUTPUT);
  digitalWrite(pin, level);
}

void releaseSleepHolds() {
  gpio_deep_sleep_hold_dis();
  for (int pin : {board::LCD_BACKLIGHT, board::LORA_RESET,
                  board::LORA_CS, board::SD_CS}) {
    const auto gpio = static_cast<gpio_num_t>(pin);
    gpio_set_level(gpio, pin == board::LORA_CS || pin == board::SD_CS);
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    gpio_hold_dis(gpio);
  }
  rtc_gpio_deinit(static_cast<gpio_num_t>(board::USER_BUTTON));
}

void sampleKeyboard(uint32_t now) {
  input::KeyEvent keys[input::KeyEventMapper::KEY_COUNT]{};
  uint64_t down = 0;
  const auto& state = M5Cardputer.Keyboard.keysState();
  for (const auto& point : M5Cardputer.Keyboard.keyList()) {
    if (point.x < 0 || point.x >= 14 || point.y < 0 || point.y >= 4) continue;
    const size_t index = point.y * 14 + point.x;
    down |= uint64_t{1} << index;
    keys[index] = input::mapCardputerKey(M5Cardputer.Keyboard.getKeyValue(point),
                                       state, M5Cardputer.Keyboard.capslocked());
  }
  keyMapper.update(down, keys, now);
}
}

namespace hw {
bool begin() {
  if (initialized) return hardwareReady;
  initialized = true;
  releaseSleepHolds();
  outputLevel(board::LORA_RESET, LOW);
  outputLevel(board::LORA_CS, HIGH);
  outputLevel(board::SD_CS, HIGH);
  auto cfg = M5.config();
  cfg.fallback_board = m5::board_t::board_M5CardputerADV;
  cfg.internal_mic = false;
  cfg.internal_imu = false;
  cfg.internal_rtc = false;
  cfg.external_imu = false;
  cfg.external_rtc = false;
  cfg.external_display_value = 0; // This target has only the built-in LCD.
  cfg.internal_spk = true;
  cfg.led_brightness = 0;
  M5Cardputer.begin(cfg, true);
  // Short alert tones need a bounded audio queue. Preserve M5's ADV codec,
  // sample rate and pins; this also sizes its lazily created playback stack.
  auto speaker = M5.Speaker.config();
  speaker.dma_buf_len = 128;
  speaker.dma_buf_count = 4;
  M5.Speaker.config(speaker);
  M5.Display.setRotation(board::DISPLAY_ROTATION);
  M5.Display.setBrightness(150);
  M5.Speaker.setVolume(64);
  keyboardReady = keyboardConfigured();
  // Configure the U214 antenna-switch supply low until RadioLib is ready.
  // Missing U214 is a visible fault; it is not treated as an older U201 cap.
  capReady = capIo.begin() && capIo.digitalWrite(0, false) &&
      capIo.setDirection(0, true) && capIo.setHighImpedance(0, false);
  const bool spiReady = peripheralBus.begin(board::SD_SCK, board::SD_MISO,
      board::SD_MOSI, board::SD_CS);
  pinMode(board::USER_BUTTON, INPUT_PULLUP);
  userButton.reset(millis(), digitalRead(board::USER_BUTTON) == LOW);
  keyMapper.reset();
  hardwareReady = spiReady && M5.Display.width() == board::DISPLAY_WIDTH &&
      M5.Display.height() == board::DISPLAY_HEIGHT;
  Serial.printf("LT HARDWARE target=%s keyboard=%d cap=%d spi=%d\n",
                board::TARGET_ID, keyboardReady, capReady, spiReady);
  return hardwareReady;
}

lgfx::LGFX_Device& display() { return M5.Display; }
SPIClass& peripheralSpi() { return peripheralBus; }
SPIClass& radioSpi() { return peripheralBus; }
bool loraFrontendPresent() { return capReady; }
bool inputReady() { return keyboardReady; }
bool touchPresent() { return false; }
bool touch(uint16_t&, uint16_t&) { return false; }

bool pollKeyboard(input::KeyEvent& event) {
  if (keyMapper.pop(event)) return true;
  if (!keyboardReady) return false;
  const uint32_t now = millis();
  if (now - lastKeyboardPoll < 5) return false;
  lastKeyboardPoll = now;
  M5.update();
  // The vendor TCA8418 reader consumes one FIFO item per update. Drain a
  // bounded batch and observe each transition, including fast press/release.
  for (unsigned i = 0; i < 16; ++i) {
    M5Cardputer.Keyboard.updateKeyList();
    M5Cardputer.Keyboard.updateKeysState();
    sampleKeyboard(now);
    if (digitalRead(board::KEYBOARD_IRQ) != LOW) break;
  }
  return keyMapper.pop(event);
}

uint8_t navigationButtons() {
  const auto press = userButton.update(millis(),
      digitalRead(board::USER_BUTTON) == LOW);
  if (press == tunnel::ButtonEvent::ShortPress) return Select;
  if (press == tunnel::ButtonEvent::LongPress) return Back;
  return 0;
}

void beep() { M5.Speaker.tone(2200, 65); }

float batteryVolts() {
  const float volts = M5.Power.getBatteryVoltage() * 0.001f *
      board::BATTERY_CALIBRATION;
  return volts >= 2.5f && volts <= 4.5f ? volts : NAN;
}

void setLoRaFrontend(bool enabled, bool) {
  if (capReady && !capIo.digitalWrite(0, enabled)) capReady = false;
}

bool prepareSleepHardware() {
  const auto wake = static_cast<gpio_num_t>(board::USER_BUTTON);
  if (digitalRead(board::USER_BUTTON) == LOW ||
      !esp_sleep_is_valid_wakeup_gpio(wake)) return false;
  bool ok = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL) == ESP_OK;
  if (ok) ok = rtc_gpio_init(wake) == ESP_OK;
  if (ok) ok = rtc_gpio_set_direction(wake, RTC_GPIO_MODE_INPUT_ONLY) == ESP_OK;
  if (ok) ok = rtc_gpio_pulldown_dis(wake) == ESP_OK;
  if (ok) ok = rtc_gpio_pullup_en(wake) == ESP_OK;
  if (ok) ok = esp_sleep_enable_ext0_wakeup(wake, 0) == ESP_OK;
  if (ok && rtc_gpio_get_level(wake) == 0) ok = false;
  if (!ok) cancelSleepPreparation();
  return ok;
}

void cancelSleepPreparation() {
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT0);
  rtc_gpio_deinit(static_cast<gpio_num_t>(board::USER_BUTTON));
  pinMode(board::USER_BUTTON, INPUT_PULLUP);
}

[[noreturn]] void enterDeepSleep() {
  // Caller has stopped radios/UART, closed files and unmounted SD. The cap's
  // GNSS supply remains powered; this does not claim hardware power cutoff.
  M5.Speaker.end();
  setLoRaFrontend(false);
  M5.Display.waitDMA();
  M5.Display.setBrightness(0);
  M5.Display.sleep();
  M5.Display.waitDisplay();
  peripheralBus.end();
  for (int pin : {board::LCD_BACKLIGHT, board::LORA_RESET,
                  board::LORA_CS, board::SD_CS}) {
    outputLevel(pin, pin == board::LORA_CS || pin == board::SD_CS);
    gpio_hold_en(static_cast<gpio_num_t>(pin));
  }
  gpio_deep_sleep_hold_en();
  esp_deep_sleep_start();
}
}
#endif
