#include <Arduino.h>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <new>
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
for(unsigned type=0;type<6;++type){event.kind=type==5?Kind::Lora:Kind(type);event.wifi_frame=type==0;event.length=type==0?1024:type==1?512:255;
if(type==2 || type==5) {
  new (&event.mesh_data) mesh::Result{};
  auto& m=event.mesh_data;m.protocol=type==2?mesh::Protocol::Meshtastic:mesh::Protocol::MeshCore;
  m.status=mesh::Status::Decoded;m.authentication=mesh::Authentication::Unverified;
  m.has_node=m.has_position=m.has_altitude=m.has_time=m.has_text=m.has_battery=m.has_voltage=m.has_hardware=m.has_role=true;
  std::memset(m.name,'"',39);std::memset(m.short_name,'S',4);std::memset(m.text,'\\',159);
  std::memset(m.public_key,0xaa,32);m.latitude_i=123450000;m.longitude_i=-456700000;m.altitude=-400;
  m.battery=101;m.voltage=4.2;m.timestamp=0xffffffff;m.port=4;m.role=2;m.precision_bits=32;
  event.radio.frequency_mhz=type==2?906.875:910.525;event.radio.bandwidth_khz=type==2?250:62.5;
  event.radio.sf=type==2?11:7;event.radio.cr=5;event.radio.protocol=type==2?1:2;event.radio.sync_word=type==2?0x2b:0x12;
  event.radio.preamble_symbols=type==2?16:32;std::strcpy(event.radio.name,type==2?"US LongFast":"US-NARROW");
  config.frequency=123; // Delayed writer must use immutable packet snapshot.
}
if(type==4) {new (&event.radio_coverage) lora_scan::Status{};event.radio_coverage.listen_ms[0]=30000;event.radio_coverage.listen_ms[1]=31000;event.radio_coverage.switches=1;}
used=0;serialize(event);
if(record.overflowed()||counters.log_drops||used>8192){std::cerr<<"Evidence overflow\n";return 1;}++checks;
const size_t hostSlot=sizeof(ArduinoJson::detail::VariantSlot);if(hostSlot<16)return 2;
const size_t projected=record.memoryUsage()-slots(record.as<JsonVariantConst>())*(hostSlot-16);if(projected>4096)return 3;++checks;largest=std::max(largest,projected);
StaticJsonDocument<8192> parsed;if(deserializeJson(parsed,buffer)){std::cerr<<"Serialized evidence is invalid JSON\n";return 4;}++checks;
if(type<3 || type==5){const char* raw=parsed[type==0?"wifi":type==1?"ble":"lora"][type==0?"raw_frame_hex":type==1?"payload_hex":"raw_hex"];if(!raw||std::strlen(raw)!=size_t(event.length)*2)return 5;++checks;
if(type<2){if(!parsed["recon"]["remote_id"]["operator_latitude"].is<double>()||!parsed["recon"]["ibeacon"]["uuid_hex"].is<const char*>())return 6;++checks;}
else {
 if(parsed["lora"]["frequency_mhz"].as<float>()!=event.radio.frequency_mhz || parsed.containsKey("recon"))return 7;++checks;
 if(std::fabs(parsed["lora"]["decoded"]["advertised_position"]["lat"].as<double>()-12.345)>0.000001 || parsed["gps"]["lat"].as<double>()!=event.gps.lat)return 8;++checks;
 if(parsed["lora"]["decoded"]["text"].as<std::string>().size()!=159 || !parsed["lora"]["decoded"]["external_power"].as<bool>())return 9;++checks;
 if(type==5 && std::strlen(parsed["lora"]["decoded"]["public_key_hex"].as<const char*>())!=64)return 10;++checks;
}}
if(type==4 && parsed["lora_coverage"]["meshcore_listen_ms"].as<unsigned>()!=31000)return 11;
std::cout<<"kind="<<type<<" target_document_bytes="<<projected<<" serialized_bytes="<<used<<"\n";
}
std::cout<<"Logger capacity: "<<checks<<" checks passed; maximum target arena use "<<largest<<" / 4096 bytes. Raw limits, full Recon metadata, 8192-byte text buffer preserved.\n";return 0;}
