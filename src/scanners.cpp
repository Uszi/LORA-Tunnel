#include "scanners.h"
#include "app_types.h"
#include "board_config.h"
#include "hardware.h"
#include "config.h"
#include "text_sanitize.h"
#include "gnss_protocol.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <NimBLEDevice.h>
#include <RadioLib.h>
#include <TinyGPSPlus.h>
#include <algorithm>
#include <new>

namespace tunnel {
Counters counters;
QueueHandle_t captureQueue=nullptr;
SemaphoreHandle_t busMutex=nullptr;
std::atomic<bool> capturing{false};
std::atomic<uint32_t> sessionId{0};
std::atomic<CaptureMode> captureMode{CaptureMode::Wardriver};
namespace {
portMUX_TYPE fixMux=portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE captureMux=portMUX_INITIALIZER_UNLOCKED;
Fix fix;
TinyGPSPlus gnss;
TinyGPSCustom gnStatus(gnss,"GNRMC",2), gpStatus(gnss,"GPRMC",2);
TinyGPSCustom gnQuality(gnss,"GNGGA",6), gpQuality(gnss,"GPGGA",6);
HardwareSerial gpsSerial(1);
uint32_t gnssUartAt=0;
uint32_t gnssControlAt=0, gnssHoldMs=0;
bool gnssControlSent=false;
char gnssSoftware[64]{};
// Observe only the sentence identifier, allowlisted status and SW version text.
// Never buffer complete NMEA sentences, coordinates, timestamps or other TXT.
struct NmeaDiagnostics {
  enum Type : uint8_t { Other, Gga, Rmc, Gsa, Gsv, Txt } type=Other;
  GnssDiagnostics data;
  uint32_t lastAt=0, ggaAt=0, rmcAt=0, gsaAt=0, gsvAt=0, antennaAt=0, antennaStatusAt=0;
  gnss_protocol::GsvFields gsvFields;
  gnss_protocol::SoftwareVersionFields softwareFields;
  gnss_protocol::SkyTracker sky;
  char id[6]{}, field[64]{};
  uint16_t length=0, term=0;
  uint8_t idLength=0, fieldLength=0, parity=0, expected=0, digits=0;
  bool active=false, checksum=false, bad=false, fieldOverflow=false;
  gnss_protocol::AntennaStatus antenna=gnss_protocol::AntennaStatus::Unknown;
  int16_t value=-1;
  char status='?';

  bool selected() const {
    return (type==Gga && term==6) || (type==Rmc && term==2) ||
           (type==Gsa && term==2) || type==Gsv || (type==Txt && (term==4 || term==5));
  }
  void endTerm() {
    if(type==Txt && term>=4) {
      if(fieldOverflow || (term>5 && softwareFields.recognized)) softwareFields.invalid=true;
      else softwareFields.field(term,field,fieldLength);
    }
    if(term==0) {
      if(idLength==5 && id[0]>='A' && id[0]<='Z' && id[1]>='A' && id[1]<='Z') {
        if(!strcmp(id+2,"GGA")) type=Gga;
        else if(!strcmp(id+2,"RMC")) type=Rmc;
        else if(!strcmp(id+2,"GSA")) type=Gsa;
        else if(!strcmp(id+2,"GSV")) type=Gsv;
        else if(!strcmp(id+2,"TXT")) type=Txt;
      }
    } else if(type==Gsv) {
      if(fieldOverflow) gsvFields.invalid=true;
      else gsvFields.field(term,field,fieldLength);
      if(term==3 && !gsvFields.invalid) value=gsvFields.values[3];
    } else if(selected() && !fieldOverflow && fieldLength) {
      if(type==Rmc) {
        if(fieldLength==1 && (field[0]=='A' || field[0]=='V')) status=field[0];
      } else if(type==Txt) {
        if(term==4) antenna=gnss_protocol::antennaStatus(field,fieldLength);
      } else if(fieldLength<=3) {
        int16_t number=0;
        for(uint8_t i=0;i<fieldLength;++i) {
          if(field[i]<'0' || field[i]>'9') return;
          number=number*10+(field[i]-'0');
        }
        if(number<=255) value=number;
      }
    }
  }
  void commit(uint32_t now) {
    lastAt=now;
    switch(type) {
      case Gga: ++data.gga; ggaAt=now; data.fix_quality=value; break;
      case Rmc: ++data.rmc; rmcAt=now; data.rmc_status=status; break;
      case Gsa: ++data.gsa; gsaAt=now; data.fix_type=value; break;
      case Gsv:
        ++data.gsv; gsvAt=now; data.satellites_visible=value;
        sky.accept(id,gsvFields,now);
        break;
      case Txt:
        ++data.txt;
        // Only the software-identification field requested by Meshtastic's
        // L76K probe is exposed. Arbitrary TXT/NMEA remains private.
        if(softwareFields.recognized && !softwareFields.invalid)
          strlcpy(gnssSoftware,softwareFields.value,sizeof(gnssSoftware));
        if(antenna!=gnss_protocol::AntennaStatus::Unknown) {
          data.antenna_status=static_cast<int8_t>(antenna); antennaStatusAt=now;
          if(antenna==gnss_protocol::AntennaStatus::Open) { ++data.antenna_open_reports; antennaAt=now; }
          else if(antenna==gnss_protocol::AntennaStatus::Ok) ++data.antenna_ok_reports;
        }
        break;
      default: ++data.other; break;
    }
  }
  void feed(char c) {
    if(c=='$') {
      type=Other; length=term=0; idLength=fieldLength=parity=expected=digits=0;
      memset(id,0,sizeof(id)); memset(field,0,sizeof(field));
      active=true; checksum=bad=fieldOverflow=false; value=-1; status='?';
      antenna=gnss_protocol::AntennaStatus::Unknown;
      gsvFields=gnss_protocol::GsvFields{};
      softwareFields=gnss_protocol::SoftwareVersionFields{};
      return;
    }
    if(!active) return;
    if(c=='\r' || c=='\n') {
      if(!bad && checksum && digits==2 && parity==expected) commit(millis());
      active=false; return;
    }
    // A malformed/unterminated stream cannot grow memory or monopolize work.
    if(++length>256) { active=false; return; }
    if(checksum) {
      int digit=c>='0' && c<='9' ? c-'0' :
                c>='A' && c<='F' ? c-'A'+10 :
                c>='a' && c<='f' ? c-'a'+10 : -1;
      if(digit<0 || digits>=2) bad=true;
      else { expected=(expected<<4)|digit; ++digits; }
      return;
    }
    if(c=='*') { endTerm(); checksum=true; return; }
    parity^=static_cast<uint8_t>(c);
    if(c==',') { endTerm(); ++term; fieldLength=0; fieldOverflow=false; return; }
    if(term==0) {
      if(idLength<5) id[idLength]=c;
      if(idLength<6) ++idLength;
    } else if(selected()) {
      if(fieldLength<sizeof(field)-1) field[fieldLength++]=c;
      else fieldOverflow=true;
    }
  }
} nmeaDiagnostics;
NimBLEScan* bleScanner=nullptr;
SPIClass radioBus(FSPI);
SX1262 radio=new Module(board::LORA_CS,board::LORA_IRQ,board::LORA_RESET,board::LORA_BUSY,radioBus);
std::atomic<bool> radioIRQ{false}, wifiDone{false};
bool radioOK=false;
bool radioInitialized=false, radioInitAttempted=false;
std::atomic<bool> quietLatched{false};
bool quietFinished=false, quietShutdownOK=false;
enum class Phase { Idle, Wifi, ReconWifi, Ble, Guard };
Phase phase=Phase::Idle;
uint32_t phaseAt=0;
uint32_t reconHopAt=0;
uint8_t reconChannel=1;
std::atomic<bool> reconWifiActive{false};
// Each producer is serialized by its own driver task. Separate reusable PSRAM
// scratch objects avoid large callback-stack frames and allocate only at init.
Observation* wifiScratch=nullptr;
Observation* bleScratch=nullptr;
constexpr uint32_t RECON_DWELL_MS=200;
constexpr UBaseType_t RECON_MANAGEMENT_RESERVE=8;
constexpr size_t BLE_CAPTURE_MAX=512;
constexpr size_t LORA_CAPTURE_MAX=255;
wifi_ap_record_t aps[64];

void IRAM_ATTR onRadio() { radioIRQ.store(true,std::memory_order_relaxed); }
class BleCallbacks final: public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* device) override {
    if(!bleScratch || !capturing.load(std::memory_order_relaxed)) return;
    Observation& event=*new (bleScratch) Observation{};
    if(!beginObservation(event)) return;
    event.kind=Kind::Ble;
    const auto bleAddress=device->getAddress();
    const auto* address=bleAddress.getVal();
    snprintf(event.address,sizeof(event.address),"%02x:%02x:%02x:%02x:%02x:%02x",address[5],address[4],address[3],address[2],address[1],address[0]);
    // Names are bounded for UI; full advertisement remains available below.
    event.rssi=device->getRSSI(); event.address_type=device->getAddressType();
    const auto& bytes=device->getPayload();
    event.original_length=static_cast<uint16_t>(std::min(bytes.size(),size_t(UINT16_MAX)));
    event.length=static_cast<uint16_t>(std::min(bytes.size(),BLE_CAPTURE_MAX));
    event.ble_adv_length=device->getAdvLength();
    event.truncated=bytes.size()>BLE_CAPTURE_MAX;
    if(event.length) memcpy(event.raw,bytes.data(),event.length);
    // Shortened/complete local-name AD elements, without allocating a string.
    for(size_t p=0;p<event.length;) {
      const size_t end=event.ble_adv_length && p<event.ble_adv_length ?
        std::min(size_t(event.ble_adv_length),size_t(event.length)) : size_t(event.length);
      const size_t n=event.raw[p];
      if(!n) { p=end; continue; }
      if(p+1+n>end) break;
      if(n>=2 && (event.raw[p+1]==0x08 || event.raw[p+1]==0x09)) {
        event.name_sanitized=copyUtf8Bounded(event.name,sizeof(event.name),event.raw+p+2,n-1);
        if(event.raw[p+1]==0x09) break;
      }
      p+=n+1;
    }
    submit(event);
  }
} bleCallbacks;

void captureSsid(Observation& event) {
  const uint8_t* frame=event.raw;
  if(event.length<24 || ((frame[0]>>2)&3)!=0) return;
  const uint8_t subtype=frame[0]>>4;
  if(subtype!=8 && subtype!=5 && subtype!=4) return;
  size_t at=subtype==4?24:36;
  if(event.length<at) return;
  while(at+2<=event.length) {
    const uint8_t id=frame[at++],length=frame[at++];
    if(length>event.length-at) return;
    if(id==0) {
      if(length>32) { ++counters.malformed; return; }
      event.ssid_length=length;
      if(length) memcpy(event.ssid_raw,frame+at,length);
      event.name_sanitized=copyUtf8Bounded(event.name,sizeof(event.name),frame+at,length);
      return;
    }
    at+=length;
  }
}
void onWifiFrame(void* buffer,wifi_promiscuous_pkt_type_t packetType) {
  if(!buffer || !wifiScratch || !reconWifiActive.load(std::memory_order_relaxed) ||
     !capturing.load(std::memory_order_relaxed) ||
     (packetType!=WIFI_PKT_MGMT && packetType!=WIFI_PKT_DATA)) return;
  const auto* packet=static_cast<const wifi_promiscuous_pkt_t*>(buffer);
  // ESP-IDF's ESP32-S3 receive metadata explicitly includes the four-byte FCS.
  if(packet->rx_ctrl.rx_state || packet->rx_ctrl.sig_len<28) { ++counters.malformed; return; }
  const uint8_t* frame=packet->payload;
  const uint8_t frameType=(frame[0]>>2)&3;
  if((frame[0]&3) || (frameType!=0 && frameType!=2)) { ++counters.malformed; return; }
  // Data floods cannot consume the entire queue ahead of beacon/probe/NAN
  // management frames. Deliberate capacity drops remain visible in Drops.
  if(!captureQueue || (frameType==2 && uxQueueSpacesAvailable(captureQueue)<=RECON_MANAGEMENT_RESERVE)) {
    ++counters.ingress_drops; return;
  }
  Observation& event=*new (wifiScratch) Observation{};
  if(!beginObservation(event) || event.mode!=CaptureMode::Recon) return;
  event.kind=Kind::Wifi; event.wifi_frame=true;
  event.original_length=static_cast<uint16_t>(packet->rx_ctrl.sig_len-4);
  event.length=std::min(event.original_length,static_cast<uint16_t>(RAW_MAX));
  event.truncated=event.original_length>event.length;
  memcpy(event.raw,frame,event.length);
  // Addr2 is the transmitter; addr1 is not evidence of a nearby transmitter.
  snprintf(event.address,sizeof(event.address),"%02x:%02x:%02x:%02x:%02x:%02x",frame[10],frame[11],frame[12],frame[13],frame[14],frame[15]);
  event.rssi=packet->rx_ctrl.rssi; event.channel=packet->rx_ctrl.channel;
  strlcpy(event.security,"[UNPARSED]",sizeof(event.security));
  captureSsid(event);
  submit(event);
}
void stopReconWifi() {
  reconWifiActive.store(false,std::memory_order_relaxed);
  if(esp_wifi_set_promiscuous(false)!=ESP_OK) ++counters.radio_errors;
  if(esp_wifi_set_promiscuous_rx_cb(nullptr)!=ESP_OK) ++counters.radio_errors;
}
void startReconWifi() {
  // Called only at Start or after the BLE/guard phase; never during AP scan.
  wifiDone=false; reconChannel=1;
  wifi_promiscuous_filter_t filter{};
  filter.filter_mask=WIFI_PROMIS_FILTER_MASK_MGMT|WIFI_PROMIS_FILTER_MASK_DATA;
  esp_err_t status=esp_wifi_set_promiscuous_filter(&filter);
  if(status==ESP_OK) status=esp_wifi_set_promiscuous_rx_cb(onWifiFrame);
  if(status==ESP_OK) status=esp_wifi_set_channel(reconChannel,WIFI_SECOND_CHAN_NONE);
  if(status==ESP_OK) {
    reconWifiActive.store(true,std::memory_order_relaxed);
    status=esp_wifi_set_promiscuous(true);
  }
  if(status!=ESP_OK) {
    ++counters.radio_errors; stopReconWifi(); phase=Phase::Guard; phaseAt=millis(); return;
  }
  phase=Phase::ReconWifi; phaseAt=reconHopAt=millis();
}
void startBle() {
  phase=Phase::Ble; phaseAt=millis();
  if(!bleScanner || !bleScanner->start(config.ble_window_ms,false,true)) {
    ++counters.radio_errors; phase=Phase::Guard;
  }
}

const char* auth(wifi_auth_mode_t mode) {
  switch(mode) {
    case WIFI_AUTH_OPEN: return "[ESS]";
    case WIFI_AUTH_WEP: return "[WEP][ESS]";
    case WIFI_AUTH_WPA_PSK: return "[WPA-PSK][ESS]";
    case WIFI_AUTH_WPA2_PSK: return "[WPA2-PSK][ESS]";
    case WIFI_AUTH_WPA_WPA2_PSK: return "[WPA-PSK][WPA2-PSK][ESS]";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "[WPA2-EAP][ESS]";
    case WIFI_AUTH_WPA3_PSK: return "[WPA3-SAE][ESS]";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "[WPA2-PSK][WPA3-SAE][ESS]";
    case WIFI_AUTH_WAPI_PSK: return "[WAPI-PSK][ESS]";
    case WIFI_AUTH_OWE: return "[OWE][ESS]";
    default: return "[UNKNOWN][ESS]";
  }
}
void startWifi() {
  wifiDone=false;
  wifi_scan_config_t settings{};
  settings.show_hidden=true;
  settings.scan_type=WIFI_SCAN_TYPE_PASSIVE;
  settings.scan_time.passive=config.wifi_dwell_ms;
  const esp_err_t result=esp_wifi_scan_start(&settings,false);
  if(result==ESP_OK) { phase=Phase::Wifi; phaseAt=millis(); }
  else { ++counters.radio_errors; phase=Phase::Guard; phaseAt=millis(); }
}
void collectWifi() {
  uint16_t count=0;
  if(esp_wifi_scan_get_ap_num(&count)!=ESP_OK) { esp_wifi_clear_ap_list(); ++counters.radio_errors; return; }
  if(count>64) counters.ingress_drops.fetch_add(count-64);
  uint16_t available=std::min(count,uint16_t(64));
  if(available==0) { esp_wifi_clear_ap_list(); return; }
  if(esp_wifi_scan_get_ap_records(&available,aps)!=ESP_OK) { esp_wifi_clear_ap_list(); ++counters.radio_errors; return; }
  for(uint16_t i=0;i<available;++i) {
    Observation event;
    const auto& ap=aps[i]; event.kind=Kind::Wifi;
    snprintf(event.address,sizeof(event.address),"%02x:%02x:%02x:%02x:%02x:%02x",ap.bssid[0],ap.bssid[1],ap.bssid[2],ap.bssid[3],ap.bssid[4],ap.bssid[5]);
    // Preserve all bytes exposed by the SDK's terminated SSID field separately
    // from the safe JSON/UI text (the scan API supplies no embedded-NUL length).
    const size_t ssidLength=strnlen(reinterpret_cast<const char*>(ap.ssid),32);
    event.length=static_cast<uint16_t>(ssidLength);
    event.original_length=event.length;
    event.ssid_length=static_cast<uint8_t>(ssidLength);
    if(ssidLength) memcpy(event.ssid_raw,ap.ssid,ssidLength);
    if(ssidLength) memcpy(event.raw,ap.ssid,ssidLength);
    event.name_sanitized=copyUtf8Bounded(event.name,sizeof(event.name),ap.ssid,ssidLength);
    event.channel=ap.primary; event.rssi=ap.rssi;
    strlcpy(event.security,auth(ap.authmode),sizeof(event.security));
    submit(event);
  }
  ++counters.scan_cycles;
}
void serviceLoRa() {
  // Service at most one completed frame per loop so other work stays bounded.
  // Radio FIFO/metadata are consumed before the Wi-Fi result batch can delay us.
  if(!radioOK || !radioIRQ.exchange(false)) return;
  Observation event;
  event.kind=Kind::Lora;
  bool received=false;
  const size_t length=radio.getPacketLength();
  if(length==0 || length>LORA_CAPTURE_MAX) {
    ++counters.malformed;
  } else {
    event.length=static_cast<uint16_t>(length);
    event.original_length=event.length;
    const int status=radio.readData(event.raw,length);
    if(status==RADIOLIB_ERR_NONE) {
      // These belong to the completed frame; snapshot before rearming RX.
      event.rssi=radio.getRSSI();
      event.snr=radio.getSNR();
      received=true;
    } else {
      ++counters.radio_errors;
    }
  }
  // Rearm before GPS snapshotting or queue submission. Even when rearm fails,
  // preserve the frame already received successfully and report the radio fault.
  if(radio.startReceive()!=RADIOLIB_ERR_NONE) {
    ++counters.radio_errors;
    radioOK=false;
    hw::setLoRaFrontend(false);
  }
  if(received) submit(event);
}
}

Fix currentFix() {
  portENTER_CRITICAL(&fixMux); Fix snapshot=fix; portEXIT_CRITICAL(&fixMux);
  return snapshot;
}
void startCapture(uint32_t id,CaptureMode mode) {
  portENTER_CRITICAL(&captureMux);
  if(!quietLatched.load(std::memory_order_relaxed)) {
    sessionId.store(id,std::memory_order_relaxed);
    captureMode.store(mode,std::memory_order_relaxed);
    // Zero is reserved for observations not yet assigned to a session.
    capturing.store(id!=0,std::memory_order_relaxed);
  }
  portEXIT_CRITICAL(&captureMux);
}
void endCapture() {
  portENTER_CRITICAL(&captureMux);
  capturing.store(false,std::memory_order_relaxed);
  portEXIT_CRITICAL(&captureMux);
}
bool beginObservation(Observation& event) {
  portENTER_CRITICAL(&captureMux);
  const bool active=capturing.load(std::memory_order_relaxed);
  if(active) {
    event.session=sessionId.load(std::memory_order_relaxed);
    event.mode=captureMode.load(std::memory_order_relaxed);
    event.mono_ms=esp_timer_get_time()/1000;
  }
  portEXIT_CRITICAL(&captureMux);
  return active;
}
void submit(Observation& event) {
  if(!captureQueue || (event.session==0 && !beginObservation(event))) return;
  // Avoid holding the capture gate while acquiring the independent GNSS lock.
  event.gps=currentFix();
  portENTER_CRITICAL(&captureMux);
  // Admission and Stop are serialized: either this entire enqueue happens
  // before endCapture returns, or the stopped/old-generation event is dropped.
  if(!capturing.load(std::memory_order_relaxed) ||
     event.session!=sessionId.load(std::memory_order_relaxed) ||
     event.mode!=captureMode.load(std::memory_order_relaxed) ||
     xQueueSend(captureQueue,&event,0)!=pdTRUE) {
    ++counters.ingress_drops;
  }
  portEXIT_CRITICAL(&captureMux);
}
void gnssBegin() {
  gpsSerial.setRxBufferSize(2048);
  gpsSerial.begin(board::GNSS_BAUD,SERIAL_8N1,board::GNSS_RX,board::GNSS_TX);
  gnssUartAt=millis();
}
namespace {
void clearGnssEpoch() {
  gnss=TinyGPSPlus{};
  // Assignment clears TinyGPSPlus's linked custom-field list. Re-register all
  // four existing objects; begin() also clears their cached validity/contents.
  gnStatus.begin(gnss,"GNRMC",2); gpStatus.begin(gnss,"GPRMC",2);
  gnQuality.begin(gnss,"GNGGA",6); gpQuality.begin(gnss,"GPGGA",6);
  nmeaDiagnostics=NmeaDiagnostics{};
  portENTER_CRITICAL(&fixMux); fix=Fix{}; portEXIT_CRITICAL(&fixMux);
}
}
const char* gnssSoftwareVersion() { return gnssSoftware; }
bool gnssControlPending() { return gnssHoldMs!=0; }
bool gnssControl(GnssControl command) {
  if(capturing.load() || gnssHoldMs || (gnssControlSent && millis()-gnssControlAt<5000)) return false;
  char wire[64]; size_t length=0;
  switch(command) {
    case GnssControl::Version: length=gnss_protocol::versionQueryCommand(wire,sizeof(wire)); break;
    case GnssControl::Warm: length=gnss_protocol::warmRestartCommand(wire,sizeof(wire)); break;
    case GnssControl::Cold: length=gnss_protocol::coldRestartCommand(wire,sizeof(wire)); break;
    case GnssControl::Constellations: length=gnss_protocol::allConstellationsCommand(wire,sizeof(wire)); break;
  }
  if(!length || gpsSerial.availableForWrite()<static_cast<int>(length)) return false;
  if(gpsSerial.write(reinterpret_cast<const uint8_t*>(wire),length)!=length) return false;
  gnssControlAt=millis(); gnssControlSent=true;
  if(command==GnssControl::Version) gnssSoftware[0]=0;
  else {
    clearGnssEpoch();
    gnssHoldMs=command==GnssControl::Constellations?250:1500;
  }
  return true;
}
void gnssTick() {
  if(gnssHoldMs) {
    // Discard pre-restart/settling bytes with the same bounded receive budget.
    // No old coordinate or UTC value can survive into the new receiver epoch.
    unsigned discard=0;
    while(gpsSerial.available() && discard++<512) gpsSerial.read();
    if(millis()-gnssControlAt>=gnssHoldMs && !gpsSerial.available()) {
      clearGnssEpoch(); gnssHoldMs=0;
      gnssUartAt=millis()-1500; // Reapply output config on the next loop.
    }
    return;
  }
  if(!nmeaDiagnostics.data.output_config_attempted && millis()-gnssUartAt>=1500) {
    char command[64];
    const size_t length=gnss_protocol::diagnosticCommand(command,sizeof(command));
    if(length && gpsSerial.availableForWrite()>=static_cast<int>(length)) {
      nmeaDiagnostics.data.output_config_attempted=true;
      nmeaDiagnostics.data.output_config_sent=gpsSerial.write(reinterpret_cast<const uint8_t*>(command),length)==length;
    }
  }
  unsigned read=0;
  while(gpsSerial.available() && read++<512) {
    const char c=static_cast<char>(gpsSerial.read());
    gnss.encode(c);
    nmeaDiagnostics.feed(c);
  }
  Fix next;
  if(gnss.location.isValid()) {
    next.age_ms=gnss.location.age();
    next.valid=next.age_ms<=GPS_STALE_MS;
    // An explicit loss of fix stays invalid until a newer location arrives.
    // Expiring the invalid sentence after 1.5s could resurrect old coordinates.
    for(auto* status:{&gnStatus,&gpStatus})
      if(status->isValid() && status->age()<=next.age_ms && status->value()[0]=='V') next.valid=false;
    for(auto* quality:{&gnQuality,&gpQuality})
      if(quality->isValid() && quality->age()<=next.age_ms && quality->value()[0]=='0') next.valid=false;
    if(next.valid) { next.lat=gnss.location.lat(); next.lon=gnss.location.lng(); }
  }
  if(gnss.altitude.isValid() && gnss.altitude.age()<=GPS_STALE_MS) next.altitude=gnss.altitude.meters();
  if(gnss.hdop.isValid() && gnss.hdop.age()<=GPS_STALE_MS) next.hdop=gnss.hdop.hdop();
  if(gnss.satellites.isValid() && gnss.satellites.age()<=GPS_STALE_MS) next.satellites=gnss.satellites.value();
  // Never create wall-clock timestamps from boot time or an old GNSS sentence.
  if(gnss.date.isValid() && gnss.time.isValid() && gnss.date.age()<=1500 && gnss.time.age()<=1500 && gnss.date.year()>=2024) {
    next.utc_age_ms=gnss.time.age();
    snprintf(next.utc,sizeof(next.utc),"%04u-%02u-%02uT%02u:%02u:%02uZ",gnss.date.year(),gnss.date.month(),gnss.date.day(),gnss.time.hour(),gnss.time.minute(),gnss.time.second());
  }
  portENTER_CRITICAL(&fixMux); fix=next; portEXIT_CRITICAL(&fixMux);
}
unsigned long gnssCharacters() { return gnss.charsProcessed(); }
GnssDiagnostics gnssDiagnostics() {
  GnssDiagnostics result=nmeaDiagnostics.data;
  const uint32_t now=millis();
  result.baud=board::GNSS_BAUD;
  result.bytes=gnss.charsProcessed();
  result.checksum_ok=gnss.passedChecksum();
  result.checksum_failed=gnss.failedChecksum();
  result.sentences_with_fix=gnss.sentencesWithFix();
  if(result.gga || result.rmc || result.gsa || result.gsv || result.txt || result.other)
    result.last_sentence_age_ms=now-nmeaDiagnostics.lastAt;
  if(result.gga) result.gga_age_ms=now-nmeaDiagnostics.ggaAt;
  if(result.rmc) result.rmc_age_ms=now-nmeaDiagnostics.rmcAt;
  if(result.gsa) result.gsa_age_ms=now-nmeaDiagnostics.gsaAt;
  if(result.gsv) result.gsv_age_ms=now-nmeaDiagnostics.gsvAt;
  if(result.antenna_open_reports) result.antenna_open_age_ms=now-nmeaDiagnostics.antennaAt;
  if(result.antenna_status>=0) result.antenna_status_age_ms=now-nmeaDiagnostics.antennaStatusAt;
  nmeaDiagnostics.sky.snapshot(result.sky,now);
  result.parser_location_valid=gnss.location.isValid();
  if(result.parser_location_valid) result.location_age_ms=gnss.location.age();
  result.date_valid=gnss.date.isValid();
  result.time_valid=gnss.time.isValid();
  return result;
}
bool scannersBegin() {
  if(quietLatched.load(std::memory_order_relaxed)) return false;
  if(!wifiScratch) wifiScratch=static_cast<Observation*>(heap_caps_malloc(sizeof(Observation),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!bleScratch) bleScratch=static_cast<Observation*>(heap_caps_malloc(sizeof(Observation),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!wifiScratch || !bleScratch) return false;
  WiFi.persistent(false); WiFi.setAutoReconnect(false); WiFi.mode(WIFI_STA); WiFi.disconnect(false,false);
  // Passive scan only. No credentials are used for association in this milestone.
  wifi_country_t country{}; memcpy(country.cc,"US",3); country.schan=1; country.nchan=11; country.policy=WIFI_COUNTRY_POLICY_MANUAL;
  if(esp_wifi_set_country(&country)!=ESP_OK) ++counters.radio_errors;
  WiFi.onEvent([](WiFiEvent_t event) { if(event==ARDUINO_EVENT_WIFI_SCAN_DONE) wifiDone=true; });
  NimBLEDevice::init("LORA Tunnel");
  bleScanner=NimBLEDevice::getScan();
  bleScanner->setScanCallbacks(&bleCallbacks,true);
  bleScanner->setActiveScan(false);
  bleScanner->setInterval(100); bleScanner->setWindow(80); bleScanner->setMaxResults(0);
  if(!config.lora_enabled) return true;
  radioBus.begin(board::LORA_SCK,board::LORA_MISO,board::LORA_MOSI,board::LORA_CS);
  radioInitAttempted=true;
  const int state=radio.begin(config.frequency,config.bandwidth,config.sf,config.cr,0x2b,0,config.preamble,board::LORA_TCXO_VOLTS,false);
  if(state!=RADIOLIB_ERR_NONE) { ++counters.radio_errors; return true; }
  radioInitialized=true;
  const int switchState=radio.setDio2AsRfSwitch(true);
  if(switchState!=RADIOLIB_ERR_NONE) { ++counters.radio_errors; return true; }
  radio.setDio1Action(onRadio);
  radioOK=true;
  return true;
}
void scannersStart() {
  if(quietLatched.load(std::memory_order_relaxed) || !bleScanner) return;
  const bool wardriver=captureMode.load(std::memory_order_relaxed)==CaptureMode::Wardriver;
  if(wardriver) startWifi(); else startReconWifi();
  if(radioOK && wardriver) {
    radio.standby(); hw::setLoRaFrontend(true,config.lna); radioIRQ=false;
    if(radio.startReceive()!=RADIOLIB_ERR_NONE) { ++counters.radio_errors; radioOK=false; hw::setLoRaFrontend(false); }
  } else {
    radioIRQ=false;
    if(radioOK) radio.standby();
    hw::setLoRaFrontend(false);
  }
}
void scannersStop() {
  if(quietFinished) return;
  if(phase==Phase::Wifi) esp_wifi_scan_stop();
  stopReconWifi();
  if(bleScanner) bleScanner->stop();
  esp_wifi_clear_ap_list(); phase=Phase::Idle;
  if(radioOK) radio.standby();
  radioIRQ=false; wifiDone=false;
  hw::setLoRaFrontend(false);
}
bool scannersEnterQuiet() {
  // The capture gate also prevents a future session from reviving callbacks
  // after a partial shutdown. Normal startup is possible only after reboot.
  portENTER_CRITICAL(&captureMux);
  if(capturing.load(std::memory_order_relaxed)) {
    portEXIT_CRITICAL(&captureMux);
    return false;
  }
  const bool alreadyQuiet=quietLatched.exchange(true,std::memory_order_relaxed);
  portEXIT_CRITICAL(&captureMux);
  if(alreadyQuiet) return quietFinished && quietShutdownOK;

  scannersStop();
  // A failed initialization does not prove the chip reached a quiet state.
  bool loraOff=!radioInitAttempted;
  if(radioInitialized) {
    const int standbyState=radio.standby();
    const int sleepState=radio.sleep(true);
    loraOff=standbyState==RADIOLIB_ERR_NONE && sleepState==RADIOLIB_ERR_NONE;
  }
  hw::setLoRaFrontend(false);
  radioOK=false;
  radioIRQ=false;

  // clearAll=false retains library-owned objects; this module deliberately
  // discards its scanner reference and never reinitializes them before reboot.
  const bool bleStopped=NimBLEDevice::deinit(false);
  bleScanner=nullptr;
  const bool bleOff=bleStopped && !NimBLEDevice::isInitialized() &&
      esp_bt_controller_get_status()==ESP_BT_CONTROLLER_STATUS_IDLE;
  const bool wifiStopped=WiFi.mode(WIFI_OFF);
  wifi_mode_t remainingMode=WIFI_MODE_NULL;
  const bool wifiOff=wifiStopped && esp_wifi_get_mode(&remainingMode)==ESP_ERR_WIFI_NOT_INIT;
  wifiDone=false;
  quietShutdownOK=loraOff && bleOff && wifiOff;
  quietFinished=true;
  if(!loraOff) ++counters.radio_errors;
  if(!bleOff) ++counters.radio_errors;
  if(!wifiOff) ++counters.radio_errors;
  return quietShutdownOK;
}
void scannersTick() {
  if(quietLatched.load(std::memory_order_relaxed) || !capturing.load()) return;
  if(captureMode.load(std::memory_order_relaxed)==CaptureMode::Wardriver) serviceLoRa();
  if(phase==Phase::Wifi && wifiDone.exchange(false)) {
    collectWifi(); startBle();
  } else if(phase==Phase::Wifi && millis()-phaseAt>15000) {
    esp_wifi_scan_stop(); esp_wifi_clear_ap_list(); ++counters.radio_errors;
    phase=Phase::Guard; phaseAt=millis();
  } else if(phase==Phase::ReconWifi) {
    const uint32_t now=millis();
    // Finish only after channel 11's own dwell. A slow UI/SD iteration may
    // extend the sweep; it must not cause its later channels to be skipped.
    if(reconChannel==11 && now-reconHopAt>=RECON_DWELL_MS) {
      stopReconWifi(); ++counters.scan_cycles; startBle();
    } else if(now-reconHopAt>=RECON_DWELL_MS && reconChannel<11) {
      ++reconChannel; reconHopAt=now;
      if(esp_wifi_set_channel(reconChannel,WIFI_SECOND_CHAN_NONE)!=ESP_OK) {
        ++counters.radio_errors; stopReconWifi(); phase=Phase::Guard; phaseAt=now;
      }
    }
  } else if(phase==Phase::Ble && millis()-phaseAt>config.ble_window_ms+100) {
    // Explicit stop/guard ensures the scheduler does not start Wi-Fi over BLE.
    bleScanner->stop(); phase=Phase::Guard; phaseAt=millis();
  } else if(phase==Phase::Guard && millis()-phaseAt>=100) {
    if(captureMode.load(std::memory_order_relaxed)==CaptureMode::Recon) startReconWifi(); else startWifi();
  }
}
const char* scanPhase() {
  if(quietLatched.load(std::memory_order_relaxed)) return "Quiet until reboot";
  switch(phase) { case Phase::Wifi:return "Wi-Fi passive"; case Phase::ReconWifi:return "Recon Wi-Fi frames"; case Phase::Ble:return "BLE passive"; case Phase::Guard:return "Radio guard"; default:return "Ready"; }
}
bool loraReady() { return radioOK; }
}
