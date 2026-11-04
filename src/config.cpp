#include "config.h"
#include "config_document.h"
#include "credential_validation.h"
#include <ArduinoJson.h>
#include <SD.h>
#include <Preferences.h>
#include <strings.h>
#include <cmath>
#include <new>
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
#include "ui.h"
#include "checked_sd_file.h"
#endif

namespace tunnel {
Config config;
ConfigLoadState configLoadState=ConfigLoadState::Missing;
Rule* rules=nullptr;
size_t ruleCount = 0;
bool configAllocationFailed=false;
bool rulesLoadFailed=false;
namespace {
constexpr char path[] = "/lora-tunnel/config.json";
constexpr char backupPath[] = "/lora-tunnel/config.bak";
constexpr char tempPath[] = "/lora-tunnel/config.tmp";
// Keep the full 16 KiB JSON capacity and file limit. Cardputer borrows existing
// pixel memory exclusively while stopped; Heltec retains scoped heap scratch.
// No JsonVariant escapes, and no callback loads or saves configuration.
constexpr size_t documentCapacity=16384;
RadioSettings radioSettings;
bool selectionMade=false;
bool hardwareCompatible=true;
bool sourceOpenFailed=false;
constexpr char preferenceNamespace[]="lt-region";
constexpr const char* credentialKeys[]={"wigle_api_name","wigle_api_token","wdgwars_token","wifi_ssid","wifi_password","wardrift_api_token"};
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
constexpr size_t fileBufferBytes=512;
struct ConfigScratchLease {
  void* memory=ui::acquireConfigScratch(documentCapacity+fileBufferBytes);
  bool allocated=false,everAllocated=false;
  ConfigScratchLease()=default;
  ~ConfigScratchLease() { ui::releaseConfigScratch(memory); }
  ConfigScratchLease(const ConfigScratchLease&)=delete;
  ConfigScratchLease& operator=(const ConfigScratchLease&)=delete;
};
struct ConfigAllocator {
  ConfigScratchLease* lease=nullptr;
  explicit ConfigAllocator(ConfigScratchLease* value=nullptr):lease(value) {}
  void* allocate(size_t bytes) {
    if(!lease || !lease->memory || lease->everAllocated || bytes>documentCapacity)return nullptr;
    lease->allocated=lease->everAllocated=true;return lease->memory;
  }
  void deallocate(void* memory) {
    if(lease && memory==lease->memory)lease->allocated=false;
  }
  void* reallocate(void*,size_t) { return nullptr; } // No copying/growth/garbage collection.
};
struct ScopedConfigDocument {
  // Reverse destruction order is essential: destroy JSON before wiping pixels.
  ConfigScratchLease lease;
  BasicJsonDocument<ConfigAllocator> document;
  ScopedConfigDocument():document(documentCapacity,ConfigAllocator(&lease)) {}
  ScopedConfigDocument(const ScopedConfigDocument&)=delete;
  ScopedConfigDocument& operator=(const ScopedConfigDocument&)=delete;
  void* fileBuffer() { return lease.memory?static_cast<uint8_t*>(lease.memory)+documentCapacity:nullptr; }
};
using ConfigFile=storage::CheckedFile;
bool exists(const char* filename) {
  const auto state=storage::status(filename);
  if(state==storage::PathStatus::Error)sourceOpenFailed=true;
  return state!=storage::PathStatus::Missing;
}
bool removeFile(const char* filename) { return storage::remove(filename); }
bool renameFile(const char* from,const char* to) { return storage::rename(from,to); }
bool makeDirectory() { return storage::mkdir("/lora-tunnel"); }
bool openFile(ConfigFile& file,const char* filename,bool write,void* buffer) {
  const bool opened=file.open(filename,write?storage::OpenMode::Truncate:storage::OpenMode::Read,buffer,fileBufferBytes);
  if(!opened && file.failure()==storage::Failure::Buffer)configAllocationFailed=true;
  return opened;
}
bool closeFile(ConfigFile& file) { return file.close(); }
#else
struct ScopedConfigDocument {
  DynamicJsonDocument document{documentCapacity};
  void* fileBuffer() { return nullptr; }
};
using ConfigFile=File;
bool exists(const char* filename) { return SD.exists(filename); }
bool removeFile(const char* filename) { return SD.remove(filename); }
bool renameFile(const char* from,const char* to) { return SD.rename(from,to); }
bool makeDirectory() { SD.mkdir("/lora-tunnel");return true; }
bool openFile(ConfigFile& file,const char* filename,bool write,void*) {
  file=SD.open(filename,write?FILE_WRITE:FILE_READ);return bool(file);
}
bool closeFile(ConfigFile& file) { const bool ok=!file.getWriteError();file.close();return ok; }
#endif
bool read(JsonDocument& document,const char* filename,void* buffer) {
  document.clear();
  const bool present=exists(filename);
  if(sourceOpenFailed)return false;
  ConfigFile f;
  if(!openFile(f,filename,false,buffer)) { sourceOpenFailed=sourceOpenFailed || present;return false; }
  if(f.size()>documentCapacity) {if(!closeFile(f))sourceOpenFailed=true;return false;}
  const auto error=deserializeJson(document,f,DeserializationOption::NestingLimit(6));
  if(!closeFile(f)) { sourceOpenFailed=true;return false; }
  if(error==DeserializationError::NoMemory) {configAllocationFailed=true;return false;}
  return !error && parseConfigRadio(document.as<JsonVariantConst>(),radioSettings);
}
void copy(char* target, size_t size, JsonVariantConst v) {
  if (v.is<const char*>()) strlcpy(target, v.as<const char*>(), size);
}
bool copyCredential(char* target, int field, JsonVariantConst value) {
  // Never turn malformed imported credentials into a different, truncated key
  // or SSID. Keep the source document intact so it can be corrected on SD.
  target[0]=0;
  if(!value.is<const char*>()) return false;
  const JsonString text=value.as<JsonString>();
  if(text.size()!=strlen(text.c_str()) || text.size()>=128 ||
      !validCredential(field,text.c_str())) return false;
  memcpy(target,text.c_str(),text.size()+1);
  return true;
}
uint32_t boundedSetting(JsonVariantConst value, uint32_t fallback, uint32_t minimum, uint32_t maximum) {
  if (!value.is<uint32_t>()) return fallback;
  const uint32_t parsed=value.as<uint32_t>();
  return parsed>=minimum && parsed<=maximum ? parsed : fallback;
}
void applyRegion(const char* code) {
  const auto* profile=lora_profiles::find(code);
  strlcpy(config.region,code,sizeof(config.region));
  selectionMade=true; hardwareCompatible=true;
  config.lora_enabled=profile!=nullptr;
  config.lna=false;
  config.frequency=profile ? profile->frequency_mhz : 0;
  config.bandwidth=profile ? profile->bandwidth_khz : 250;
  config.sf=profile ? profile->sf : 11;
  config.cr=profile ? profile->cr : 5;
  config.preamble=profile ? profile->preamble : 16;
  if(profile) snprintf(config.profile,sizeof(config.profile),"%s LongFast",code);
  else strlcpy(config.profile,"Choose region",sizeof(config.profile));
}
bool storeRegionPreference(const char* code) {
  Preferences prefs;
  if(!prefs.begin(preferenceNamespace,false)) return false;
  char existing[12]{};
  const bool unchanged=prefs.getString("code",existing,sizeof(existing)) && !strcmp(existing,code);
  bool ok=unchanged || prefs.putString("code",code)==strlen(code);
  char verify[12]{};
  ok=ok && prefs.getString("code",verify,sizeof(verify)) && !strcmp(verify,code);
  prefs.end();
  return ok;
}
void seedDocument(JsonDocument& document) {
  document.clear(); document["schema"]=1;
  document["lora"]["region"]=config.region;
  document["lora"]["selection_complete"]=selectionMade;
  document["lora"]["enabled"]=config.lora_enabled;
  document["lora"]["name"]=config.profile;
  document["lora"]["frequency_mhz"]=config.frequency;
  document["lora"]["bandwidth_khz"]=config.bandwidth;
  document["lora"]["spreading_factor"]=config.sf;
  document["lora"]["coding_rate_denominator"]=config.cr;
  document["lora"]["preamble_symbols"]=config.preamble;
  document["lora"]["lna"]=config.lna;
  document["ui"]["buttons_enabled"]=config.buttons_enabled;
  document["recon"]["buzzer"]=config.buzzer;
  document["recon"]["display_min"]=reconConfidenceKey(static_cast<recon::Confidence>(config.recon_display_min));
  document["recon"]["alert_min"]=reconConfidenceKey(static_cast<recon::Confidence>(config.recon_alert_min));
  JsonArray categories=document["recon"].createNestedArray("categories");
  for(unsigned category=1;category<=14;++category)
    if(config.recon_categories & (uint16_t(1u)<<category))categories.add(reconCategoryKey(static_cast<recon::Category>(category)));
  document["uploads"]["wigle"]=config.auto_wigle;
  document["uploads"]["wardrift"]=config.auto_wardrift;
  document["uploads"]["wdgwars"]=config.auto_wdgwars;
  document["schedule"]["wifi_dwell_ms"]=config.wifi_dwell_ms;
  document["schedule"]["ble_window_ms"]=config.ble_window_ms;
  const char* values[]={config.wigle_name,config.wigle_token,config.wdgwars_token,config.wifi_name,config.wifi_password,config.wardrift_token};
  for(size_t i=0;i<6;++i) document["credentials"][credentialKeys[i]]=values[i];
}
bool prepareSave(JsonDocument& document,void* buffer) {
  if(!makeDirectory()) {sourceOpenFailed=true;return false;}
  const bool mainValid=read(document,path,buffer);
  if(sourceOpenFailed || configAllocationFailed) return false;
  if(!mainValid && !read(document,backupPath,buffer)) {
    if(sourceOpenFailed || configAllocationFailed) return false;
    seedDocument(document);
  }
  return mainValid;
}
bool commitPrepared(JsonDocument& document,bool mainValid,const char* key,const char* value,const char* region,
                    const char* upload=nullptr,bool uploadEnabled=false,void* buffer=nullptr) {
  const auto fail=[&document]() {
    document.clear();
    if(exists(tempPath))removeFile(tempPath);
    return false;
  };
  RadioSettings candidate;
  if(sourceOpenFailed || configAllocationFailed || document.overflowed() || !parseConfigRadio(document.as<JsonVariantConst>(),candidate)) return fail();
  const size_t expected=measureJson(document);
  if(expected>16384) return fail();
  ConfigFile f;
  if(!openFile(f,tempPath,true,buffer))return fail();
  const bool ok=serializeJson(document,f)==expected;
  // Card close verifies fflush, fsync and fclose once; retain Heltec behavior.
#if !defined(LORA_TUNNEL_CARDPUTER_ADV)
  f.flush();
#endif
  const bool closed=closeFile(f);
  if(!ok || !closed || !read(document,tempPath,buffer))return fail();
  if(key) {
    const char* saved=document["credentials"][key];
    if(!saved || strcmp(saved,value)) return fail();
  }
  if(region && (strcmp(radioSettings.region,region) || !radioSettings.selection_complete)) return fail();
  if(upload && (!document["uploads"][upload].is<bool>() ||
      document["uploads"][upload].as<bool>()!=uploadEnabled)) return fail();
  // Rotate only a semantically valid original. Recovery never destroys the
  // usable backup merely because the primary was parseable but invalid.
  if(mainValid) {
    if(exists(backupPath) && (sourceOpenFailed || !removeFile(backupPath)))return fail();
    if(!renameFile(path,backupPath))return fail();
  } else if(exists(path) && (sourceOpenFailed || !removeFile(path)))return fail();
  if(!renameFile(tempPath,path))return fail();
  document.clear();
  return true;
}
}
bool regionConfigured() {
  const auto* profile=lora_profiles::find(config.region);
  return profile && lora_profiles::selectable(*profile);
}
bool regionSelectionMade() { return selectionMade; }
bool regionHardwareCompatible() { return hardwareCompatible; }
bool loadRegionPreference() {
  if(configLoadState==ConfigLoadState::Primary || configLoadState==ConfigLoadState::Backup ||
     configLoadState==ConfigLoadState::Invalid) return false;
  Preferences prefs;
  if(!prefs.begin(preferenceNamespace,true)) return false;
  char code[12]{};
  const bool found=prefs.getString("code",code,sizeof(code))>0;
  prefs.end();
  const auto* profile=lora_profiles::find(code);
  if(!found) return false;
  if(!lora_profiles::isUnset(code) && (!profile || !lora_profiles::selectable(*profile))) {
    hardwareCompatible=false;
    return false;
  }
  applyRegion(code);
  configLoadState=ConfigLoadState::Nvs;
  return true;
}
bool loadConfig() {
  // An explicit usable SD file wins. Only absent files may use the saved NVS region.
  config = Config{};
  selectionMade=false; hardwareCompatible=true;
  configLoadState=ConfigLoadState::Missing;
  configAllocationFailed=false; sourceOpenFailed=false; rulesLoadFailed=false;
  ScopedConfigDocument operation;
  auto& document=operation.document;
  if(document.capacity()<documentCapacity) {
    configAllocationFailed=true; configLoadState=ConfigLoadState::Invalid;
    return false;
  }
  const bool configPresent=exists(path) || exists(backupPath);
  const bool primary=read(document,path,operation.fileBuffer());
  if(sourceOpenFailed || configAllocationFailed) { configLoadState=ConfigLoadState::Invalid; return false; }
  const bool loaded=primary || read(document,backupPath,operation.fileBuffer());
  if(sourceOpenFailed || configAllocationFailed) { configLoadState=ConfigLoadState::Invalid; return false; }
  if(!loaded && configPresent) configLoadState=ConfigLoadState::Invalid;
  else if(!loaded) loadRegionPreference();
  if (loaded) {
    configLoadState=primary ? ConfigLoadState::Primary : ConfigLoadState::Backup;
    selectionMade=radioSettings.selection_complete;
    hardwareCompatible=radioSettings.hardware_compatible;
    auto radio = document["lora"];
    config.lna = radio["lna"] | false;
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
    config.lna=false; // U214 has no selectable Heltec FEM/LNA.
#endif
    strlcpy(config.region,radioSettings.region,sizeof(config.region));
    if(regionConfigured()) snprintf(config.profile,sizeof(config.profile),"%s LongFast",config.region);
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
    char* credentialFields[]={config.wigle_name,config.wigle_token,config.wdgwars_token,
                             config.wifi_name,config.wifi_password,config.wardrift_token};
    for(int field=0;field<6;++field) {
      const bool valid=copyCredential(credentialFields[field],field,credentials[credentialKeys[field]]);
      // An explicitly invalid imported password must not become an open-network
      // connection attempt. An absent password retains legacy open-Wi-Fi support.
      if(field==4 && !valid && credentials.containsKey(credentialKeys[field])) config.wifi_name[0]=0;
    }
    config.auto_wigle=document["uploads"]["wigle"].is<bool>() && document["uploads"]["wigle"].as<bool>();
    config.auto_wardrift=document["uploads"]["wardrift"].is<bool>() && document["uploads"]["wardrift"].as<bool>();
    config.auto_wdgwars=document["uploads"]["wdgwars"].is<bool>() && document["uploads"]["wdgwars"].as<bool>();
  }
  document.clear();
  constexpr char rulePath[]="/lora-tunnel/detection_rules.json";
  const bool present=exists(rulePath);
  ConfigFile f;
  const bool opened=!sourceOpenFailed && openFile(f,rulePath,false,operation.fileBuffer());
  // A transient open failure must not replace an existing rule set with empty.
  if(sourceOpenFailed || (present && !opened)) {rulesLoadFailed=true;return false;}
  bool valid=!present;
  if(present && f.size()<=documentCapacity) {
    const auto error=deserializeJson(document,f,DeserializationOption::NestingLimit(5));
    if(error==DeserializationError::NoMemory)configAllocationFailed=true;
    valid=!error && document["schema"].is<uint32_t>() &&
        document["schema"].as<uint32_t>()==1 && document["rules"].is<JsonArrayConst>();
  }
  const bool closed=opened?closeFile(f):!present;
  if(!valid || !closed) { rulesLoadFailed=true; return false; }
  size_t candidateCount=0;
  if(present) for(JsonObjectConst entry:document["rules"].as<JsonArrayConst>()) {
    Rule candidate;
    if(parseReconRule(entry,candidate) && ++candidateCount==64) break;
  }
  Rule* candidateRules=candidateCount?new(std::nothrow) Rule[candidateCount]:nullptr;
  if(candidateCount && !candidateRules) {
    configAllocationFailed=true; rulesLoadFailed=true; return false;
  }
  size_t copied=0;
  if(candidateCount) for(JsonObjectConst entry:document["rules"].as<JsonArrayConst>()) {
    Rule candidate;
    if(parseReconRule(entry,candidate)) {
      candidateRules[copied++]=candidate;
      if(copied==candidateCount) break;
    }
  }
  // Replace only a complete validated set. Missing/empty files need no slab;
  // an invalid file or failed allocation leaves the previous valid set intact.
  Rule* previous=rules; rules=candidateRules; ruleCount=copied; delete[] previous;
  return loaded;
}
bool saveCredential(int field, const char* value) {
  configAllocationFailed=false; sourceOpenFailed=false;
  if (!validCredential(field,value)) return false;
  ScopedConfigDocument operation;
  auto& document=operation.document;
  if(document.capacity()<documentCapacity) { configAllocationFailed=true; return false; }
  const bool mainValid=prepareSave(document,operation.fileBuffer());
  document["credentials"][credentialKeys[field]]=value;
  if(!commitPrepared(document,mainValid,credentialKeys[field],value,nullptr,nullptr,false,operation.fileBuffer())) return false;
  char* fields[] = {config.wigle_name,config.wigle_token,config.wdgwars_token,config.wifi_name,config.wifi_password,config.wardrift_token};
  strlcpy(fields[field],value,128);
  configLoadState=ConfigLoadState::Primary;
  return true;
}
bool saveAutoUpload(int service,bool enabled) {
  const char* names[]={"wigle","wardrift","wdgwars"};
  configAllocationFailed=false; sourceOpenFailed=false;
  if(service<0 || service>2) return false;
  ScopedConfigDocument operation;
  auto& document=operation.document;
  if(document.capacity()<documentCapacity) { configAllocationFailed=true; return false; }
  const bool mainValid=prepareSave(document,operation.fileBuffer());
  if(!setConfigDocumentUpload(document.as<JsonVariant>(),service,enabled)) { document.clear(); return false; }
  if(!commitPrepared(document,mainValid,nullptr,nullptr,nullptr,names[service],enabled,operation.fileBuffer())) return false;
  bool* values[]={&config.auto_wigle,&config.auto_wardrift,&config.auto_wdgwars};
  *values[service]=enabled;
  return true;
}
RegionSaveResult saveLoRaRegion(const char* code,bool sdMounted) {
  configAllocationFailed=false; sourceOpenFailed=false;
  RegionSaveResult result;
  const auto* profile=lora_profiles::find(code);
  if(!lora_profiles::isUnset(code) && (!profile || !lora_profiles::selectable(*profile))) return result;
  if(sdMounted) {
    ScopedConfigDocument operation;
    auto& document=operation.document;
    if(document.capacity()<documentCapacity) { configAllocationFailed=true; return result; }
    const bool mainValid=prepareSave(document,operation.fileBuffer());
    if(!setConfigDocumentRegion(document.as<JsonVariant>(),code) ||
       !commitPrepared(document,mainValid,nullptr,nullptr,code,nullptr,false,operation.fileBuffer())) { document.clear(); return result; }
    result.sd_saved=true;
  }
  result.nvs_saved=storeRegionPreference(code);
  if(!result.sd_saved && !result.nvs_saved) return result;
  applyRegion(code);
  configLoadState=result.sd_saved ? ConfigLoadState::Primary : ConfigLoadState::Nvs;
  result.applied=true;
  return result;
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
