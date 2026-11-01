#include "config.h"
#include "config_document.h"
#include <ArduinoJson.h>
#include <SD.h>
#include <strings.h>
#include <cmath>

namespace tunnel {
Config config;
Rule rules[64];
size_t ruleCount = 0;
namespace {
constexpr char path[] = "/lora-tunnel/config.json";
constexpr char backupPath[] = "/lora-tunnel/config.bak";
constexpr char tempPath[] = "/lora-tunnel/config.tmp";
// Fixed allocation deliberately uses ArduinoJson 6.21.5, not unbounded v7.
StaticJsonDocument<16384> document;
RadioSettings radioSettings;
bool read(const char* filename) {
  document.clear();
  File f = SD.open(filename, FILE_READ);
  if (!f || f.size() > 16384) { if (f) f.close(); return false; }
  auto error = deserializeJson(document, f, DeserializationOption::NestingLimit(6));
  f.close();
  return !error && parseConfigRadio(document.as<JsonVariantConst>(), radioSettings);
}
void copy(char* target, size_t size, JsonVariantConst v) {
  if (v.is<const char*>()) strlcpy(target, v.as<const char*>(), size);
}
uint32_t boundedSetting(JsonVariantConst value, uint32_t fallback, uint32_t minimum, uint32_t maximum) {
  if (!value.is<uint32_t>()) return fallback;
  const uint32_t parsed=value.as<uint32_t>();
  return parsed>=minimum && parsed<=maximum ? parsed : fallback;
}
}
bool loadConfig() {
  // Missing configuration uses confirmed US defaults. Corrupt files disable RX.
  config = Config{};
  const bool configPresent = SD.exists(path) || SD.exists(backupPath);
  const bool loaded = read(path) || read(backupPath);
  if (!loaded && configPresent) config.lora_enabled = false;
  if (loaded) {
    auto radio = document["lora"];
    config.lna = radio["lna"] | false;
    copy(config.profile, sizeof(config.profile), radio["name"]);
    // read() already validated the full radio settings before selecting this file.
    config.lora_enabled = radioSettings.enabled;
    config.frequency = static_cast<float>(radioSettings.frequency);
    config.bandwidth = static_cast<float>(radioSettings.bandwidth);
    config.sf = static_cast<uint8_t>(radioSettings.sf);
    config.cr = static_cast<uint8_t>(radioSettings.cr);
    config.preamble = static_cast<uint16_t>(radioSettings.preamble);
    const auto preferences=parseReconPreferences(document.as<JsonVariantConst>());
    config.buttons_enabled=preferences.buttons_enabled;
    config.buzzer=preferences.buzzer;
    config.recon_display_min=preferences.display_min;
    config.recon_alert_min=preferences.alert_min;
    config.recon_categories=preferences.categories;
    config.wifi_dwell_ms = boundedSetting(document["schedule"]["wifi_dwell_ms"],120,60,500);
    config.ble_window_ms = boundedSetting(document["schedule"]["ble_window_ms"],2500,500,10000);
    auto credentials = document["credentials"];
    copy(config.wigle_name, sizeof(config.wigle_name), credentials["wigle_api_name"]);
    copy(config.wigle_token, sizeof(config.wigle_token), credentials["wigle_api_token"]);
    copy(config.wdgwars_token, sizeof(config.wdgwars_token), credentials["wdgwars_token"]);
    copy(config.wifi_name, sizeof(config.wifi_name), credentials["wifi_ssid"]);
    copy(config.wifi_password, sizeof(config.wifi_password), credentials["wifi_password"]);
  }
  document.clear();
  File f = SD.open("/lora-tunnel/detection_rules.json", FILE_READ);
  ruleCount = 0;
  if (f && f.size() <= 16384 && !deserializeJson(document, f, DeserializationOption::NestingLimit(5)) &&
      document["schema"].is<uint32_t>() && document["schema"].as<uint32_t>() == 1 && document["rules"].is<JsonArrayConst>()) {
    for (JsonObjectConst entry : document["rules"].as<JsonArrayConst>()) {
      if (ruleCount >= 64) break;
      Rule r;
      if(parseReconRule(entry,r))rules[ruleCount++]=r;
    }
  }
  if (f) f.close();
  document.clear();
  return loaded;
}
bool saveCredential(int field, const char* value) {
  if (field < 0 || field > 4 || !value || strlen(value) > 127) return false;
  SD.mkdir("/lora-tunnel");
  const bool mainValid = read(path);
  if (!mainValid && !read(backupPath)) {
    document.clear(); document["schema"] = 1;
    document["lora"]["enabled"] = config.lora_enabled;
    document["lora"]["name"] = config.profile;
    document["lora"]["frequency_mhz"] = config.frequency;
    document["lora"]["bandwidth_khz"] = config.bandwidth;
    document["lora"]["spreading_factor"] = config.sf;
    document["lora"]["coding_rate_denominator"] = config.cr;
    document["lora"]["preamble_symbols"] = config.preamble;
    document["lora"]["lna"] = config.lna;
    document["ui"]["buttons_enabled"] = config.buttons_enabled;
    document["recon"]["buzzer"] = config.buzzer;
    document["recon"]["display_min"] = reconConfidenceKey(static_cast<recon::Confidence>(config.recon_display_min));
    document["recon"]["alert_min"] = reconConfidenceKey(static_cast<recon::Confidence>(config.recon_alert_min));
    JsonArray categories=document["recon"].createNestedArray("categories");
    for(unsigned category=1;category<=14;++category)
      if(config.recon_categories & (uint16_t(1u)<<category))categories.add(reconCategoryKey(static_cast<recon::Category>(category)));
    document["schedule"]["wifi_dwell_ms"] = config.wifi_dwell_ms;
    document["schedule"]["ble_window_ms"] = config.ble_window_ms;
  }
  const char* keys[] = {"wigle_api_name","wigle_api_token","wdgwars_token","wifi_ssid","wifi_password"};
  document["credentials"][keys[field]] = value;
  const auto fail = []() {
    document.clear();
    // This path is only a failed candidate; the original main/backup survives.
    if (SD.exists(tempPath)) SD.remove(tempPath);
    return false;
  };
  if (document.overflowed()) return fail();
  const size_t expected = measureJson(document);
  if (expected > 16384) return fail();
  File f = SD.open(tempPath, FILE_WRITE);
  if (!f) return fail();
  const bool ok = serializeJson(document, f) == expected;
  f.flush(); const bool writeError = f.getWriteError(); f.close();
  if (!ok || writeError || !read(tempPath)) return fail();
  const char* saved = document["credentials"][keys[field]];
  if (!saved || strcmp(saved, value)) return fail();
  // Rotate only a validated original main. If recovery used the backup, keep
  // that backup intact while replacing the invalid main with the tested temp.
  if (mainValid) {
    if (SD.exists(backupPath) && !SD.remove(backupPath)) return fail();
    if (!SD.rename(path, backupPath)) return fail();
  } else if (SD.exists(path) && !SD.remove(path)) {
    return fail();
  }
  if (!SD.rename(tempPath, path)) return fail();
  char* fields[] = {config.wigle_name,config.wigle_token,config.wdgwars_token,config.wifi_name,config.wifi_password};
  strlcpy(fields[field],value,128);
  document.clear();
  return true;
}
const Rule* matchRule(bool ble, const char* address, uint8_t addressType, const char* name) {
  for (size_t i=0;i<ruleCount;++i) {
    const Rule& r=rules[i];
    if(reconRuleMatches(r,ble,address,addressType,name,nullptr,0))return &r;
  }
  return nullptr;
}
RuleApplication applyReconRules(recon::Result& result,bool ble,const char* address,
                               uint8_t addressType,const char* name,const uint8_t* ad,
                               size_t length,size_t advLength) {
  return applyReconRuleSet(result,rules,ruleCount,ble,address,addressType,name,ad,length,advLength);
}
}
