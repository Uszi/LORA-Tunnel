#include "logger.h"
#include "hardware.h"
#include "board_config.h"
#include "config.h"
#include "mesh_header.h"
#include "version.h"
#include "ble_ad_summary.h"
#include "resource_memory.h"
#include <SD.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <cmath>
#include <algorithm>

namespace tunnel {
namespace {
QueueHandle_t queue = nullptr;
StaticQueue_t queueControl;
uint8_t* queueStorage=nullptr;
constexpr UBaseType_t QUEUE_RECORDS=board::WRITER_RECORDS;
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
// IDF preallocates a 4136-byte FIL for each slot. Uploads peak at two
// regular files; directory handles are separate. The host adapter checks this.
constexpr uint8_t SD_FILE_SLOTS=2;
#else
constexpr uint8_t SD_FILE_SLOTS=5;
#endif
TaskHandle_t writerTask=nullptr;
std::atomic<bool> mounted{false}, openSession{false}, busy{false};
File sessionFile;
uint32_t sequence=0, fileSession=0;
char buffer[8192];
size_t used=0;
uint32_t bufferedRecords=0;
// Full raw Wi-Fi plus maximum Recon/Remote ID evidence uses 3084 bytes on
// the 32-bit target; tools/check_logger_capacity.py proves the 4096-byte bound.
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
StaticJsonDocument<4096> record;
#else
StaticJsonDocument<8192> record;
#endif

void fault() {
  ++counters.storage_errors;
  counters.log_drops.fetch_add(bufferedRecords);
  bufferedRecords=0;
  mounted=false;
  if (sessionFile) sessionFile.close();
  used=0;
}
void flush() {
  if (!sessionFile || used==0) return;
  const uint32_t started=millis();
  size_t offset=0;
  while(offset<used) {
    const size_t amount=std::min(used-offset,board::LORA_SHARED_WITH_SD ? size_t(512) : used);
    xSemaphoreTake(busMutex,portMAX_DELAY);
    const size_t written=sessionFile.write(reinterpret_cast<const uint8_t*>(buffer)+offset,amount);
    if(written!=amount || sessionFile.getWriteError()) { fault(); xSemaphoreGive(busMutex); return; }
    xSemaphoreGive(busMutex);
    offset+=amount;
    // Release the shared cap SPI bus between SD blocks for RX FIFO/rearm.
    if(board::LORA_SHARED_WITH_SD) vTaskDelay(1);
  }
  xSemaphoreTake(busMutex,portMAX_DELAY);
  sessionFile.flush();
  if(sessionFile.getWriteError()) { fault(); xSemaphoreGive(busMutex); return; }
  xSemaphoreGive(busMutex);
  used=0; bufferedRecords=0;
  const uint32_t elapsed=millis()-started;
  if(elapsed>counters.flush_max_ms) counters.flush_max_ms=elapsed;
}
void hex(const uint8_t* bytes,size_t size,char* out) {
  constexpr char digits[]="0123456789abcdef";
  for (size_t i=0;i<size;i++) { out[i*2]=digits[bytes[i]>>4]; out[i*2+1]=digits[bytes[i]&15]; }
  out[size*2]=0;
}
void serializeRecon(const recon::Result& detection) {
  if(!detection.matched()) return;
  auto details=record.createNestedObject("recon");
  details["category"]=recon::categoryName(detection.category);
  details["confidence"]=recon::confidenceName(detection.confidence);
  details["label"]=detection.label;
  details["vendor"]=detection.vendor;
  details["evidence"]=detection.evidence;
  details["payload_valid"]=detection.payload_valid;
  if(detection.remote_id.decoded) {
    const auto& source=detection.remote_id;
    auto rid=details.createNestedObject("remote_id");
    rid["source"]="self_reported_broadcast";
    rid["authenticated"]=false;
    if(source.has_id) {
      rid["id"]=source.id; rid["id_type"]=source.id_type;
      rid["aircraft_type"]=source.aircraft_type;
    }
    if(source.has_location && std::isfinite(source.latitude) && std::isfinite(source.longitude)) {
      rid["latitude"]=source.latitude; rid["longitude"]=source.longitude;
    }
    if(source.has_operator && std::isfinite(source.operator_latitude) && std::isfinite(source.operator_longitude)) {
      rid["operator_latitude"]=source.operator_latitude;
      rid["operator_longitude"]=source.operator_longitude;
      rid["operator_location_type"]=source.operator_location_type;
    }
    if(source.has_altitude && std::isfinite(source.altitude_m)) rid["altitude_m"]=source.altitude_m;
    if(source.has_speed && std::isfinite(source.speed_mps)) rid["speed_mps"]=source.speed_mps;
  }
  if(detection.beacon.valid) {
    auto beacon=details.createNestedObject("ibeacon");
    char uuid[33]; hex(detection.beacon.uuid,16,uuid);
    beacon["uuid_hex"]=uuid;
    beacon["major"]=detection.beacon.major;
    beacon["minor"]=detection.beacon.minor;
    beacon["measured_power_dbm"]=detection.beacon.measured_power;
  }
}
void serialize(const Observation& event) {
  record.clear();
  record["schema"]=1;
  record["capture_mode"]=event.mode==CaptureMode::Recon?"recon":"wardriver";
  char session[9]; snprintf(session,sizeof(session),"%08lx",(unsigned long)event.session);
  record["session"]=session; record["seq"]=++sequence; record["monotonic_ms"]=event.mono_ms;
  if (event.gps.utc[0]) record["utc"]=event.gps.utc; else record["utc"]=nullptr;
  record["timestamp_basis"]="gnss_latest_sentence";
  if(event.gps.utc_age_ms!=UINT32_MAX) record["utc_age_ms"]=event.gps.utc_age_ms; else record["utc_age_ms"]=nullptr;
  auto gps=record.createNestedObject("gps");
  gps["valid"]=event.gps.valid;
  if (event.gps.age_ms!=UINT32_MAX) gps["age_ms"]=event.gps.age_ms; else gps["age_ms"]=nullptr;
  if (event.gps.valid) { gps["lat"]=event.gps.lat; gps["lon"]=event.gps.lon; }
  else { gps["lat"]=nullptr; gps["lon"]=nullptr; }
  if (event.gps.valid && std::isfinite(event.gps.altitude)) gps["alt_m"]=event.gps.altitude; else gps["alt_m"]=nullptr;
  if (std::isfinite(event.gps.hdop)) gps["hdop"]=event.gps.hdop; else gps["hdop"]=nullptr;
  gps["sats"]=event.gps.satellites;
  const size_t capturedLength=std::min(size_t(event.length),size_t(RAW_MAX));
  const size_t originalLength=std::max(size_t(event.original_length),size_t(event.length));
  if(event.kind!=Kind::Start && event.kind!=Kind::Stop) serializeRecon(event.detection);
  if (event.kind==Kind::Start || event.kind==Kind::Stop) {
    record["type"]=event.kind==Kind::Start?"session_start":"session_end";
    record["firmware"]=TUNNEL_VERSION;
    record["board"]=board::NAME;
    record["counts"]["ingress_drops"]=counters.ingress_drops.load();
    record["counts"]["log_drops"]=counters.log_drops.load();
    record["counts"]["storage_errors"]=counters.storage_errors.load();
    record["counts"]["radio_errors"]=counters.radio_errors.load();
    record["counts"]["ui_skips"]=counters.ui_skips.load();
    record["counts"]["flush_max_ms"]=counters.flush_max_ms.load();
  } else if (event.kind==Kind::Wifi) {
    record["type"]="wifi";
    auto wifi=record.createNestedObject("wifi");
    wifi["wifi_frame"]=event.wifi_frame;
    // Promiscuous frames identify a transmitter, which may be a client or NAN
    // peer. Never fabricate an AP/BSSID identity for an arbitrary transmitter.
    if(event.wifi_frame) wifi["tx_address"]=event.address;
    else wifi["bssid"]=event.address;
    wifi["ssid"]=event.name;
    wifi["rssi"]=event.rssi; wifi["channel"]=event.channel;
    wifi["frequency_mhz"]=event.channel==14?2484:2407+5*event.channel;
    wifi["security"]=event.security;
    char ssidBytes[65]; hex(event.ssid_raw,std::min(size_t(event.ssid_length),size_t(32)),ssidBytes);
    wifi["ssid_hex"]=ssidBytes; wifi["ssid_sanitized"]=event.name_sanitized;
    wifi["timestamp_basis"]=event.wifi_frame?"packet_callback":"scan_result_delivery";
    if(event.wifi_frame) {
      char bytes[RAW_MAX*2+1]; hex(event.raw,capturedLength,bytes);
      wifi["raw_frame_hex"]=bytes;
      wifi["original_len"]=originalLength; wifi["captured_len"]=capturedLength;
      wifi["frame_truncated"]=event.truncated || originalLength>capturedLength;
      wifi["fcs_present"]=false;
    }
  } else if (event.kind==Kind::Ble) {
    record["type"]="ble";
    auto ble=record.createNestedObject("ble");
    ble["address"]=event.address; ble["name"]=event.name; ble["rssi"]=event.rssi;
    ble["name_sanitized"]=event.name_sanitized;
    const char* types[]={"public","random","public_identity","random_identity"};
    ble["address_type"]=event.address_type<4?types[event.address_type]:"unknown";
    const size_t bleLength=std::min(capturedLength,size_t(512));
    char bytes[1025]; hex(event.raw,bleLength,bytes);
    ble["payload_hex"]=bytes;
    ble["payload_truncated"]=event.truncated || originalLength>bleLength;
    ble["original_len"]=originalLength; ble["captured_len"]=bleLength;
    ble["advertising_len"]=std::min(size_t(event.ble_adv_length),bleLength);
    const auto ad=summarizeBleAd(event.raw,bleLength,event.ble_adv_length);
    if(ad.malformed) ble["malformed_ad"]=true;
    if(ad.has_manufacturer) ble["manufacturer_id"]=ad.manufacturer_id;
  } else {
    record["type"]="lora";
    auto lora=record.createNestedObject("lora");
    lora["frequency_mhz"]=config.frequency; lora["bandwidth_khz"]=config.bandwidth;
    lora["spreading_factor"]=config.sf; lora["coding_rate_denominator"]=config.cr;
    lora["sync_word"]=43; lora["preamble_symbols"]=config.preamble;
    lora["profile"]=config.profile; lora["lna"]=config.lna;
    const size_t loraLength=std::min(capturedLength,size_t(255));
    lora["rssi"]=event.rssi; lora["snr"]=event.snr; lora["length"]=loraLength;
    lora["original_len"]=originalLength;
    lora["truncated"]=event.truncated || originalLength>loraLength;
    lora["radio_read_status"]="ok"; lora["crc_status"]="not_reported"; lora["payload_status"]="not_decoded";
    char bytes[511]; hex(event.raw,loraLength,bytes); lora["raw_hex"]=bytes;
    lt::MeshHeader candidate;
    if (lt::parseMeshHeader(event.raw,loraLength,candidate)) {
      lora["classification"]="meshtastic_header_candidate";
      auto header=lora.createNestedObject("candidate_header");
      char id[9];
      snprintf(id,sizeof(id),"%08lx",(unsigned long)candidate.from); header["from"]=id;
      snprintf(id,sizeof(id),"%08lx",(unsigned long)candidate.to); header["to"]=id;
      snprintf(id,sizeof(id),"%08lx",(unsigned long)candidate.id); header["packet_id"]=id;
      header["channel_hash"]=candidate.channel; header["hop_limit"]=candidate.hop_limit;
      header["hop_start"]=candidate.hop_start; header["via_mqtt"]=candidate.via_mqtt;
      header["want_ack"]=candidate.want_ack; header["next_hop_raw"]=candidate.next_hop;
      header["relay_node_raw"]=candidate.relay_node;
      header["relay_fields_available"]=candidate.hop_start>0;
    } else lora["classification"]="unknown";
  }
  const size_t length=measureJson(record);
  if (record.overflowed() || length+1>sizeof(buffer)) { ++counters.log_drops; return; }
  if (used+length+1>sizeof(buffer)) flush();
  if (!sessionFile) { ++counters.log_drops; return; }
  const size_t n=serializeJson(record,buffer+used,sizeof(buffer)-used);
  if (n!=length) { ++counters.log_drops; return; }
  used+=n; buffer[used++]='\n';
  ++bufferedRecords;
  ++counters.written; // Serialized/enqueued to write buffer, not a durability claim.
}
void task(void*) {
  Observation event;
  uint32_t lastFlush=millis();
  for (;;) {
    if (xQueueReceive(queue,&event,pdMS_TO_TICKS(100))==pdTRUE) {
      busy=true;
      if (event.kind==Kind::Start) {
        flush();
        xSemaphoreTake(busMutex,portMAX_DELAY);
        if(sessionFile) sessionFile.close();
        fileSession=event.session; sequence=0; used=0; openSession=true;
        if (mounted) {
          char path[64]; snprintf(path,sizeof(path),"/sessions/LT-%08lx.jsonl",(unsigned long)event.session);
          // Never append a new session to an old file, even on a rare ID collision.
          if (SD.exists(path)) fault();
          else {
            sessionFile=SD.open(path,FILE_WRITE);
            if (!sessionFile) fault();
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
            // Keep the 8 KiB writer batch and FAT sector cache; bound only
            // Arduino's additional per-file stdio buffer (default 4 KiB).
            else if (!sessionFile.setBufferSize(512)) fault();
#endif
          }
        }
        xSemaphoreGive(busMutex);
      }
      if (event.session==fileSession && sessionFile) serialize(event);
      else ++counters.log_drops;
      if (event.kind==Kind::Stop) {
        flush();
        xSemaphoreTake(busMutex,portMAX_DELAY);
        if(sessionFile) sessionFile.close();
        xSemaphoreGive(busMutex);
        openSession=false;
      }
      busy=false;
    }
    if (millis()-lastFlush>=2000) {
      flush();
      lastFlush=millis();
    }
  }
}
}
bool mountStorage() {
  // Call only before tasks start, or while stopped and loggerIdle().
  SD.end();
  const bool ok=SD.begin(board::SD_CS,hw::peripheralSpi(),board::SD_SPI_HZ,"/sd",SD_FILE_SLOTS);
  mounted=ok && SD.cardType()!=CARD_NONE && SD.mkdir("/sessions");
  if (!mounted) ++counters.storage_errors;
  return mounted;
}
bool storageReady() { return mounted; }
uint32_t loggerStackMargin() { return writerTask ? uxTaskGetStackHighWaterMark(writerTask) : 0; }
bool loggerBegin() {
  if(queue) return true;
  // Target-specific bounded payload storage; task/queue control stay internal.
  queueStorage=static_cast<uint8_t*>(allocatePayload(QUEUE_RECORDS*sizeof(Observation)));
  if(!queueStorage) return false;
  queue=xQueueCreateStatic(QUEUE_RECORDS,sizeof(Observation),queueStorage,&queueControl);
  if(!queue) { heap_caps_free(queueStorage); queueStorage=nullptr; return false; }
  // ESP-IDF's task stack argument is bytes, not vanilla FreeRTOS words.
  if(xTaskCreatePinnedToCore(task,"sd-writer",board::WRITER_STACK_BYTES,nullptr,1,&writerTask,0)!=pdPASS) {
    vQueueDelete(queue); queue=nullptr;
    heap_caps_free(queueStorage); queueStorage=nullptr;
    return false;
  }
  return true;
}
bool logObservation(const Observation& event) {
  if (!queue || xQueueSend(queue,&event,0)!=pdTRUE) {
    // Control commands are retried; count only an observation actually dropped.
    if(event.kind!=Kind::Start && event.kind!=Kind::Stop) ++counters.log_drops;
    return false;
  }
  return true;
}
bool loggerIdle() { return queue && uxQueueMessagesWaiting(queue)==0 && !openSession && !busy; }
bool unmountStorageForSleep() {
  if(!loggerIdle()) return false;
  SD.end(); mounted=false;
  return true;
}
}
