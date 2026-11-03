#include <Arduino.h>
#include <cmath>
#include <iostream>
#include <algorithm>
using QueueHandle_t=int;using SemaphoreHandle_t=int;
#include "app_types.h"
#include "mesh_header.h"
#include "ble_ad_summary.h"
#include "version.h"
#include <ArduinoJson.h>
namespace board {constexpr char NAME[]="Cardputer ADV + LoRa-1262 U214";}
namespace tunnel {
namespace recon {const char* categoryName(Category){return "CUSTOM";}const char* confidenceName(Confidence){return "HIGH";}}
Counters counters;
struct ConfigForTest {float frequency=906.875,bandwidth=250;unsigned sf=11,cr=5,preamble=16;bool lna=false;char profile[40]{};} config;
// A 64-bit CI host needs larger bookkeeping slots. Its measured string memory
// and slot count are normalized below to the proven ESP32 16-byte slot size.
StaticJsonDocument<sizeof(void*)==4?4096:8192> record;
char buffer[8192];size_t used=0;unsigned sequence=0,bufferedRecords=0;bool sessionFile=true;
void flush(){used=0;}
// LOGGER_IMPLEMENTATION
}
size_t slots(JsonVariantConst value){size_t total=0;if(value.is<JsonObjectConst>())for(JsonPairConst pair:value.as<JsonObjectConst>())total+=1+slots(pair.value());else if(value.is<JsonArrayConst>())for(JsonVariantConst child:value.as<JsonArrayConst>())total+=1+slots(child);return total;}
int main(){using namespace tunnel;Observation event;event.session=0xffffffff;event.mono_ms=0xffffffffffffffffULL;event.mode=CaptureMode::Recon;
std::memset(event.name,'"',sizeof(event.name)-1);std::memset(event.security,'\\',sizeof(event.security)-1);std::strcpy(event.address,"AA:BB:CC:DD:EE:FF");std::memset(event.ssid_raw,0xff,32);event.ssid_length=32;event.channel=14;event.rssi=-127;event.original_length=65535;event.gps.valid=true;event.gps.age_ms=10000;event.gps.utc_age_ms=1500;event.gps.lat=-90;event.gps.lon=-180;event.gps.altitude=100000;event.gps.hdop=999;event.gps.satellites=65535;std::strcpy(event.gps.utc,"2026-11-03T23:59:59Z");
auto& d=event.detection;d.category=recon::Category::Custom;d.confidence=recon::Confidence::High;d.payload_valid=true;std::memset(d.label,'"',sizeof(d.label)-1);std::memset(d.vendor,'\\',sizeof(d.vendor)-1);std::memset(d.evidence,'\n',sizeof(d.evidence)-1);
auto& rid=d.remote_id;rid.decoded=rid.has_id=rid.has_location=rid.has_operator=rid.has_altitude=rid.has_speed=true;std::memset(rid.id,'"',sizeof(rid.id)-1);rid.id_type=255;rid.aircraft_type=255;rid.latitude=-90;rid.longitude=-180;rid.operator_latitude=-90;rid.operator_longitude=-180;rid.operator_location_type=255;rid.altitude_m=100000;rid.speed_mps=10000;d.beacon.valid=true;std::memset(d.beacon.uuid,0xff,16);d.beacon.major=d.beacon.minor=65535;d.beacon.measured_power=-128;
std::memset(event.raw,0xff,sizeof(event.raw));std::memset(config.profile,'"',sizeof(config.profile)-1);
size_t largest=0;unsigned checks=0;
for(unsigned type=0;type<5;++type){event.kind=Kind(type);event.wifi_frame=type==0;event.length=type==0?1024:type==1?512:255;used=0;serialize(event);
if(record.overflowed()||counters.log_drops||used>8192){std::cerr<<"Evidence overflow\n";return 1;}++checks;
const size_t hostSlot=sizeof(ArduinoJson::detail::VariantSlot);if(hostSlot<16)return 2;
const size_t projected=record.memoryUsage()-slots(record.as<JsonVariantConst>())*(hostSlot-16);if(projected>4096)return 3;++checks;largest=std::max(largest,projected);
StaticJsonDocument<8192> parsed;if(deserializeJson(parsed,buffer)){std::cerr<<"Serialized evidence is invalid JSON\n";return 4;}++checks;
if(type<3){const char* raw=parsed[type==0?"wifi":type==1?"ble":"lora"][type==0?"raw_frame_hex":type==1?"payload_hex":"raw_hex"];if(!raw||std::strlen(raw)!=size_t(event.length)*2)return 5;++checks;if(!parsed["recon"]["remote_id"]["operator_latitude"].is<double>()||!parsed["recon"]["ibeacon"]["uuid_hex"].is<const char*>())return 6;++checks;}
std::cout<<"kind="<<type<<" target_document_bytes="<<projected<<" serialized_bytes="<<used<<"\n";
}
std::cout<<"Logger capacity: "<<checks<<" checks passed; maximum target arena use "<<largest<<" / 4096 bytes. Raw limits, full Recon metadata, 8192-byte text buffer preserved.\n";return 0;}
