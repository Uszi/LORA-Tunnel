#pragma once
#include <Arduino.h>
#include "recon_rules.h"

namespace tunnel {
struct Config {
  // This build targets the user's confirmed United States / 915 MHz kit.
  bool lora_enabled = true, lna = false, buzzer = true;
  bool buttons_enabled = true;
  uint8_t recon_display_min = 1, recon_alert_min = 2;
  uint16_t recon_categories = kAllReconCategories;
  float frequency = 906.875f, bandwidth = 250.0f;
  uint8_t sf = 11, cr = 5;
  uint16_t preamble = 16;
  uint32_t wifi_dwell_ms = 120, ble_window_ms = 2500;
  char profile[40] = "US LongFast";
  char wifi_name[128]{}, wifi_password[128]{};
  char wigle_name[128]{}, wigle_token[128]{}, wdgwars_token[128]{};
};
extern Config config;
extern Rule rules[64];
extern size_t ruleCount;
bool loadConfig(); // Caller owns SPI mutex, SD already mounted.
bool saveCredential(int field, const char* value);
const Rule* matchRule(bool ble, const char* address, uint8_t addressType, const char* name);
RuleApplication applyReconRules(recon::Result& result, bool ble, const char* address,
                               uint8_t addressType, const char* name,
                               const uint8_t* ad, size_t length, size_t advLength = 0);
}
