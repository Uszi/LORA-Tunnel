#pragma once

#include <ArduinoJson.h>
#include <cstdint>
#include <cstring>
#include "config_validation.h"
#include "lora_profiles.h"
#include "lora_scan.h"
#include "mesh_config.h"

namespace tunnel {

struct RadioSettings {
  bool enabled = false;
  bool selection_complete = false;
  bool hardware_compatible = true;
  double frequency = 0, bandwidth = 250;
  std::int64_t sf = 11, cr = 5, preamble = 16;
  char region[12] = "UNSET";
  uint8_t mode=0;
  uint32_t dwell_ms=30000;
  char meshcore_profile[32]="AUTO";
};

// A parseable schema marker alone is insufficient for primary/backup rotation.
// Every config read must also contain a usable radio section, even when disabled.
inline bool parseConfigRadio(JsonVariantConst root, RadioSettings& result) {
  if (!root.is<JsonObjectConst>() || !root["schema"].is<std::uint32_t>() ||
      root["schema"].as<std::uint32_t>() != 1 || !root["lora"].is<JsonObjectConst>()) return false;
  const JsonObjectConst radio = root["lora"];
  if (!radio["enabled"].is<bool>()) return false;
  if(radio.containsKey("selection_complete") && !radio["selection_complete"].is<bool>()) return false;
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
  if(radio.containsKey("mode")) {
    if(!radio["mode"].is<const char*>())return false;
    const char* mode=radio["mode"];
    if(strlen(mode)!=radio["mode"].as<JsonString>().size())return false;
    if(!strcmp(mode,"meshcore"))parsed.mode=1;
    else if(!strcmp(mode,"combined"))parsed.mode=2;
    else if(strcmp(mode,"meshtastic"))return false;
  }
  if(radio.containsKey("dwell_ms")) {
    if(!radio["dwell_ms"].is<uint32_t>())return false;
    parsed.dwell_ms=radio["dwell_ms"];
    if(parsed.dwell_ms<30000 || parsed.dwell_ms>600000)return false;
  }
  if(radio.containsKey("meshcore_profile")) {
    if(!radio["meshcore_profile"].is<const char*>())return false;
    const JsonString name=radio["meshcore_profile"].as<JsonString>();
    if(!name.size() || name.size()>=sizeof(parsed.meshcore_profile) || strlen(name.c_str())!=name.size())return false;
    strcpy(parsed.meshcore_profile,name.c_str());
  }
  uint8_t keyCount=0; bool publicDefault=true;
  if(!parseMeshKeys(radio,nullptr,keyCount,publicDefault))return false;
  parsed.enabled = radio["enabled"].as<bool>();
  const bool legacy=!radio.containsKey("region");
  const lora_profiles::Profile* profile=nullptr;
  if(!legacy) {
    if(!radio["region"].is<const char*>()) return false;
    const char* code=radio["region"];
    if(std::strlen(code)>=sizeof(parsed.region) || strlen(code)!=radio["region"].as<JsonString>().size()) return false;
    profile=lora_profiles::find(code);
    if(!profile && !lora_profiles::isUnset(code)) return false;
    std::strcpy(parsed.region,code);
  } else if(parsed.enabled || radio.containsKey("frequency_mhz")) {
    // Schema 1 before region selection accepted only 902-928 MHz. Keep those
    // installed configurations, including custom settings, without retuning.
    profile=lora_profiles::find("US");
    std::strcpy(parsed.region,"US");
  }
  if(profile) {
    parsed.frequency=profile->frequency_mhz;
    parsed.bandwidth=profile->bandwidth_khz;
    parsed.sf=profile->sf; parsed.cr=profile->cr; parsed.preamble=profile->preamble;
  }
  parsed.frequency = number("frequency_mhz", parsed.frequency);
  parsed.bandwidth = number("bandwidth_khz", parsed.bandwidth);
  parsed.sf = integer("spreading_factor", parsed.sf);
  parsed.cr = integer("coding_rate_denominator", parsed.cr);
  parsed.preamble = integer("preamble_symbols", parsed.preamble);
  // Validate modem fields before any narrowing. An unset/disabled radio uses
  // frequency zero, never a hidden US fallback.
  if(!valid || !validLoRaSettings(906.875,parsed.bandwidth,parsed.sf,parsed.cr,parsed.preamble) ||
     !std::isfinite(parsed.frequency)) return false;
  if(!profile) {
    if(parsed.enabled || parsed.frequency!=0) return false;
  } else if(legacy) {
    if(parsed.frequency<902 || parsed.frequency>928) return false;
  } else {
    if(parsed.enabled && !profile->high_band_compatible) return false;
    const double halfWidth=parsed.bandwidth/2000.0;
    if(parsed.frequency-halfWidth<profile->minimum_mhz-0.0001 ||
       parsed.frequency+halfWidth>profile->maximum_mhz+0.0001) return false;
  }
  // Named supported profiles (including legacy US) already represent a choice.
  // UNSET needs an explicit marker to distinguish Skip from a blank example.
  parsed.selection_complete=profile ? lora_profiles::selectable(*profile) :
      (radio["selection_complete"] | false);
  if(parsed.enabled && parsed.mode && !lora_scan::findMeshCoreProfile(parsed.region,parsed.meshcore_profile))return false;
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
  // A valid Heltec SD config is not corrupt merely because this cap cannot
  // receive its frequency. Preserve its credentials/other settings and report
  // the incompatibility rather than selecting a different backup/NVS region.
  parsed.hardware_compatible=!profile || lora_profiles::fitsHardware(
      parsed.frequency,parsed.bandwidth,lora_profiles::hardwareMinimum,lora_profiles::hardwareMaximum);
  if(parsed.mode && profile && !lora_scan::meshCoreProfile(parsed.region,parsed.meshcore_profile))parsed.hardware_compatible=false;
  if(!parsed.hardware_compatible) { parsed.enabled=false; parsed.selection_complete=false; }
#endif
  result = parsed;
  return true;
}

// Update only the radio keys; credentials, scheduling and unknown future fields
// survive a region change. Invalid/unavailable selections leave the document alone.
inline bool setConfigDocumentRegion(JsonVariant root,const char* code) {
  const auto* profile=lora_profiles::find(code);
  if(!root.is<JsonObject>() || (!lora_profiles::isUnset(code) &&
     (!profile || !lora_profiles::selectable(*profile)))) return false;
  root["schema"]=1;
  auto radio=root["lora"];
  radio["region"]=code;
  radio["selection_complete"]=true;
  radio["enabled"]=profile!=nullptr;
  char name[40]="Choose region";
  if(profile) {
    std::strcpy(name,profile->code);
    std::strcat(name," LongFast");
  }
  radio["name"]=name;
  radio["frequency_mhz"]=profile ? profile->frequency_mhz : 0;
  radio["bandwidth_khz"]=profile ? profile->bandwidth_khz : 250;
  radio["spreading_factor"]=profile ? profile->sf : 11;
  radio["coding_rate_denominator"]=profile ? profile->cr : 5;
  radio["preamble_symbols"]=profile ? profile->preamble : 16;
  radio["lna"]=false;
  // A region change must not leave a previous region's MeshCore frequency active.
  radio["meshcore_profile"]="AUTO";
  if(!lora_scan::meshCoreProfile(code,"AUTO"))radio["mode"]="meshtastic";
  return true;
}

inline bool setConfigDocumentUpload(JsonVariant root,int service,bool enabled) {
  const char* names[]={"wigle","wardrift","wdgwars"};
  if(!root.is<JsonObject>() || service<0 || service>2) return false;
  auto uploads=root["uploads"];
  if(!uploads.isNull() && !uploads.is<JsonObject>()) return false;
  uploads[names[service]]=enabled;
  return uploads[names[service]].is<bool>() && uploads[names[service]].as<bool>()==enabled;
}

}  // namespace tunnel
