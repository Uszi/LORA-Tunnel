#pragma once

#include <ArduinoJson.h>
#include <cstdint>
#include <cstring>
#include "config_validation.h"

namespace tunnel {

struct RadioSettings {
  bool enabled = false;
  double frequency = 906.875, bandwidth = 250;
  std::int64_t sf = 11, cr = 5, preamble = 16;
};

// A parseable schema marker alone is insufficient for primary/backup rotation.
// Every config read must also contain a usable radio section, even when disabled.
inline bool parseConfigRadio(JsonVariantConst root, RadioSettings& result) {
  if (!root.is<JsonObjectConst>() || !root["schema"].is<std::uint32_t>() ||
      root["schema"].as<std::uint32_t>() != 1 || !root["lora"].is<JsonObjectConst>()) return false;
  const JsonObjectConst radio = root["lora"];
  if (!radio["enabled"].is<bool>()) return false;
  if (radio.containsKey("lna") && !radio["lna"].is<bool>()) return false;
  if (radio.containsKey("name") &&
      (!radio["name"].is<const char*>() || std::strlen(radio["name"].as<const char*>()) > 39)) return false;

  bool valid = true;
  const auto number = [&](const char* key, double fallback) {
    if (!radio.containsKey(key)) return fallback;
    if (!radio[key].is<double>()) { valid = false; return fallback; }
    return radio[key].as<double>();
  };
  const auto integer = [&](const char* key, std::int64_t fallback) {
    if (!radio.containsKey(key)) return fallback;
    if (!radio[key].is<std::int64_t>()) { valid = false; return fallback; }
    return radio[key].as<std::int64_t>();
  };
  RadioSettings parsed;
  parsed.enabled = radio["enabled"].as<bool>();
  parsed.frequency = number("frequency_mhz", parsed.frequency);
  parsed.bandwidth = number("bandwidth_khz", parsed.bandwidth);
  parsed.sf = integer("spreading_factor", parsed.sf);
  parsed.cr = integer("coding_rate_denominator", parsed.cr);
  parsed.preamble = integer("preamble_symbols", parsed.preamble);
  if (!valid || !validLoRaSettings(parsed.frequency, parsed.bandwidth, parsed.sf, parsed.cr, parsed.preamble)) return false;
  result = parsed;
  return true;
}

}  // namespace tunnel
