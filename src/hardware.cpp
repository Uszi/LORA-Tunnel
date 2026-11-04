#include "hardware.h"
#include "board_config.h"
#include "button_input.h"

#if !defined(LORA_TUNNEL_CARDPUTER_ADV)
#include <Wire.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <driver/spi_master.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <cmath>
#include <freertos/semphr.h>

namespace {
// On ESP32-S3 Arduino HSPI is SPI3; FSPI/SPI2 remains available for SX1262.
SPIClass peripheralBus(HSPI);
SPIClass radioBus(FSPI);
bool audioReady=false;
esp_timer_handle_t audioOffTimer=nullptr;
uint32_t audioDeadline=0;
StaticSemaphore_t audioMutexStorage;
SemaphoreHandle_t audioMutex=nullptr;

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
bool initialized = false, hardwareReady = false;
tunnel::ButtonInput userButton;
tunnel::ButtonInput secondaryButton(false);

struct SleepOutput { int pin; int level; };
constexpr SleepOutput sleepOutputs[]={
  {board::VEXT_ENABLE,HIGH}, {board::GNSS_ENABLE,HIGH},
  {board::LCD_BACKLIGHT,LOW}, {board::FEM_ENABLE,LOW},
  {board::FEM_POWER,LOW}, {board::FEM_CTX,LOW},
  {board::LORA_RESET,LOW}, {board::BUZZER,LOW}
};

void releaseSleepHolds() {
  gpio_deep_sleep_hold_dis();
  // Set the desired safe level before releasing retained pads on a sleep wake.
  // Do this on every boot so a reset after sleep preparation also recovers.
  for(const auto& output:sleepOutputs) {
    const auto pin=static_cast<gpio_num_t>(output.pin);
    gpio_set_level(pin,output.level);
    gpio_set_direction(pin,GPIO_MODE_OUTPUT);
    gpio_hold_dis(pin);
  }
  rtc_gpio_deinit(static_cast<gpio_num_t>(board::USER_BUTTON));
}

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
  if (initialized) return hardwareReady;
  initialized = true;
  releaseSleepHolds();
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
  const bool peripheralReady = peripheralBus.begin(board::LCD_SCK,
      board::LCD_MISO, board::LCD_MOSI, board::SD_CS);
  const bool radioReady = radioBus.begin(board::LORA_SCK, board::LORA_MISO,
      board::LORA_MOSI, board::LORA_CS);
  const bool displayInitialized = screen.init();
  screen.setRotation(board::DISPLAY_ROTATION);
  screen.setBrightness(150);
  // Own one PWM output without Arduino tone()'s queued, duration-blocked task.
  audioMutex=xSemaphoreCreateMutexStatic(&audioMutexStorage);
  const bool pwmReady=audioMutex && ledcAttach(board::BUZZER,2200,10);
  if(pwmReady) {
    ledcWriteTone(board::BUZZER,0);
    esp_timer_create_args_t timer{};
    timer.callback=[](void*) {
      // Timer task and main loop share the driver; an old callback must not
      // silence a new note or race detaching its pin during sleep.
      xSemaphoreTake(audioMutex,portMAX_DELAY);
      if(audioReady && int32_t(millis()-audioDeadline)>=0)ledcWriteTone(board::BUZZER,0);
      xSemaphoreGive(audioMutex);
    };
    timer.name="alert-off";
    audioReady=esp_timer_create(&timer,&audioOffTimer)==ESP_OK;
    if(!audioReady)ledcDetach(board::BUZZER);
  }
  Serial.printf("LT AUDIO ready=%d\n",audioReady);

  // Display reset also reset the CHSC6xxx. It enters normal mapped mode itself.
  Wire.begin(board::TOUCH_SDA, board::TOUCH_SCL, 100000);
  Wire.setTimeOut(10);
  delay(60);
  Wire.beginTransmission(board::TOUCH_ADDRESS);
  hasTouch = Wire.endTransmission() == 0;
  hardwareReady = displayInitialized && peripheralReady && radioReady;
  Serial.printf("LT HARDWARE target=%s spi=%d\n", board::TARGET_ID,
                peripheralReady && radioReady);
  return hardwareReady;
}

lgfx::LGFX_Device& display() { return screen; }
SPIClass& peripheralSpi() { return peripheralBus; }
SPIClass& radioSpi() { return radioBus; }
bool loraFrontendPresent() { return true; }
bool inputReady() { return hasTouch; }
bool pollKeyboard(input::KeyEvent&) { return false; }
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

bool alertAudioReady() { return audioReady; }
void alertTone(uint16_t frequencyHz, uint16_t durationMs) {
  if(!audioReady)return;
  xSemaphoreTake(audioMutex,portMAX_DELAY);
  esp_timer_stop(audioOffTimer);
  audioDeadline=millis()+durationMs;
  if(!frequencyHz || !durationMs)ledcWriteTone(board::BUZZER,0);
  else {
    ledcWriteTone(board::BUZZER,frequencyHz);
    if(esp_timer_start_once(audioOffTimer,uint64_t(durationMs)*1000)!=ESP_OK)
      ledcWriteTone(board::BUZZER,0);
  }
  xSemaphoreGive(audioMutex);
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

bool prepareSleepHardware() {
  const auto wake=static_cast<gpio_num_t>(board::USER_BUTTON);
  // Never enter an immediate sleep/wake loop while the wake button is held.
  if(buttonPressed(board::USER_BUTTON,board::USER_BUTTON_ACTIVE_LOW) ||
     !esp_sleep_is_valid_wakeup_gpio(wake)) return false;
  bool ok=esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL)==ESP_OK;
  if(ok) ok=rtc_gpio_init(wake)==ESP_OK;
  if(ok) ok=rtc_gpio_set_direction(wake,RTC_GPIO_MODE_INPUT_ONLY)==ESP_OK;
  if(ok) ok=rtc_gpio_pulldown_dis(wake)==ESP_OK;
  if(ok) ok=rtc_gpio_pullup_en(wake)==ESP_OK;
  if(ok) ok=esp_sleep_enable_ext0_wakeup(wake,0)==ESP_OK;
  if(ok && rtc_gpio_get_level(wake)==0) ok=false;
  if(!ok) cancelSleepPreparation();
  return ok;
}

void cancelSleepPreparation() {
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT0);
  rtc_gpio_deinit(static_cast<gpio_num_t>(board::USER_BUTTON));
  pinMode(board::USER_BUTTON,INPUT_PULLUP);
}

[[noreturn]] void enterDeepSleep() {
  // The caller has already saved/unmounted SD and stopped asynchronous users.
  alertTone(0,0);
  if(audioReady) {
    xSemaphoreTake(audioMutex,portMAX_DELAY);
    audioReady=false;
    ledcDetach(board::BUZZER);
    xSemaphoreGive(audioMutex);
  }
  screen.waitDMA();
  screen.setBrightness(0);
  screen.sleep();
  screen.waitDisplay();
  Wire.end();
  peripheralBus.end();
  radioBus.end();
  hasTouch=false;

  // Drive no SPI/I2C/UART signal high into a peripheral whose rail is removed.
  // No pin in this list is a flash/PSRAM/USB signal or the USER wake source.
  constexpr int floatingPins[]={
    board::LCD_SCK,board::LCD_MOSI,board::LCD_MISO,board::LCD_CS,
    board::LCD_DC,board::LCD_RESET,board::SD_CS,
    board::TOUCH_SDA,board::TOUCH_SCL,
    board::GNSS_RX,board::GNSS_TX,board::GNSS_PPS,
    board::LORA_SCK,board::LORA_MOSI,board::LORA_MISO,board::LORA_CS,
    board::LORA_IRQ,board::LORA_BUSY
  };
  for(int pin:floatingPins) {
    pinMode(pin,INPUT);
    gpio_set_pull_mode(static_cast<gpio_num_t>(pin),GPIO_FLOATING);
  }
  // Remove the FEM enable before its power and hold SX1262 reset low. VEXT and
  // GNSS are active-low power controls; the L76K backup supply remains present.
  outputLevel(board::FEM_ENABLE,LOW);
  outputLevel(board::FEM_POWER,LOW);
  for(const auto& output:sleepOutputs) {
    outputLevel(output.pin,output.level);
    gpio_hold_en(static_cast<gpio_num_t>(output.pin));
  }
  // S3 digital pads need both per-pad hold and this deep-sleep hold enable.
  gpio_deep_sleep_hold_en();
  esp_deep_sleep_start();
}
}
#endif
