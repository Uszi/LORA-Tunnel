#pragma once
#include <Arduino.h>
#include "recon_rules.h"
#include "lora_profiles.h"

namespace tunnel {
struct Config {
  // Fresh installations require an explicit region selection before LoRa RX.
  bool lora_enabled = false, lna = false, buzzer = true;
  bool buttons_enabled = true;
  bool auto_wigle = false, auto_wardrift = false, auto_wdgwars = false;
  uint8_t recon_display_min = 1, recon_alert_min = 2;
  uint16_t recon_categories = kAllReconCategories;
  float frequency = 0.0f, bandwidth = 250.0f;
  uint8_t sf = 11, cr = 5;
  uint16_t preamble = 16;
  uint32_t wifi_dwell_ms = 120, ble_window_ms = 2500;
  char profile[40] = "Choose region", region[12] = "UNSET";
  char wifi_name[128]{}, wifi_password[128]{};
  char wigle_name[128]{}, wigle_token[128]{}, wdgwars_token[128]{}, wardrift_token[128]{};
};
extern Config config;
// Exact-sized validated custom rule set (at most 64); null for an empty set.
extern Rule* rules;
extern bool configAllocationFailed, rulesLoadFailed;
extern size_t ruleCount;
bool loadConfig(); // Caller owns SPI mutex, SD already mounted.
enum class ConfigLoadState : uint8_t { Missing, Primary, Backup, Invalid, Nvs };
extern ConfigLoadState configLoadState;
bool regionConfigured();
// A saved explicit Skip counts as completed setup while regionConfigured stays false.
bool regionSelectionMade();
bool regionHardwareCompatible();
// Used when SD is unavailable. Never overrides a loaded or invalid SD config.
bool loadRegionPreference();
struct RegionSaveResult { bool applied=false, nvs_saved=false, sd_saved=false; };
// Idle only, caller owns SPI mutex. SD first when mounted; NVS contains code only.
RegionSaveResult saveLoRaRegion(const char* code, bool sdMounted);
bool saveCredential(int field, const char* value);
// Service order: WiGLE, Wardrift, WDGWars. Caller owns SPI, capture/upload idle.
bool saveAutoUpload(int service, bool enabled);
const Rule* matchRule(bool ble, const char* address, uint8_t addressType, const char* name);
RuleApplication applyReconRules(recon::Result& result, bool ble, const char* address,
                               uint8_t addressType, const char* name,
                               const uint8_t* ad, size_t length, size_t advLength = 0);
}
